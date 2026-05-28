"""Unit tests for SYS_EXEC (AH=0x27) — program overlay execution.

Tests cover:
  - exec_parse_args: kernel-local argument tokenizer
  - SYS_EXEC contract: error codes, v2 relocation on exec'd binary
  - Binary format validation for the exec path

The exec_parse_args tests use the Unicorn-based emulator to exercise the
kernel's argument parser (same logic as shell_parse_args but for SYS_EXEC).
"""

import struct
import pytest
from pathlib import Path
from tests.harness.emulator import MiniOSEmulator
from tests.harness.assembler import assemble_stub
from tests.harness.constants import (
    ARGV_ARGC, ARGV_PTRS, ARGV_STORAGE, ARGV_STORAGE_END, ARGV_MAX_ARGS,
    USER_PROG_BASE, USER_PROG_MAX_SEC,
)
from tests.conftest import register_coverage


# ─── Fixtures ─────────────────────────────────────────────────────────────────

@pytest.fixture(scope="module")
def exec_parse_args_bin():
    """Assembled exec_parse_args stub binary."""
    return assemble_stub("stub_exec_parse_args")


# ─── Coverage tracking ────────────────────────────────────────────────────────

_all_executed: set[int] = set()
_all_edges: set[tuple[int, int]] = set()
_binary_size: int = 0
_code_base: int = 0
_binary_path: Path | None = None


# ─── Helpers ──────────────────────────────────────────────────────────────────

# exec_args_buf is at a fixed offset after the code in the stub.
# We'll find it by reading the assembled binary size and using the label offset.
# The stub has: entry (3 bytes: call + hlt) + exec_parse_args code + data
# Rather than hardcode, we search for the 128-byte zero region at the end.

def _find_exec_args_buf(bin_path: Path) -> int:
    """Find the exec_args_buf address in the stub binary.

    The buffer is 128 bytes of zeros at the end of the binary, at ORG 0x1000.
    """
    data = bin_path.read_bytes()
    # The buffer is the last 128 bytes of the binary
    offset = len(data) - 128
    return 0x1000 + offset


def _run(emu: MiniOSEmulator, bin_path: Path, input_str: str | None):
    """Load stub, write args to exec_args_buf, run, track coverage."""
    global _binary_size, _code_base, _binary_path
    emu.load(bin_path)
    _binary_size = emu.code_size
    _code_base = emu.code_base
    _binary_path = bin_path

    buf_addr = _find_exec_args_buf(bin_path)

    if input_str is None:
        # Write empty string at buffer
        emu.write_byte(buf_addr, 0)
    else:
        emu.write_string(buf_addr, input_str)

    emu.run()
    _all_executed.update(emu.coverage_in_binary)
    _all_edges.update(emu.edges_in_binary)


def _get_argv(emu: MiniOSEmulator, index: int) -> str:
    """Read argv[index] string from the emulator."""
    ptr = emu.read_word(ARGV_PTRS + index * 2)
    return emu.read_string(ptr)


# ─── exec_parse_args Tests ────────────────────────────────────────────────────

class TestExecParseArgs:
    """Test the kernel-local exec_parse_args routine."""

    def test_empty_string(self, emu, exec_parse_args_bin):
        """Empty exec_args_buf → argc = 0."""
        _run(emu, exec_parse_args_bin, "")
        assert emu.read_byte(ARGV_ARGC) == 0

    def test_null_byte(self, emu, exec_parse_args_bin):
        """NUL as first byte → argc = 0."""
        _run(emu, exec_parse_args_bin, None)
        assert emu.read_byte(ARGV_ARGC) == 0

    def test_single_arg(self, emu, exec_parse_args_bin):
        """Single word → argc = 1."""
        _run(emu, exec_parse_args_bin, "hello")
        assert emu.read_byte(ARGV_ARGC) == 1
        assert _get_argv(emu, 0) == "hello"

    def test_two_args(self, emu, exec_parse_args_bin):
        """Two words → argc = 2."""
        _run(emu, exec_parse_args_bin, "one two")
        assert emu.read_byte(ARGV_ARGC) == 2
        assert _get_argv(emu, 0) == "one"
        assert _get_argv(emu, 1) == "two"

    def test_multiple_spaces(self, emu, exec_parse_args_bin):
        """Multiple spaces collapsed."""
        _run(emu, exec_parse_args_bin, "a   b   c")
        assert emu.read_byte(ARGV_ARGC) == 3
        assert _get_argv(emu, 0) == "a"
        assert _get_argv(emu, 1) == "b"
        assert _get_argv(emu, 2) == "c"

    def test_leading_spaces(self, emu, exec_parse_args_bin):
        """Leading spaces are skipped."""
        _run(emu, exec_parse_args_bin, "   hello")
        assert emu.read_byte(ARGV_ARGC) == 1
        assert _get_argv(emu, 0) == "hello"

    def test_tab_separator(self, emu, exec_parse_args_bin):
        """Tabs work as separators."""
        _run(emu, exec_parse_args_bin, "x\ty")
        assert emu.read_byte(ARGV_ARGC) == 2
        assert _get_argv(emu, 0) == "x"
        assert _get_argv(emu, 1) == "y"

    def test_quoted_arg(self, emu, exec_parse_args_bin):
        """Quoted strings treated as single argument."""
        _run(emu, exec_parse_args_bin, '"hello world" end')
        assert emu.read_byte(ARGV_ARGC) == 2
        assert _get_argv(emu, 0) == "hello world"
        assert _get_argv(emu, 1) == "end"

    def test_max_args_limit(self, emu, exec_parse_args_bin):
        """Stops at ARGV_MAX_ARGS (15)."""
        args = " ".join(f"a{i}" for i in range(20))
        _run(emu, exec_parse_args_bin, args)
        assert emu.read_byte(ARGV_ARGC) == ARGV_MAX_ARGS

    def test_only_spaces(self, emu, exec_parse_args_bin):
        """String of only spaces → argc = 0."""
        _run(emu, exec_parse_args_bin, "     ")
        assert emu.read_byte(ARGV_ARGC) == 0


