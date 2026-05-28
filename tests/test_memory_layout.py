"""Memory layout consistency tests.

Validates that the memory layout constants in memory.inc are internally
consistent — i.e., no component overlaps another, the module area is properly
bounded, the stack canary fits between the kernel and the stack, and the TPA
is above all resident data.

With v0.9.14's relocatable modules, FS/MM/SHELL positions are dynamic (packed
sequentially from MODULE_FIRST_BASE by the kernel).  These tests validate the
fixed anchors and invariants — actual module overlap is checked at boot time.
"""

import pytest
from tests.harness.constants import (
    LOADER_OFF,
    KERNEL_OFF,
    MODULE_FIRST_BASE,
    MODULE_AREA_END,
    DIR_SCRATCH_BUF,
    STACK_CANARY_ADDR,
    STACK_CANARY_SIZE,
    USER_PROG_BASE,
    USER_PROG_END,
    USER_PROG_MAX,
    ARGV_TABLE,
    SHELL_SAVED_SP,
)

# --- Layout parameters (sector sizes = maximum allowed) -----------------------
SECTOR = 512
KERNEL_MAX_SECTORS_RELEASE = 8   # 4 KB
KERNEL_MAX_SECTORS_DEBUG = 14    # 7 KB (debug adds serial, asserts, canary code)
SP_INITIAL = 0x7C00              # Set by MBR, stack grows downward


class TestModuleAreaLayout:
    """Verify module area anchors are consistent."""

    def test_module_first_base_is_loader_off(self):
        """First module replaces LOADER at 0x0800."""
        assert MODULE_FIRST_BASE == LOADER_OFF

    def test_module_area_end_is_kernel(self):
        """Module area must end at kernel start."""
        assert MODULE_AREA_END == KERNEL_OFF

    def test_module_area_has_space(self):
        """Module area must be at least 8 KB (to fit minimal FS+MM+SHELL)."""
        area_size = MODULE_AREA_END - MODULE_FIRST_BASE
        assert area_size >= 8 * 1024, (
            f"Module area is only {area_size} bytes — too small"
        )

    def test_dir_scratch_buf_in_module_area(self):
        """Directory scratch buffer must be within module area bounds."""
        assert MODULE_FIRST_BASE <= DIR_SCRATCH_BUF < MODULE_AREA_END, (
            f"DIR_SCRATCH_BUF 0x{DIR_SCRATCH_BUF:04X} outside module area "
            f"(0x{MODULE_FIRST_BASE:04X}–0x{MODULE_AREA_END:04X})"
        )

    def test_dir_scratch_buf_near_kernel(self):
        """Scratch buffer should be near top of module area (won't be overwritten)."""
        assert DIR_SCRATCH_BUF >= MODULE_AREA_END - 1024, (
            f"DIR_SCRATCH_BUF 0x{DIR_SCRATCH_BUF:04X} is too far from kernel — "
            f"risks being overwritten by modules"
        )

    def test_kernel_debug_below_canary(self):
        """Debug kernel (largest variant) must not overlap stack canary."""
        kernel_end = KERNEL_OFF + KERNEL_MAX_SECTORS_DEBUG * SECTOR
        assert kernel_end <= STACK_CANARY_ADDR, (
            f"Debug kernel end 0x{kernel_end:04X} overlaps canary at "
            f"0x{STACK_CANARY_ADDR:04X}"
        )

    def test_kernel_release_below_canary(self):
        """Release kernel must not overlap stack canary."""
        kernel_end = KERNEL_OFF + KERNEL_MAX_SECTORS_RELEASE * SECTOR
        assert kernel_end <= STACK_CANARY_ADDR, (
            f"Release kernel end 0x{kernel_end:04X} overlaps canary at "
            f"0x{STACK_CANARY_ADDR:04X}"
        )


