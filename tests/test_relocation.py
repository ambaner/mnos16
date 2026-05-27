"""Relocation system tests.

Tests for the MNEX v2 relocatable module toolchain:
  - gen_relocs.py: relocation table generation via delta comparison
  - pack_module.py: module packaging with pre-biasing
  - Kernel apply_relocs: load-time relocation patching (simulated)
  - Shell program relocation: user .MNX programs patched at TPA

These tests verify that relocatable modules and programs are correctly
built and that the kernel/shell can patch them to run at any address.
"""

import math
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parent.parent
TOOLS_DIR = REPO_ROOT / "tools"
GEN_RELOCS = TOOLS_DIR / "gen_relocs.py"
PACK_MODULE = TOOLS_DIR / "pack_module.py"
NASM_DIR = TOOLS_DIR / "nasm"
NASM_EXE = NASM_DIR / "nasm.exe"
BUILD_DIR = REPO_ROOT / "build" / "boot"

PYTHON = sys.executable


# =============================================================================
# Helpers
# =============================================================================

def run_tool(args, check=True):
    """Run a tool and return result."""
    result = subprocess.run(
        [PYTHON] + [str(a) for a in args],
        capture_output=True, text=True
    )
    if check and result.returncode != 0:
        raise RuntimeError(f"Tool failed:\n{result.stderr}")
    return result


def create_test_module(tmp_path, code, *, includes=None):
    """Write a minimal NASM module source that uses RELOC_BASE."""
    src = tmp_path / "test_module.asm"
    preamble = """%ifndef RELOC_BASE
%define RELOC_BASE 0
%endif
[BITS 16]
[ORG RELOC_BASE]
"""
    src.write_text(preamble + code)
    return src


def assemble_raw(src_path, tmp_path, defines=None):
    """Assemble a source file to raw binary (ORG 0)."""
    out = tmp_path / "raw.bin"
    cmd = [str(NASM_EXE), '-f', 'bin', '-o', str(out), str(src_path)]
    if defines:
        for d in defines:
            cmd.extend(['-D', d])
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"NASM failed:\n{result.stderr}")
    return out


# =============================================================================
# gen_relocs.py tests
# =============================================================================

class TestGenRelocs:
    """Test relocation table generation."""

    def test_simple_absolute_reference(self, tmp_path):
        """A `mov si, label` should produce one relocation."""
        src = create_test_module(tmp_path, """
entry:
    mov si, my_data
    ret
my_data:
    db 'hello', 0
""")
        rel_out = tmp_path / "test.rel"
        run_tool([GEN_RELOCS, src, '--nasm', str(NASM_EXE),
                  '--header-size', '0', '-o', str(rel_out)])

        rel_data = rel_out.read_bytes()
        assert len(rel_data) >= 2, "Should have at least one relocation"
        # Each reloc is 2 bytes
        reloc_count = len(rel_data) // 2
        assert reloc_count == 1, f"Expected 1 reloc, got {reloc_count}"

    def test_no_relocs_for_relative_jumps(self, tmp_path):
        """Near jumps (JMP, CALL) are IP-relative — no relocations needed."""
        src = create_test_module(tmp_path, """
entry:
    jmp short .skip
    nop
.skip:
    call helper
    ret
helper:
    ret
""")
        rel_out = tmp_path / "test.rel"
        run_tool([GEN_RELOCS, src, '--nasm', str(NASM_EXE),
                  '--header-size', '0', '-o', str(rel_out)])

        rel_data = rel_out.read_bytes()
        assert len(rel_data) == 0, "Relative jumps should produce zero relocs"

    def test_multiple_absolute_references(self, tmp_path):
        """Multiple mov reg, label should each produce a relocation."""
        src = create_test_module(tmp_path, """
entry:
    mov si, data1
    mov di, data2
    mov bx, data3
    ret
data1: db 'A', 0
data2: db 'B', 0
data3: db 'C', 0
""")
        rel_out = tmp_path / "test.rel"
        run_tool([GEN_RELOCS, src, '--nasm', str(NASM_EXE),
                  '--header-size', '0', '-o', str(rel_out)])

        rel_data = rel_out.read_bytes()
        reloc_count = len(rel_data) // 2
        assert reloc_count == 3, f"Expected 3 relocs, got {reloc_count}"

    def test_dw_label_produces_reloc(self, tmp_path):
        """A `dw label` data reference should produce a relocation."""
        src = create_test_module(tmp_path, """
entry:
    mov si, [table]
    ret
table:
    dw entry
    dw handler
handler:
    ret
""")
        rel_out = tmp_path / "test.rel"
        run_tool([GEN_RELOCS, src, '--nasm', str(NASM_EXE),
                  '--header-size', '0', '-o', str(rel_out)])

        rel_data = rel_out.read_bytes()
        reloc_count = len(rel_data) // 2
        # 1 for `mov si, [table]` + 2 for `dw entry` + `dw handler`
        assert reloc_count >= 3, f"Expected >= 3 relocs, got {reloc_count}"

    def test_header_size_skips_bytes(self, tmp_path):
        """With --header-size=4, bytes 0-3 should be excluded from relocs."""
        src = create_test_module(tmp_path, """
; Fake header (4 bytes that happen to contain values matching delta)
header: dw 0x0100    ; This would be a false positive without header skip
        dw 0x0000

entry:
    mov si, mystr
    ret
mystr: db 'X', 0
""")
        # Without header skip — might get false positive
        rel_no_skip = tmp_path / "no_skip.rel"
        run_tool([GEN_RELOCS, src, '--nasm', str(NASM_EXE),
                  '--header-size', '0', '-o', str(rel_no_skip)])

        # With header skip
        rel_skip = tmp_path / "skip.rel"
        run_tool([GEN_RELOCS, src, '--nasm', str(NASM_EXE),
                  '--header-size', '4', '-o', str(rel_skip)])

        skip_count = len(rel_skip.read_bytes()) // 2
        # The entry `mov si, mystr` should still be caught
        assert skip_count >= 1


