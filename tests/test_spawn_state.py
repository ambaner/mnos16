"""Unit tests for SYS_SPAWN state management.

Tests cover:
  - spawn_push: pushing parent filenames onto the spawn stack
  - spawn depth tracking and overflow protection
  - Trampoline install/skip logic (outermost vs nested)
  - spawn_rollback_if_pending: undo spawn state on exec error
  - Nested rollback (depth > 1 after rollback keeps trampoline)

These tests exercise the spawn state machine in isolation using extracted
routines from kernel_syscall.inc, running in Unicorn emulation.
"""

import struct
import pytest
from pathlib import Path
from tests.harness.emulator import MiniOSEmulator
from tests.harness.assembler import assemble_stub
from tests.harness.constants import (
    SHELL_SAVED_SP, CODE_BASE,
)
from tests.conftest import register_coverage


# ─── Fixtures ─────────────────────────────────────────────────────────────────

@pytest.fixture(scope="module")
def spawn_state_bin():
    """Assembled spawn_state stub binary."""
    return assemble_stub("stub_spawn_state")


# ─── Coverage tracking ────────────────────────────────────────────────────────

_all_executed: set[int] = set()
_all_edges: set[tuple[int, int]] = set()
_binary_size: int = 0
_code_base: int = 0
_binary_path: Path | None = None

# ─── Constants from the stub layout ──────────────────────────────────────────

SPAWN_MAX_DEPTH = 4
# We need to find data offsets in the binary. The data section is at the end.
# We'll find them by searching for the spawn_depth label (first zero byte of
# the data area after code).


def _find_data_offsets(bin_path: Path) -> dict:
    """Find data section offsets in the stub binary.

    The stub ends with:
      spawn_depth:        db 0
      spawn_parent_stack: times 44 db 0  (4*11)
      spawn_saved_ret:    dw 0
      spawn_pending:      db 0
    Total data section: 1 + 44 + 2 + 1 = 48 bytes
    """
    data = bin_path.read_bytes()
    data_size = 1 + (SPAWN_MAX_DEPTH * 11) + 2 + 1  # 48 bytes
    # Data starts at end - 48
    base = CODE_BASE + len(data) - data_size
    return {
        "spawn_depth": base,
        "spawn_parent_stack": base + 1,
        "spawn_saved_ret": base + 1 + (SPAWN_MAX_DEPTH * 11),
        "spawn_pending": base + 1 + (SPAWN_MAX_DEPTH * 11) + 2,
    }


# ─── Helpers ──────────────────────────────────────────────────────────────────

# Fake stack slot address for trampoline tests
FAKE_STACK_SLOT = 0x7BFC  # Just below SHELL_SAVED_SP

# Test filename: "MNMON   MNX" (11 bytes, padded)
TEST_FNAME = b'MNMON   MNX'
TEST_FNAME_2 = b'EDIT    MNX'
FNAME_ADDR = 0x4000  # Where we place the test filename in memory


def _setup(emu: MiniOSEmulator, bin_path: Path, test_num: int,
           setup_trampoline: bool = False) -> dict:
    """Load stub, configure for test, return data offsets."""
    global _binary_size, _code_base, _binary_path
    emu.load(bin_path)
    _binary_size = emu.code_size
    _code_base = emu.code_base
    _binary_path = bin_path

    offsets = _find_data_offsets(bin_path)

    # Clear spawn state
    emu.write_byte(offsets["spawn_depth"], 0)
    for i in range(SPAWN_MAX_DEPTH * 11):
        emu.write_byte(offsets["spawn_parent_stack"] + i, 0)
    emu.write_word(offsets["spawn_saved_ret"], 0)
    emu.write_byte(offsets["spawn_pending"], 0)

    # Write test filename at FNAME_ADDR
    emu.write_bytes(FNAME_ADDR, TEST_FNAME)
    emu.set_reg("bx", FNAME_ADDR)

    # Set test number in AH
    emu.set_reg("ax", test_num << 8)

    if setup_trampoline:
        # Set up SHELL_SAVED_SP → FAKE_STACK_SLOT
        # [SHELL_SAVED_SP] = FAKE_STACK_SLOT (the address of the slot)
        emu.write_word(SHELL_SAVED_SP, FAKE_STACK_SLOT)
        # [FAKE_STACK_SLOT] = 0xBEEF (fake original return address)
        emu.write_word(FAKE_STACK_SLOT, 0xBEEF)

    return offsets


def _run(emu: MiniOSEmulator):
    """Run the emulator and track coverage."""
    emu.run()
    _all_executed.update(emu.coverage_in_binary)
    _all_edges.update(emu.edges_in_binary)


# ─── Tests ────────────────────────────────────────────────────────────────────