class TestStackLayout:
    """Verify stack zone is properly bounded."""

    def test_canary_below_stack_top(self):
        """Stack canary must be well below SP initial value."""
        canary_end = STACK_CANARY_ADDR + STACK_CANARY_SIZE
        assert canary_end < SP_INITIAL, (
            f"Canary ends at 0x{canary_end:04X} but SP starts at "
            f"0x{SP_INITIAL:04X}"
        )

    def test_minimum_stack_size(self):
        """Usable stack must be at least 1 KB (512 bytes absolute minimum)."""
        usable_stack = SP_INITIAL - (STACK_CANARY_ADDR + STACK_CANARY_SIZE)
        assert usable_stack >= 1024, (
            f"Usable stack is only {usable_stack} bytes (need >= 1024)"
        )

    def test_stack_size_reasonable(self):
        """Stack should not exceed 8 KB (would indicate a layout mistake)."""
        usable_stack = SP_INITIAL - (STACK_CANARY_ADDR + STACK_CANARY_SIZE)
        assert usable_stack <= 8192, (
            f"Stack is {usable_stack} bytes — suspiciously large, check layout"
        )


class TestTPALayout:
    """Verify Transient Program Area is correctly positioned."""

    def test_tpa_above_stack_metadata(self):
        """TPA must start at or above the post-boot VBR/metadata area."""
        assert USER_PROG_BASE >= SP_INITIAL, (
            f"TPA at 0x{USER_PROG_BASE:04X} overlaps stack region "
            f"(SP=0x{SP_INITIAL:04X})"
        )

    def test_tpa_above_argv_table(self):
        """TPA must not overlap the argv table."""
        assert USER_PROG_BASE > SHELL_SAVED_SP, (
            f"TPA at 0x{USER_PROG_BASE:04X} overlaps SHELL_SAVED_SP "
            f"at 0x{SHELL_SAVED_SP:04X}"
        )

    def test_tpa_size_matches_constants(self):
        """USER_PROG_MAX must equal USER_PROG_END - USER_PROG_BASE + 1."""
        actual_size = USER_PROG_END - USER_PROG_BASE + 1
        assert actual_size == USER_PROG_MAX, (
            f"TPA size mismatch: END-BASE+1 = {actual_size}, "
            f"but USER_PROG_MAX = {USER_PROG_MAX}"
        )

    def test_tpa_within_segment_zero(self):
        """TPA must not exceed the 64 KB segment-0 boundary."""
        assert USER_PROG_END <= 0xFFFF, (
            f"TPA end 0x{USER_PROG_END:04X} exceeds segment 0 limit (0xFFFF)"
        )

    def test_tpa_minimum_size(self):
        """TPA must be at least 16 KB for useful programs."""
        assert USER_PROG_MAX >= 16 * 1024, (
            f"TPA is only {USER_PROG_MAX} bytes — too small for programs"
        )


class TestMetadataLayout:
    """Verify shell/kernel shared metadata is in valid location."""

    def test_argv_table_in_postboot_area(self):
        """Argv table must be between SP and TPA (in post-boot VBR space)."""
        assert SP_INITIAL <= ARGV_TABLE < USER_PROG_BASE, (
            f"ARGV_TABLE 0x{ARGV_TABLE:04X} not in post-boot area "
            f"(0x{SP_INITIAL:04X}–0x{USER_PROG_BASE:04X})"
        )

    def test_shell_saved_sp_in_postboot_area(self):
        """SHELL_SAVED_SP must be between SP and TPA."""
        assert SP_INITIAL <= SHELL_SAVED_SP < USER_PROG_BASE, (
            f"SHELL_SAVED_SP 0x{SHELL_SAVED_SP:04X} not in post-boot area"
        )

    def test_argv_below_saved_sp(self):
        """Argv table should be below SHELL_SAVED_SP (it's a larger region)."""
        assert ARGV_TABLE <= SHELL_SAVED_SP, (
            f"ARGV_TABLE 0x{ARGV_TABLE:04X} is above SHELL_SAVED_SP "
            f"0x{SHELL_SAVED_SP:04X}"
        )