# =============================================================================
# pack_module.py tests
# =============================================================================

class TestPackModule:
    """Test module packaging with pre-biasing."""

    def test_basic_packaging(self, tmp_path):
        """Module is correctly packaged with v2 header."""
        # Create a tiny raw binary (8 bytes of code)
        raw = tmp_path / "raw.bin"
        raw.write_bytes(b'\xBE\x06\x00\xC3' + b'\x00' * 4)  # mov si, 0x0006; ret

        # Create reloc file (one reloc at offset 1 — the immediate in mov si)
        rel = tmp_path / "test.rel"
        rel.write_bytes(struct.pack('<H', 1))

        out = tmp_path / "out.sys"
        run_tool([PACK_MODULE, raw, rel, '--magic', 'TEST', '-o', str(out)])

        data = out.read_bytes()
        # Check header
        assert data[0:4] == b'TEST'
        sector_count = struct.unpack_from('<H', data, 4)[0]
        assert sector_count == math.ceil(len(data) / 512)
        flags = struct.unpack_from('<H', data, 6)[0]
        assert flags == 0x0001  # MNEX_V2_FLAG_RELOC
        reloc_count = struct.unpack_from('<H', data, 8)[0]
        assert reloc_count == 1
        entry_offset = struct.unpack_from('<H', data, 10)[0]
        # prefix = 12 + 1*2 = 14
        assert entry_offset == 14

    def test_pre_biasing(self, tmp_path):
        """Relocated values are pre-biased by prefix_size."""
        # Raw binary: mov si, 0x0006 (bytes: BE 06 00 ... + padding)
        raw = tmp_path / "raw.bin"
        code = bytearray(b'\xBE\x06\x00\xC3' + b'\x00' * 4)
        raw.write_bytes(bytes(code))

        # One relocation at offset 1 (the 0x0006 immediate)
        rel = tmp_path / "test.rel"
        rel.write_bytes(struct.pack('<H', 1))

        out = tmp_path / "out.sys"
        run_tool([PACK_MODULE, raw, rel, '--magic', 'TEST', '-o', str(out)])

        data = out.read_bytes()
        # prefix_size = 12 + 2 = 14
        prefix_size = 14
        # The relocated value in the code should now be 0x0006 + 14 = 0x0014
        code_start = prefix_size
        biased_value = struct.unpack_from('<H', data, code_start + 1)[0]
        assert biased_value == 0x0006 + prefix_size, (
            f"Expected {0x0006 + prefix_size:#06x}, got {biased_value:#06x}"
        )

    def test_file_relative_reloc_entries(self, tmp_path):
        """Reloc entries in header are file-relative (offset + prefix_size)."""
        raw = tmp_path / "raw.bin"
        raw.write_bytes(b'\xBE\x06\x00\xC3' + b'\x00' * 4)

        rel = tmp_path / "test.rel"
        # Reloc at raw offset 1
        rel.write_bytes(struct.pack('<H', 1))

        out = tmp_path / "out.sys"
        run_tool([PACK_MODULE, raw, rel, '--magic', 'TEST', '-o', str(out)])

        data = out.read_bytes()
        # prefix = 14, so reloc entry should be 1 + 14 = 15
        reloc_entry = struct.unpack_from('<H', data, 12)[0]
        assert reloc_entry == 1 + 14

    def test_sector_padding(self, tmp_path):
        """Output is padded to sector boundary."""
        raw = tmp_path / "raw.bin"
        raw.write_bytes(b'\x90' * 100)  # 100 bytes of NOP

        rel = tmp_path / "test.rel"
        rel.write_bytes(b'')  # No relocs

        out = tmp_path / "out.sys"
        run_tool([PACK_MODULE, raw, rel, '--magic', 'NOOP',
                  '--pad-sectors', '-o', str(out)])

        size = out.stat().st_size
        assert size % 512 == 0, f"Output size {size} not sector-aligned"