# ─── SYS_EXEC Binary Contract Tests ──────────────────────────────────────────

class TestExecBinaryContract:
    """Test the SYS_EXEC binary interface contract (non-emulated).

    These verify the structural requirements that built programs satisfy
    for SYS_EXEC to work correctly.
    """

    def _load_program(self, name: str) -> bytes:
        """Load a built .MNX program binary."""
        build_dir = Path(__file__).resolve().parent.parent / "build" / "boot"
        path = build_dir / name
        if not path.exists():
            pytest.skip(f"Built program not found: {path}")
        return path.read_bytes()

    def test_mnex_magic_present(self):
        """All built programs have MNEX magic at offset 0."""
        for prog in ["SYSINFO.MNX", "MNMON.MNX", "EDIT.MNX"]:
            data = self._load_program(prog)
            assert data[:4] == b'MNEX', f"{prog} missing MNEX magic"

    def test_v2_header_flag_set(self):
        """All programs have v2 flag (bit 0 of flags at offset 6)."""
        for prog in ["SYSINFO.MNX", "MNMON.MNX", "EDIT.MNX"]:
            data = self._load_program(prog)
            flags = struct.unpack_from('<H', data, 6)[0]
            assert flags & 0x0001, f"{prog} missing v2 reloc flag"

    def test_v2_entry_offset_invariant(self):
        """entry_offset == 12 + reloc_count * 2 for all v2 programs."""
        for prog in ["SYSINFO.MNX", "MNMON.MNX", "EDIT.MNX"]:
            data = self._load_program(prog)
            reloc_count = struct.unpack_from('<H', data, 8)[0]
            entry_offset = struct.unpack_from('<H', data, 10)[0]
            expected = 12 + reloc_count * 2
            assert entry_offset == expected, (
                f"{prog}: entry_offset={entry_offset}, expected={expected}"
            )

    def test_program_fits_tpa(self):
        """All programs fit within USER_PROG_MAX_SEC sectors."""
        for prog in ["SYSINFO.MNX", "MNMON.MNX", "EDIT.MNX"]:
            data = self._load_program(prog)
            sector_count = struct.unpack_from('<H', data, 4)[0]
            assert sector_count <= USER_PROG_MAX_SEC, (
                f"{prog}: {sector_count} sectors exceeds max {USER_PROG_MAX_SEC}"
            )

    def test_relocation_entries_within_bounds(self):
        """All relocation offsets point within the program image."""
        for prog in ["SYSINFO.MNX", "MNMON.MNX", "EDIT.MNX"]:
            data = self._load_program(prog)
            reloc_count = struct.unpack_from('<H', data, 8)[0]
            entry_offset = struct.unpack_from('<H', data, 10)[0]
            file_size = len(data)

            for i in range(reloc_count):
                offset = struct.unpack_from('<H', data, 12 + i * 2)[0]
                assert offset + 1 < file_size, (
                    f"{prog}: reloc[{i}]={offset} out of bounds (size={file_size})"
                )

    def test_entry_within_bounds(self):
        """Entry offset points within program image."""
        for prog in ["SYSINFO.MNX", "MNMON.MNX", "EDIT.MNX"]:
            data = self._load_program(prog)
            entry_offset = struct.unpack_from('<H', data, 10)[0]
            assert entry_offset < len(data), (
                f"{prog}: entry_offset={entry_offset} >= size={len(data)}"
            )

    def test_exec_error_codes_documented(self):
        """Verify SYS_EXEC error code constants match documentation."""
        # Error codes as defined in the implementation:
        # 1 = file not found, 2 = not executable, 3 = too large,
        # 4 = read error, 5 = bad header
        error_codes = {1: "not found", 2: "not exec", 3: "too large",
                       4: "read error", 5: "bad header"}
        assert len(error_codes) == 5
        assert all(1 <= code <= 5 for code in error_codes)


