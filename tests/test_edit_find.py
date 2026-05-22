"""Unit tests for editor search/find routines and atoi.

Tests ed_search_text, ed_get_char_at_offset, and ed_atoi.
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

# Constants matching stub
GAP_BUF_START = 0x4000
GAP_BUF_END = 0x40FF
SEARCH_BUF_ADDR = 0x5000
STRING_ADDR = 0x5100  # For atoi test strings


def _find_entry_offsets(binary: bytes) -> dict[str, int]:
    """Find entry point offsets (CALL+HLT pattern)."""
    entries = ["search_text", "char_at_offset", "atoi"]
    offsets = {}
    pos = 0
    for name in entries:
        offsets[name] = CODE_BASE + pos
        while pos < len(binary) and binary[pos] != 0xF4:
            pos += 1
        pos += 1
    return offsets


def _find_data_addrs(binary: bytes) -> dict[str, int]:
    """Find data variables at end of binary."""
    import struct
    for i in range(len(binary) - 4):
        gs = struct.unpack_from('<H', binary, i)[0]
        ge = struct.unpack_from('<H', binary, i + 2)[0]
        if gs == GAP_BUF_START and ge == GAP_BUF_END + 1:
            return {
                "gap_start": CODE_BASE + i,
                "gap_end": CODE_BASE + i + 2,
                "search_len": CODE_BASE + i + 4,
            }
    raise RuntimeError("Could not find data area in binary")


class _FindHelper:
    """Helper for search/find tests."""

    def __init__(self, emu: MiniOSEmulator, binary_path: Path):
        self.emu = emu
        self.binary_path = binary_path
        self._binary = binary_path.read_bytes()
        self.entries = _find_entry_offsets(self._binary)
        self.data = _find_data_addrs(self._binary)

    def reset(self):
        self.emu.load(self.binary_path)
        self.emu.write_bytes(GAP_BUF_START, bytes(GAP_BUF_END - GAP_BUF_START + 1))

    def set_text(self, text: bytes):
        """Write text into gap buffer (all before gap)."""
        for i, b in enumerate(text):
            self.emu.write_byte(GAP_BUF_START + i, b)
        self.emu.write_word(self.data["gap_start"], GAP_BUF_START + len(text))
        self.emu.write_word(self.data["gap_end"], GAP_BUF_END + 1)

    def set_search(self, needle: bytes):
        """Set the search string."""
        self.emu.write_bytes(SEARCH_BUF_ADDR, needle + b'\x00')
        self.emu.write_byte(self.data["search_len"], len(needle))

    def run_entry(self, name: str):
        global _binary_size, _code_base, _binary_path
        _binary_size = self.emu.code_size
        _code_base = self.emu.code_base
        _binary_path = self.binary_path
        offset = self.entries[name] - CODE_BASE
        self.emu.run(entry_offset=offset)
        _all_executed.update(self.emu.coverage_in_binary)
        _all_edges.update(self.emu.edges_in_binary)


@pytest.fixture(scope="module")
def edit_find_bin():
    from tests.harness.assembler import assemble_stub
    return assemble_stub("stub_edit_find")


@pytest.fixture
def find(emu, edit_find_bin):
    h = _FindHelper(emu, edit_find_bin)
    h.reset()
    return h


class TestSearchText:
    """Tests for ed_search_text."""

    def test_find_at_start(self, find):
        find.set_text(b"Hello World")
        find.set_search(b"Hello")
        find.emu.set_reg("ax", 0)  # Start from offset 0
        find.run_entry("search_text")
        assert not find.emu.cf
        assert find.emu.reg("ax") == 0

    def test_find_in_middle(self, find):
        find.set_text(b"Hello World")
        find.set_search(b"World")
        find.emu.set_reg("ax", 0)
        find.run_entry("search_text")
        assert not find.emu.cf
        assert find.emu.reg("ax") == 6

    def test_find_with_offset(self, find):
        find.set_text(b"abcabc")
        find.set_search(b"abc")
        find.emu.set_reg("ax", 1)  # Skip first match
        find.run_entry("search_text")
        assert not find.emu.cf
        assert find.emu.reg("ax") == 3

    def test_not_found(self, find):
        find.set_text(b"Hello World")
        find.set_search(b"xyz")
        find.emu.set_reg("ax", 0)
        find.run_entry("search_text")
        assert find.emu.cf

    def test_empty_text(self, find):
        # Text length = 0
        find.set_search(b"a")
        find.emu.set_reg("ax", 0)
        find.run_entry("search_text")
        assert find.emu.cf

    def test_single_char_match(self, find):
        find.set_text(b"abcdef")
        find.set_search(b"d")
        find.emu.set_reg("ax", 0)
        find.run_entry("search_text")
        assert not find.emu.cf
        assert find.emu.reg("ax") == 3

    def test_find_at_end(self, find):
        find.set_text(b"test!")
        find.set_search(b"!")
        find.emu.set_reg("ax", 0)
        find.run_entry("search_text")
        assert not find.emu.cf
        assert find.emu.reg("ax") == 4


class TestGetCharAtOffset:
    """Tests for ed_get_char_at_offset."""

    def test_first_char(self, find):
        find.set_text(b"ABCDE")
        find.emu.set_reg("ax", 0)
        find.run_entry("char_at_offset")
        assert find.emu.reg("al") == ord('A')

    def test_middle_char(self, find):
        find.set_text(b"ABCDE")
        find.emu.set_reg("ax", 2)
        find.run_entry("char_at_offset")
        assert find.emu.reg("al") == ord('C')

    def test_last_char(self, find):
        find.set_text(b"ABCDE")
        find.emu.set_reg("ax", 4)
        find.run_entry("char_at_offset")
        assert find.emu.reg("al") == ord('E')

    def test_char_after_gap(self, find):
        """Test reading char that's after the gap."""
        # Put "AB" before gap, "CD" after gap
        self = find
        self.emu.write_byte(GAP_BUF_START, ord('A'))
        self.emu.write_byte(GAP_BUF_START + 1, ord('B'))
        self.emu.write_word(self.data["gap_start"], GAP_BUF_START + 2)
        # Put "CD" at end of buffer
        self.emu.write_byte(GAP_BUF_END - 1, ord('C'))
        self.emu.write_byte(GAP_BUF_END, ord('D'))
        self.emu.write_word(self.data["gap_end"], GAP_BUF_END - 1)
        # Offset 2 should be 'C' (first char after gap)
        self.emu.set_reg("ax", 2)
        self.run_entry("char_at_offset")
        assert self.emu.reg("al") == ord('C')