# =============================================================================
# Kernel apply_relocs logic test (simulated — no Unicorn needed)
# =============================================================================

class TestApplyRelocsLogic:
    """Test the relocation patching algorithm in Python (mirrors kernel logic)."""

    def apply_relocs_sim(self, module_bytes: bytearray, load_base: int):
        """Simulate the kernel's apply_relocs subroutine.

        Reads v2 header, iterates reloc table, adds load_base to each word.
        """
        flags = struct.unpack_from('<H', module_bytes, 6)[0]
        if not (flags & 0x0001):
            return  # No relocs

        reloc_count = struct.unpack_from('<H', module_bytes, 8)[0]
        assert reloc_count > 0

        for i in range(reloc_count):
            reloc_off = struct.unpack_from('<H', module_bytes, 12 + i * 2)[0]
            value = struct.unpack_from('<H', module_bytes, reloc_off)[0]
            patched = (value + load_base) & 0xFFFF
            struct.pack_into('<H', module_bytes, reloc_off, patched)

    def test_patching_at_base_0x0800(self, tmp_path):
        """Module loaded at 0x0800 should produce correct absolute addresses."""
        raw = tmp_path / "raw.bin"
        # mov si, 0x0004 ; ret ; "hi",0  (label 'data' is at raw offset 4)
        code = bytearray(b'\xBE\x04\x00\xC3' + b'hi\x00\x00')
        raw.write_bytes(bytes(code))

        rel = tmp_path / "test.rel"
        rel.write_bytes(struct.pack('<H', 1))  # offset 1 in raw

        out = tmp_path / "out.sys"
        run_tool([PACK_MODULE, raw, rel, '--magic', 'TEST', '-o', str(out)])

        module = bytearray(out.read_bytes())
        load_base = 0x0800

        # Apply relocs (simulating kernel)
        self.apply_relocs_sim(module, load_base)

        # The value at the reloc position should now be:
        # original_raw_value (4) + prefix_size (14) + load_base (0x0800) = 0x0812
        prefix_size = 14
        reloc_file_offset = 1 + prefix_size  # = 15
        patched_value = struct.unpack_from('<H', module, reloc_file_offset)[0]
        expected = 0x0004 + prefix_size + load_base
        assert patched_value == expected, (
            f"Expected {expected:#06x}, got {patched_value:#06x}"
        )

    def test_patching_at_different_bases(self, tmp_path):
        """Same module at different bases should yield different addresses."""
        raw = tmp_path / "raw.bin"
        code = bytearray(b'\xBE\x04\x00\xC3' + b'hi\x00\x00')
        raw.write_bytes(bytes(code))

        rel = tmp_path / "test.rel"
        rel.write_bytes(struct.pack('<H', 1))

        out = tmp_path / "out.sys"
        run_tool([PACK_MODULE, raw, rel, '--magic', 'TEST', '-o', str(out)])

        prefix_size = 14
        reloc_file_offset = 1 + prefix_size

        for load_base in [0x0800, 0x1000, 0x2800, 0x0C00]:
            module = bytearray(out.read_bytes())
            self.apply_relocs_sim(module, load_base)
            patched = struct.unpack_from('<H', module, reloc_file_offset)[0]
            expected = 0x0004 + prefix_size + load_base
            assert patched == expected, (
                f"At base {load_base:#06x}: expected {expected:#06x}, "
                f"got {patched:#06x}"
            )


# =============================================================================
# Integration: verify actual built modules have valid v2 headers
# =============================================================================

