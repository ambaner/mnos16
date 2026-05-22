"""Unit tests for editor filename parser (ed_fp_parse_typed_name).

Tests the 8.3 filename conversion that takes user input like "hello.txt"
and produces the space-padded MNFS format "HELLO   TXT".
"""

import pytest
from pathlib import Path
from tests.harness.emulator import MiniOSEmulator
from tests.harness.constants import CODE_BASE
from tests.conftest import register_coverage

_all_executed: set[int] = set()
_all_edges: set[tuple[int, int]] = set()
_binary_size: int = 0
_code_base: int = 0
_binary_path: Path | None = None

MNFS_NAME_LEN = 11


def _find_data_addrs(binary: bytes) -> dict[str, int]:
    """Find data addresses by looking for the known layout at end of binary."""
    # _fp_input_buf is 16 bytes, then filename is 12 bytes, then filename_len is 1 byte
    # Total suffix = 16 + 12 + 1 = 29 bytes
    # The input buf starts as all zeros, filename as all zeros, filename_len = 0
    # We look for the first big zero block near the end
    # Simpler: just use known offsets from end of binary
    # input_buf at -29, filename at -13, filename_len at -1
    size = len(binary)
    return {
        "input_buf": CODE_BASE + size - 29,
        "filename": CODE_BASE + size - 13,
        "filename_len": CODE_BASE + size - 1,
    }


def _run_parse(emu: MiniOSEmulator, binary_path: Path, input_name: str) -> str:
    """Run the parser with given input and return the 11-char result."""
    global _binary_size, _code_base, _binary_path
    binary = binary_path.read_bytes()
    data = _find_data_addrs(binary)

    emu.load(binary_path)
    _binary_size = emu.code_size
    _code_base = emu.code_base
    _binary_path = binary_path

    # Write input name to _fp_input_buf
    input_bytes = input_name.encode('ascii') + b'\x00'
    emu.write_bytes(data["input_buf"], input_bytes)

    emu.run(entry_offset=0)
    _all_executed.update(emu.coverage_in_binary)
    _all_edges.update(emu.edges_in_binary)

    # Read result from filename (11 bytes)
    result = emu.read_bytes(data["filename"], 11)
    return result.decode('ascii')


@pytest.fixture(scope="module")
def edit_fname_bin():
    from tests.harness.assembler import assemble_stub
    return assemble_stub("stub_edit_fname")


class TestParseTypedName:
    """Tests for ed_fp_parse_typed_name."""

    def test_simple_name_with_ext(self, emu, edit_fname_bin):
        result = _run_parse(emu, edit_fname_bin, "hello.txt")
        assert result == "HELLO   TXT"

    def test_short_name_no_ext(self, emu, edit_fname_bin):
        result = _run_parse(emu, edit_fname_bin, "readme")
        assert result == "README     "

    def test_8_char_name_3_ext(self, emu, edit_fname_bin):
        result = _run_parse(emu, edit_fname_bin, "12345678.abc")
        assert result == "12345678ABC"

    def test_uppercase_passthrough(self, emu, edit_fname_bin):
        result = _run_parse(emu, edit_fname_bin, "FILE.ASM")
        assert result == "FILE    ASM"

    def test_mixed_case(self, emu, edit_fname_bin):
        result = _run_parse(emu, edit_fname_bin, "Test.Txt")
        assert result == "TEST    TXT"

    def test_long_name_truncated(self, emu, edit_fname_bin):
        result = _run_parse(emu, edit_fname_bin, "longfilename.doc")
        # Only first 8 chars kept: "LONGFILE", ext "DOC"
        assert result == "LONGFILEDOC"

    def test_short_ext(self, emu, edit_fname_bin):
        result = _run_parse(emu, edit_fname_bin, "a.b")
        assert result == "A       B  "

    def test_no_dot_all_name(self, emu, edit_fname_bin):
        result = _run_parse(emu, edit_fname_bin, "autoexec")
        assert result == "AUTOEXEC   "

    def test_dot_only_ext(self, emu, edit_fname_bin):
        result = _run_parse(emu, edit_fname_bin, ".txt")
        # Empty name, extension TXT
        assert result == "        TXT"

    def test_single_char(self, emu, edit_fname_bin):
        result = _run_parse(emu, edit_fname_bin, "x")
        assert result == "X          "

    def test_numbers_in_name(self, emu, edit_fname_bin):
        result = _run_parse(emu, edit_fname_bin, "file1.mn2")
        assert result == "FILE1   MN2"

    def test_max_name_max_ext(self, emu, edit_fname_bin):
        result = _run_parse(emu, edit_fname_bin, "abcdefgh.xyz")
        assert result == "ABCDEFGHXYZ"


# ─── Coverage ─────────────────────────────────────────────────────────────────

@pytest.fixture(autouse=True, scope="module")
def _report_coverage(edit_fname_bin):
    yield
    if _binary_size > 0:
        register_coverage(
            "edit_fname",
            _binary_size,
            len(_all_executed),
            edges=_all_edges,
            binary_path=_binary_path,
        )