class TestAtoi:
    """Tests for ed_atoi."""

    def test_simple_number(self, find):
        find.emu.write_bytes(STRING_ADDR, b"42\x00")
        find.emu.set_reg("si", STRING_ADDR)
        find.run_entry("atoi")
        assert find.emu.reg("ax") == 42

    def test_zero(self, find):
        find.emu.write_bytes(STRING_ADDR, b"0\x00")
        find.emu.set_reg("si", STRING_ADDR)
        find.run_entry("atoi")
        assert find.emu.reg("ax") == 0

    def test_large_number(self, find):
        find.emu.write_bytes(STRING_ADDR, b"65535\x00")
        find.emu.set_reg("si", STRING_ADDR)
        find.run_entry("atoi")
        assert find.emu.reg("ax") == 65535

    def test_single_digit(self, find):
        find.emu.write_bytes(STRING_ADDR, b"7\x00")
        find.emu.set_reg("si", STRING_ADDR)
        find.run_entry("atoi")
        assert find.emu.reg("ax") == 7

    def test_stops_at_non_digit(self, find):
        find.emu.write_bytes(STRING_ADDR, b"123abc\x00")
        find.emu.set_reg("si", STRING_ADDR)
        find.run_entry("atoi")
        assert find.emu.reg("ax") == 123

    def test_empty_string(self, find):
        find.emu.write_bytes(STRING_ADDR, b"\x00")
        find.emu.set_reg("si", STRING_ADDR)
        find.run_entry("atoi")
        assert find.emu.reg("ax") == 0

    def test_leading_non_digit(self, find):
        find.emu.write_bytes(STRING_ADDR, b"abc\x00")
        find.emu.set_reg("si", STRING_ADDR)
        find.run_entry("atoi")
        assert find.emu.reg("ax") == 0

    def test_hundred(self, find):
        find.emu.write_bytes(STRING_ADDR, b"100\x00")
        find.emu.set_reg("si", STRING_ADDR)
        find.run_entry("atoi")
        assert find.emu.reg("ax") == 100


# ─── Coverage ─────────────────────────────────────────────────────────────────

@pytest.fixture(autouse=True, scope="module")
def _report_coverage(edit_find_bin):
    yield
    if _binary_size > 0:
        register_coverage(
            "edit_find",
            _binary_size,
            len(_all_executed),
            edges=_all_edges,
            binary_path=_binary_path,
        )