class TestSpawnPush:
    """Test spawn_push — pushing parent filenames onto the stack."""

    def test_push_one_increments_depth(self, emu, spawn_state_bin):
        """Single push → spawn_depth = 1."""
        offsets = _setup(emu, spawn_state_bin, 0x01)
        _run(emu)
        assert emu.read_byte(offsets["spawn_depth"]) == 1

    def test_push_one_stores_filename(self, emu, spawn_state_bin):
        """Single push stores the 11-byte filename at stack[0]."""
        offsets = _setup(emu, spawn_state_bin, 0x01)
        _run(emu)
        stored = emu.read_bytes(offsets["spawn_parent_stack"], 11)
        assert stored == TEST_FNAME

    def test_push_max_depth(self, emu, spawn_state_bin):
        """Push SPAWN_MAX_DEPTH times → depth = SPAWN_MAX_DEPTH."""
        offsets = _setup(emu, spawn_state_bin, 0x02)
        _run(emu)
        assert emu.read_byte(offsets["spawn_depth"]) == SPAWN_MAX_DEPTH

    def test_push_max_all_slots_filled(self, emu, spawn_state_bin):
        """All stack slots contain the filename after max pushes."""
        offsets = _setup(emu, spawn_state_bin, 0x02)
        _run(emu)
        for i in range(SPAWN_MAX_DEPTH):
            stored = emu.read_bytes(offsets["spawn_parent_stack"] + i * 11, 11)
            assert stored == TEST_FNAME, f"Stack slot {i} mismatch"

    def test_push_overflow_sets_cf(self, emu, spawn_state_bin):
        """Push beyond max → CF set."""
        offsets = _setup(emu, spawn_state_bin, 0x03)
        _run(emu)
        assert emu.cf, "CF should be set on overflow"
        # Depth should remain at SPAWN_MAX_DEPTH (not incremented)
        assert emu.read_byte(offsets["spawn_depth"]) == SPAWN_MAX_DEPTH


class TestTrampolineInstall:
    """Test trampoline install and skip logic."""

    def test_trampoline_installed_on_first_spawn(self, emu, spawn_state_bin):
        """First spawn installs trampoline at [SHELL_SAVED_SP] addr."""
        offsets = _setup(emu, spawn_state_bin, 0x05, setup_trampoline=True)
        _run(emu)
        # spawn_saved_ret should hold the original value (0xBEEF)
        assert emu.read_word(offsets["spawn_saved_ret"]) == 0xBEEF
        # The stack slot should now hold the trampoline address (not 0xBEEF)
        slot_value = emu.read_word(FAKE_STACK_SLOT)
        assert slot_value != 0xBEEF, "Trampoline should replace original ret addr"

    def test_saved_ret_preserved_on_nested(self, emu, spawn_state_bin):
        """Second push does NOT overwrite spawn_saved_ret."""
        offsets = _setup(emu, spawn_state_bin, 0x06, setup_trampoline=True)
        _run(emu)
        # spawn_saved_ret should still be 0xBEEF (original from first push)
        assert emu.read_word(offsets["spawn_saved_ret"]) == 0xBEEF
        # Depth should be 2
        assert emu.read_byte(offsets["spawn_depth"]) == 2


class TestSpawnRollback:
    """Test spawn_rollback_if_pending."""

    def test_rollback_outermost_clears_depth(self, emu, spawn_state_bin):
        """Rollback from depth=1 → depth=0."""
        offsets = _setup(emu, spawn_state_bin, 0x04, setup_trampoline=True)
        _run(emu)
        assert emu.read_byte(offsets["spawn_depth"]) == 0

    def test_rollback_outermost_clears_saved_ret(self, emu, spawn_state_bin):
        """Rollback from depth=1 → spawn_saved_ret = 0."""
        offsets = _setup(emu, spawn_state_bin, 0x04, setup_trampoline=True)
        _run(emu)
        assert emu.read_word(offsets["spawn_saved_ret"]) == 0

    def test_rollback_restores_original_ret(self, emu, spawn_state_bin):
        """Rollback restores the original ret addr at [SHELL_SAVED_SP]."""
        offsets = _setup(emu, spawn_state_bin, 0x07, setup_trampoline=True)
        _run(emu)
        # The stack slot should be restored to 0xBEEF
        slot_value = emu.read_word(FAKE_STACK_SLOT)
        assert slot_value == 0xBEEF, f"Expected 0xBEEF, got 0x{slot_value:04X}"

    def test_rollback_clears_pending(self, emu, spawn_state_bin):
        """Rollback clears spawn_pending flag."""
        offsets = _setup(emu, spawn_state_bin, 0x04, setup_trampoline=True)
        _run(emu)
        assert emu.read_byte(offsets["spawn_pending"]) == 0

    def test_rollback_nested_keeps_depth(self, emu, spawn_state_bin):
        """Rollback at depth=2 → depth=1 (not 0)."""
        offsets = _setup(emu, spawn_state_bin, 0x08, setup_trampoline=True)
        _run(emu)
        assert emu.read_byte(offsets["spawn_depth"]) == 1

    def test_rollback_nested_keeps_saved_ret(self, emu, spawn_state_bin):
        """Rollback at depth=2 → spawn_saved_ret still non-zero."""
        offsets = _setup(emu, spawn_state_bin, 0x08, setup_trampoline=True)
        _run(emu)
        # spawn_saved_ret should still be 0xBEEF (trampoline is still needed)
        assert emu.read_word(offsets["spawn_saved_ret"]) == 0xBEEF

    def test_rollback_nested_trampoline_intact(self, emu, spawn_state_bin):
        """Rollback at depth=2 → trampoline still on stack."""
        offsets = _setup(emu, spawn_state_bin, 0x08, setup_trampoline=True)
        _run(emu)
        # Stack slot should still have trampoline (not restored to 0xBEEF)
        slot_value = emu.read_word(FAKE_STACK_SLOT)
        assert slot_value != 0xBEEF, "Trampoline should remain at nested depth"


# ─── Coverage report ──────────────────────────────────────────────────────────

@pytest.fixture(autouse=True, scope="module")
def _report_coverage(spawn_state_bin):
    """Register coverage data at end of module."""
    yield
    if _binary_size > 0:
        register_coverage(
            "spawn_state",
            _binary_size,
            len(_all_executed),
            edges=_all_edges,
            binary_path=_binary_path,
        )