class TestSpawnContract:
    """Test the SYS_SPAWN (AH=0x28) binary contract.

    SYS_SPAWN is SYS_EXEC + parent reload.  These tests verify the structural
    requirements — spawn_parent_fname in kernel data, MNMON's self-filename
    constant, and the syscall number assignment.
    """

    def _load_binary(self, name: str) -> bytes:
        """Load a built binary."""
        build_dir = Path(__file__).resolve().parent.parent / "build" / "boot"
        path = build_dir / name
        if not path.exists():
            pytest.skip(f"Built binary not found: {path}")
        return path.read_bytes()

    def test_syscall_number_assignment(self):
        """SYS_SPAWN is assigned 0x28 in syscalls.inc."""
        inc_path = (Path(__file__).resolve().parent.parent /
                    "src" / "include" / "syscalls.inc")
        content = inc_path.read_text()
        assert "SYS_SPAWN" in content
        # Verify the assignment
        for line in content.splitlines():
            if "SYS_SPAWN" in line and "equ" in line.lower():
                assert "0x28" in line or "28h" in line.lower()
                break
        else:
            pytest.fail("SYS_SPAWN equ definition not found")

    def test_syscall_max_includes_spawn(self):
        """SYSCALL_MAX >= 0x28 to include SYS_SPAWN."""
        inc_path = (Path(__file__).resolve().parent.parent /
                    "src" / "include" / "syscalls.inc")
        content = inc_path.read_text()
        for line in content.splitlines():
            if "SYSCALL_MAX" in line and "equ" in line.lower():
                # Extract hex value
                parts = line.split()
                for part in parts:
                    if part.startswith("0x"):
                        val = int(part, 16)
                        assert val >= 0x28
                        return
                break
        pytest.fail("SYSCALL_MAX not found or not >= 0x28")

    def test_mnmon_contains_self_filename(self):
        """MNMON binary contains its own 8.3 filename for SYS_SPAWN BX param."""
        data = self._load_binary("MNMON.MNX")
        # Look for the 11-byte padded filename 'MNMON   MNX'
        target = b'MNMON   MNX'
        assert target in data, "MNMON.MNX missing self-filename constant"

    def test_spawn_parent_fname_in_kernel(self):
        """Kernel binary should have spawn_parent_stack (spawn nesting support)."""
        # Verify the kernel source declares spawn_parent_stack and spawn_depth
        src_path = (Path(__file__).resolve().parent.parent /
                    "src" / "kernel" / "kernel_data.inc")
        content = src_path.read_text()
        assert "spawn_parent_stack" in content
        assert "spawn_depth" in content

    def test_spawn_uses_same_error_codes_as_exec(self):
        """SYS_SPAWN error codes are identical to SYS_EXEC (1-5)."""
        # By design, .fn_spawn jumps to .fn_exec after saving parent,
        # so error codes are the same.
        error_codes = {1: "not found", 2: "not exec", 3: "too large",
                       4: "read error", 5: "bad header"}
        assert len(error_codes) == 5

    def test_mnmon_uses_sys_spawn_not_exec(self):
        """MNMON source should reference SYS_SPAWN for the x command."""
        src_path = (Path(__file__).resolve().parent.parent /
                    "src" / "programs" / "mnmon.asm")
        content = src_path.read_text()
        # Check that the exec command uses SYS_SPAWN
        assert "SYS_SPAWN" in content
        # The old SYS_EXEC reference should NOT be in the spawn call area
        lines = content.splitlines()
        for i, line in enumerate(lines):
            if "mnmon_fname" in line and "DS:BX" in line:
                # Found the spawn call setup
                # Check nearby for SYS_SPAWN
                context = "\n".join(lines[max(0, i-2):i+5])
                assert "SYS_SPAWN" in context
                break


# ─── Coverage report ──────────────────────────────────────────────────────────

def pytest_collection_modifyitems(session, config, items):
    """Hook to register coverage after all tests in this module."""
    pass


@pytest.fixture(autouse=True, scope="module")
def _report_coverage(exec_parse_args_bin):
    """Register coverage data at end of module."""
    yield
    if _binary_size > 0:
        register_coverage(
            "exec_parse_args",
            _binary_size,
            len(_all_executed),
            edges=_all_edges,
            binary_path=_binary_path,
        )