class TestBuiltModules:
    """Verify that the actual build output has correct MNEX v2 structure."""

    @pytest.fixture(params=['fs.sys', 'mm.sys', 'shell.sys'])
    def module_path(self, request):
        path = BUILD_DIR / request.param
        if not path.exists():
            pytest.skip(f"{request.param} not built")
        return path

    def test_magic_and_header(self, module_path):
        """Built module has valid 4-byte magic and v2 header fields."""
        data = module_path.read_bytes()
        assert len(data) >= 12, "Module too small for v2 header"

        magic = data[0:4]
        assert magic in (b'MNFS', b'MNMM', b'MNEX'), (
            f"Invalid magic: {magic!r}"
        )

        sector_count = struct.unpack_from('<H', data, 4)[0]
        assert sector_count * 512 == len(data), (
            f"sector_count={sector_count} but file is {len(data)} bytes"
        )

        flags = struct.unpack_from('<H', data, 6)[0]
        assert flags & 0x0001, "MNEX_V2_FLAG_RELOC not set"

        reloc_count = struct.unpack_from('<H', data, 8)[0]
        assert reloc_count > 0, "No relocations (suspicious for a real module)"

        entry_offset = struct.unpack_from('<H', data, 10)[0]
        expected_entry = 12 + reloc_count * 2
        assert entry_offset == expected_entry, (
            f"entry_offset={entry_offset:#x}, expected {expected_entry:#x}"
        )

    def test_reloc_entries_within_bounds(self, module_path):
        """All relocation entries point within the module."""
        data = module_path.read_bytes()
        reloc_count = struct.unpack_from('<H', data, 8)[0]
        entry_offset = struct.unpack_from('<H', data, 10)[0]

        for i in range(reloc_count):
            reloc = struct.unpack_from('<H', data, 12 + i * 2)[0]
            assert reloc >= entry_offset, (
                f"Reloc {i} at {reloc:#x} points into header "
                f"(entry starts at {entry_offset:#x})"
            )
            assert reloc + 1 < len(data), (
                f"Reloc {i} at {reloc:#x} exceeds module size {len(data)}"
            )

    def test_modules_fit_in_module_area(self):
        """All three modules together fit below KERNEL_OFF."""
        from tests.harness.constants import MODULE_FIRST_BASE, MODULE_AREA_END

        total = 0
        for name in ['fs.sys', 'mm.sys', 'shell.sys']:
            path = BUILD_DIR / name
            if not path.exists():
                pytest.skip(f"{name} not built")
            total += path.stat().st_size

        end_addr = MODULE_FIRST_BASE + total
        assert end_addr <= MODULE_AREA_END, (
            f"Modules end at 0x{end_addr:04X}, exceeds limit 0x{MODULE_AREA_END:04X}"
        )


# =============================================================================
# Integration: verify built .MNX user programs have valid v2 headers
# =============================================================================

class TestBuiltPrograms:
    """Verify that built .MNX programs have correct MNEX v2 relocatable structure."""

    @pytest.fixture(params=['edit.mnx', 'mnmon.mnx', 'sysinfo.mnx'])
    def program_path(self, request):
        path = BUILD_DIR / request.param
        if not path.exists():
            pytest.skip(f"{request.param} not built")
        return path

    def test_magic_is_mnex(self, program_path):
        """All .MNX programs must have 'MNEX' magic."""
        data = program_path.read_bytes()
        assert data[0:4] == b'MNEX', f"Expected MNEX magic, got {data[0:4]!r}"

    def test_v2_header_valid(self, program_path):
        """Programs have valid v2 header with relocation flag set."""
        data = program_path.read_bytes()
        assert len(data) >= 12, "Program too small for v2 header"

        sector_count = struct.unpack_from('<H', data, 4)[0]
        assert sector_count * 512 == len(data), (
            f"sector_count={sector_count} but file is {len(data)} bytes"
        )

        flags = struct.unpack_from('<H', data, 6)[0]
        assert flags & 0x0001, "MNEX_V2_FLAG_RELOC not set"

        reloc_count = struct.unpack_from('<H', data, 8)[0]
        assert reloc_count > 0, "No relocations (suspicious for a real program)"

        entry_offset = struct.unpack_from('<H', data, 10)[0]
        expected_entry = 12 + reloc_count * 2
        assert entry_offset == expected_entry

    def test_reloc_entries_within_bounds(self, program_path):
        """All relocation entries point within the program."""
        data = program_path.read_bytes()
        reloc_count = struct.unpack_from('<H', data, 8)[0]
        entry_offset = struct.unpack_from('<H', data, 10)[0]

        for i in range(reloc_count):
            reloc = struct.unpack_from('<H', data, 12 + i * 2)[0]
            assert reloc >= entry_offset, (
                f"Reloc {i} at {reloc:#x} points into header "
                f"(entry starts at {entry_offset:#x})"
            )
            assert reloc + 1 < len(data), (
                f"Reloc {i} at {reloc:#x} exceeds program size {len(data)}"
            )

    def test_program_fits_in_tpa(self, program_path):
        """Program size does not exceed the TPA limit (30 KB)."""
        from tests.harness.constants import USER_PROG_MAX
        size = program_path.stat().st_size
        assert size <= USER_PROG_MAX, (
            f"{program_path.name} is {size} bytes, exceeds TPA max {USER_PROG_MAX}"
        )


# =============================================================================
# Shell relocation simulation: patching .MNX at TPA base
# =============================================================================

class TestShellRelocLogic:
    """Simulate the shell's program relocation (mirrors shell_cmd_run.inc)."""

    def shell_apply_relocs(self, module_bytes: bytearray, load_base: int):
        """Simulate shell's relocation patching for user programs.

        Same algorithm as kernel apply_relocs — add load_base to each
        word at the offsets listed in the relocation table.
        Includes secondary v2 validation: entry_offset == 12 + reloc_count*2.
        Returns the absolute entry point address.
        """
        flags = struct.unpack_from('<H', module_bytes, 6)[0]
        if not (flags & 0x0001):
            return load_base + 6  # Legacy v1: entry at offset 6

        # Secondary validation: entry_offset must equal 12 + reloc_count * 2
        reloc_count = struct.unpack_from('<H', module_bytes, 8)[0]
        entry_offset = struct.unpack_from('<H', module_bytes, 10)[0]
        expected_entry = 12 + reloc_count * 2
        if entry_offset != expected_entry:
            return load_base + 6  # Failed v2 validation, treat as v1

        for i in range(reloc_count):
            reloc_off = struct.unpack_from('<H', module_bytes, 12 + i * 2)[0]
            value = struct.unpack_from('<H', module_bytes, reloc_off)[0]
            patched = (value + load_base) & 0xFFFF
            struct.pack_into('<H', module_bytes, reloc_off, patched)

        return entry_offset + load_base  # Absolute entry point

    def test_program_patching_at_tpa(self, tmp_path):
        """Program loaded at USER_PROG_BASE gets correct absolute addresses."""
        from tests.harness.constants import USER_PROG_BASE

        raw = tmp_path / "raw.bin"
        # mov si, 0x0004 ; ret ; "hi",0
        code = bytearray(b'\xBE\x04\x00\xC3' + b'hi\x00\x00')
        raw.write_bytes(bytes(code))

        rel = tmp_path / "test.rel"
        rel.write_bytes(struct.pack('<H', 1))  # offset 1

        out = tmp_path / "out.mnx"
        run_tool([PACK_MODULE, raw, rel, '--magic', 'MNEX',
                  '--pad-sectors', '-o', str(out)])

        module = bytearray(out.read_bytes())
        entry = self.shell_apply_relocs(module, USER_PROG_BASE)

        # Entry should be at load_base + entry_offset
        prefix_size = 14  # 12 + 1*2
        assert entry == USER_PROG_BASE + prefix_size

        # The patched value (data ref) should be file_offset + load_base
        reloc_file_offset = 1 + prefix_size
        patched_value = struct.unpack_from('<H', module, reloc_file_offset)[0]
        expected = 0x0004 + prefix_size + USER_PROG_BASE
        assert patched_value == expected

    @pytest.mark.parametrize("program", ['edit.mnx', 'mnmon.mnx', 'sysinfo.mnx'])
    def test_real_program_relocation_no_overflow(self, program):
        """Applying relocs to real programs at TPA doesn't overflow 16 bits."""
        from tests.harness.constants import USER_PROG_BASE

        path = BUILD_DIR / program
        if not path.exists():
            pytest.skip(f"{program} not built")

        data = bytearray(path.read_bytes())
        flags = struct.unpack_from('<H', data, 6)[0]
        assert flags & 0x0001

        reloc_count = struct.unpack_from('<H', data, 8)[0]
        for i in range(reloc_count):
            reloc_off = struct.unpack_from('<H', data, 12 + i * 2)[0]
            value = struct.unpack_from('<H', data, reloc_off)[0]
            patched = value + USER_PROG_BASE
            assert patched <= 0xFFFF, (
                f"{program}: reloc {i} at offset {reloc_off:#x} overflows: "
                f"{value:#06x} + {USER_PROG_BASE:#06x} = {patched:#06x}"
            )

    def test_legacy_v1_fallback(self, tmp_path):
        """A v1 program (no reloc flag) returns entry at load_base + 6."""
        # Simulate v1: 'MNEX' + sector_count + code starting with NOP (0x90)
        # The word at offset 6 = 0xC390 — bit 0 clear, so detected as v1
        module = bytearray(b'MNEX' + struct.pack('<H', 1) + b'\x90\xC3' + b'\x00' * 504)
        entry = self.shell_apply_relocs(module, 0x8000)
        # v1 entry = load_base + 6 (past the 6-byte header)
        assert entry == 0x8000 + 6, f"Expected v1 entry 0x8006, got {entry:#x}"

    def test_v1_with_odd_first_opcode_not_falsely_v2(self, tmp_path):
        """A v1 program whose first byte has bit 0 set is NOT falsely v2.

        Before the secondary validation, 'jmp short' (0xEB) at offset 6 would
        set MNEX_V2_FLAG_RELOC bit and be falsely treated as v2.  The secondary
        check (entry_offset == 12 + reloc_count*2) catches this.
        """
        # 0xEB 0x04 = jmp short +4 (skips 4 bytes ahead)
        # word at offset 6 = 0x04EB — bit 0 set!
        # word at offset 8 (reloc_count) = random code bytes
        # word at offset 10 (entry_offset) = random code bytes
        # Secondary check will fail: entry_offset != 12 + reloc_count*2
        code = b'\xEB\x04\x90\x90\x90\xC3'  # jmp short +4, nops, ret
        module = bytearray(b'MNEX' + struct.pack('<H', 1) + code + b'\x00' * (512 - 10))
        entry = self.shell_apply_relocs(module, 0x8000)
        # Should fall back to v1 entry
        assert entry == 0x8000 + 6, f"Expected v1 fallback 0x8006, got {entry:#x}"

    def test_v1_push_bp_not_falsely_v2(self, tmp_path):
        """A v1 program starting with 'push bp' (0x55) is not falsely v2.

        0x55 has bit 0 set.  Without secondary validation, this would trigger
        v2 relocation on random code bytes.
        """
        # push bp (0x55), mov bp, sp (0x89 0xE5), ret (0xC3)
        code = b'\x55\x89\xE5\xC3'
        module = bytearray(b'MNEX' + struct.pack('<H', 1) + code + b'\x00' * (512 - 10))
        entry = self.shell_apply_relocs(module, 0x8000)
        assert entry == 0x8000 + 6, f"Expected v1 fallback 0x8006, got {entry:#x}"

    def test_v1_ret_opcode_not_falsely_v2(self, tmp_path):
        """A v1 program starting with 'ret' (0xC3) is not falsely v2.

        0xC3 has bit 0 set.  This is a degenerate but valid program.
        """
        code = b'\xC3\x00'
        module = bytearray(b'MNEX' + struct.pack('<H', 1) + code + b'\x00' * (512 - 8))
        entry = self.shell_apply_relocs(module, 0x8000)
        assert entry == 0x8000 + 6, f"Expected v1 fallback 0x8006, got {entry:#x}"

    def test_v2_secondary_validation_passes(self, tmp_path):
        """A genuine v2 binary passes both flag check AND secondary validation."""
        raw = tmp_path / "raw.bin"
        # Simple: mov si, label; ret; "AB"
        code = bytearray(b'\xBE\x04\x00\xC3' + b'AB')
        raw.write_bytes(bytes(code))

        rel = tmp_path / "test.rel"
        rel.write_bytes(struct.pack('<H', 1))  # 1 relocation at offset 1

        out = tmp_path / "out.mnx"
        run_tool([PACK_MODULE, raw, rel, '--magic', 'MNEX',
                  '--pad-sectors', '-o', str(out)])

        module = bytearray(out.read_bytes())
        # Verify header consistency
        flags = struct.unpack_from('<H', module, 6)[0]
        assert flags & 0x0001
        reloc_count = struct.unpack_from('<H', module, 8)[0]
        entry_offset = struct.unpack_from('<H', module, 10)[0]
        assert entry_offset == 12 + reloc_count * 2  # Secondary check passes

        entry = self.shell_apply_relocs(module, 0x8000)
        assert entry == 0x8000 + entry_offset  # Correct v2 entry
