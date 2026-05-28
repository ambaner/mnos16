"""Unit tests for editor gap buffer operations.

Tests the core gap buffer routines: insert, delete (back/fwd), move_to,
text_length, cursor_offset, char_at_si, and get_line_offset.
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

# Gap buffer constants (must match stub)
GAP_BUF_START = 0x4000
GAP_BUF_END = 0x40FF
GAP_BUF_SIZE = GAP_BUF_END - GAP_BUF_START + 1  # 256 bytes


def _find_entry_offsets(binary: bytes) -> dict[str, int]:
    """Find entry point offsets by scanning for CALL+HLT patterns.
    
    Returns dict mapping entry name to absolute address (org-based).
    We rely on the known order from the stub file.
    """
    # Entry points are sequential CALL+HLT blocks at the start
    # Each is: CALL rel16 (3 bytes = E8 xx xx) + HLT (1 byte = F4) = 4 bytes
    # (Or CALL near could be 3 bytes for E8 + 2-byte offset)
    entries = [
        "insert", "delete_back", "delete_fwd", "move_to",
        "text_length", "cursor_offset", "char_at_si", "line_offset"
    ]
    offsets = {}
    pos = 0
    for name in entries:
        offsets[name] = CODE_BASE + pos
        # Find next HLT (0xF4)
        while pos < len(binary) and binary[pos] != 0xF4:
            pos += 1
        pos += 1  # skip the HLT
    return offsets


def _get_data_addrs(binary: bytes) -> dict[str, int]:
    """Find data variable addresses.
    
    The stub puts gap_start, gap_end, modified, total_lines at the end.
    We find them by searching for the initial values.
    """
    # Search for the pattern: 00 40 (gap_start=0x4000) 00 41 (gap_end=0x4100)
    # which is the initialized state
    import struct
    for i in range(len(binary) - 6):
        gs = struct.unpack_from('<H', binary, i)[0]
        ge = struct.unpack_from('<H', binary, i + 2)[0]
        if gs == GAP_BUF_START and ge == GAP_BUF_END + 1:
            return {
                "gap_start": CODE_BASE + i,
                "gap_end": CODE_BASE + i + 2,
                "modified": CODE_BASE + i + 4,
                "total_lines": CODE_BASE + i + 5,
            }
    raise RuntimeError("Could not find data area in binary")


class _GapHelper:
    """Helper to manage gap buffer state in the emulator."""

    def __init__(self, emu: MiniOSEmulator, binary_path: Path):
        self.emu = emu
        self.binary_path = binary_path
        self._binary = binary_path.read_bytes()
        self.entries = _find_entry_offsets(self._binary)
        self.data = _get_data_addrs(self._binary)

    def reset(self):
        """Load fresh binary and reset gap buffer to empty state."""
        self.emu.load(self.binary_path)
        # Ensure gap buffer memory region is zeroed
        self.emu.write_bytes(GAP_BUF_START, bytes(GAP_BUF_SIZE))

    def set_gap(self, gap_start: int, gap_end: int):
        """Set gap_start and gap_end variables."""
        self.emu.write_word(self.data["gap_start"], gap_start)
        self.emu.write_word(self.data["gap_end"], gap_end)

    def get_gap_start(self) -> int:
        return self.emu.read_word(self.data["gap_start"])

    def get_gap_end(self) -> int:
        return self.emu.read_word(self.data["gap_end"])

    def get_modified(self) -> int:
        return self.emu.read_byte(self.data["modified"])

    def get_total_lines(self) -> int:
        return self.emu.read_word(self.data["total_lines"])

    def set_total_lines(self, n: int):
        self.emu.write_word(self.data["total_lines"], n)

    def set_modified(self, v: int):
        self.emu.write_byte(self.data["modified"], v)

    def run_entry(self, name: str):
        """Run from the named entry point."""
        global _binary_size, _code_base, _binary_path
        _binary_size = self.emu.code_size
        _code_base = self.emu.code_base
        _binary_path = self.binary_path
        offset = self.entries[name] - CODE_BASE
        self.emu.run(entry_offset=offset)
        _all_executed.update(self.emu.coverage_in_binary)
        _all_edges.update(self.emu.edges_in_binary)

    def write_text_before_gap(self, text: bytes):
        """Write text before the gap (simulates existing text before cursor)."""
        for i, b in enumerate(text):
            self.emu.write_byte(GAP_BUF_START + i, b)
        self.set_gap(GAP_BUF_START + len(text), self.get_gap_end())

    def write_text_after_gap(self, text: bytes):
        """Write text after the gap (simulates text after cursor)."""
        end = GAP_BUF_END + 1
        start = end - len(text)
        for i, b in enumerate(text):
            self.emu.write_byte(start + i, b)
        self.set_gap(self.get_gap_start(), start)

    def read_full_text(self) -> bytes:
        """Read all text in the buffer (before gap + after gap)."""
        gs = self.get_gap_start()
        ge = self.get_gap_end()
        before = self.emu.read_bytes(GAP_BUF_START, gs - GAP_BUF_START)
        after = self.emu.read_bytes(ge, GAP_BUF_END + 1 - ge)
        return before + after


@pytest.fixture(scope="module")
def edit_gap_bin():
    """Assembled edit gap buffer stub."""
    from tests.harness.assembler import assemble_stub
    return assemble_stub("stub_edit_gap")


@pytest.fixture
def gap(emu, edit_gap_bin):
    """Fresh gap buffer helper."""
    h = _GapHelper(emu, edit_gap_bin)
    h.reset()
    return h


class TestGapInsert:
    """Tests for ed_gap_insert."""

    def test_insert_single_char(self, gap):
        gap.emu.set_reg("ax", ord('A'))
        gap.run_entry("insert")
        assert gap.get_gap_start() == GAP_BUF_START + 1
        assert gap.emu.read_byte(GAP_BUF_START) == ord('A')
        assert gap.get_modified() == 1

    def test_insert_multiple_chars(self, gap):
        # Test inserting "Hi" char by char
        gap.reset()
        gap.emu.set_reg("ax", ord('H'))
        gap.run_entry("insert")
        # Re-load for second call (emulator state may be stale after hlt)
        # Instead, just set up and run again from same entry
        gap.emu.set_reg("ax", ord('i'))
        offset = gap.entries["insert"] - CODE_BASE
        gap.emu.run(entry_offset=offset)
        _all_executed.update(gap.emu.coverage_in_binary)
        assert gap.get_gap_start() == GAP_BUF_START + 2
        assert gap.emu.read_byte(GAP_BUF_START) == ord('H')
        assert gap.emu.read_byte(GAP_BUF_START + 1) == ord('i')

    def test_insert_newline_increments_total_lines(self, gap):
        gap.set_total_lines(1)
        gap.emu.set_reg("ax", 0x0A)  # newline
        gap.run_entry("insert")
        assert gap.get_total_lines() == 2

    def test_insert_non_newline_no_line_change(self, gap):
        gap.set_total_lines(1)
        gap.emu.set_reg("ax", ord('x'))
        gap.run_entry("insert")
        assert gap.get_total_lines() == 1

    def test_insert_when_full_does_nothing(self, gap):
        # Set gap_start == gap_end (buffer full)
        gap.set_gap(GAP_BUF_START + 100, GAP_BUF_START + 100)
        gap.set_modified(0)
        gap.emu.set_reg("ax", ord('X'))
        gap.run_entry("insert")
        # gap_start unchanged
        assert gap.get_gap_start() == GAP_BUF_START + 100
        assert gap.get_modified() == 0


class TestGapDeleteBack:
    """Tests for ed_gap_delete_back."""

    def test_delete_back_normal(self, gap):
        # Put 'A' before the gap
        gap.write_text_before_gap(b"A")
        gap.set_total_lines(1)
        gap.run_entry("delete_back")
        assert gap.get_gap_start() == GAP_BUF_START
        assert gap.get_modified() == 1
        assert not gap.emu.cf  # CF clear = success

    def test_delete_back_at_start_fails(self, gap):
        # gap_start == GAP_BUF_START (nothing before)
        gap.run_entry("delete_back")
        assert gap.emu.cf  # CF set = failure
        assert gap.get_gap_start() == GAP_BUF_START

    def test_delete_back_newline_decrements_lines(self, gap):
        gap.write_text_before_gap(b"\n")
        gap.set_total_lines(2)
        gap.run_entry("delete_back")
        assert gap.get_total_lines() == 1

    def test_delete_back_non_newline_no_line_change(self, gap):
        gap.write_text_before_gap(b"x")
        gap.set_total_lines(3)
        gap.run_entry("delete_back")
        assert gap.get_total_lines() == 3


class TestGapDeleteFwd:
    """Tests for ed_gap_delete_fwd."""

    def test_delete_fwd_normal(self, gap):
        # Put 'B' after the gap
        gap.write_text_after_gap(b"B")
        gap.set_total_lines(1)
        gap.run_entry("delete_fwd")
        assert gap.get_gap_end() == GAP_BUF_END + 1
        assert gap.get_modified() == 1
        assert not gap.emu.cf

    def test_delete_fwd_at_end_fails(self, gap):
        # gap_end == GAP_BUF_END + 1 (nothing after)
        gap.run_entry("delete_fwd")
        assert gap.emu.cf  # CF set = failure

    def test_delete_fwd_newline_decrements_lines(self, gap):
        gap.write_text_after_gap(b"\n")
        gap.set_total_lines(3)
        gap.run_entry("delete_fwd")
        assert gap.get_total_lines() == 2


class TestGapMoveTo:
    """Tests for ed_gap_move_to."""

    def test_move_to_same_position(self, gap):
        # Gap at position 0, move to 0 — no-op
        gap.run_entry("move_to")
        assert gap.get_gap_start() == GAP_BUF_START
        assert gap.get_gap_end() == GAP_BUF_END + 1

    def test_move_right(self, gap):
        # Put "Hello" after the gap, gap at position 0
        gap.write_text_after_gap(b"Hello")
        # Move gap to position 3 (between 'l' and 'l')
        gap.emu.set_reg("ax", 3)
        gap.run_entry("move_to")
        # Now gap_start should be at GAP_BUF_START + 3
        assert gap.get_gap_start() == GAP_BUF_START + 3
        # Text should still read "Hello"
        text = gap.read_full_text()
        assert text == b"Hello"

    def test_move_left(self, gap):
        # Put "Hello" before gap (gap at position 5)
        gap.write_text_before_gap(b"Hello")
        # Move gap to position 2
        gap.emu.set_reg("ax", 2)
        gap.run_entry("move_to")
        assert gap.get_gap_start() == GAP_BUF_START + 2
        # Text should still read "Hello"
        text = gap.read_full_text()
        assert text == b"Hello"

    def test_move_preserves_ax(self, gap):
        # ed_gap_move_to pushes and pops AX
        gap.write_text_after_gap(b"ABCD")
        gap.emu.set_reg("ax", 2)
        gap.run_entry("move_to")
        assert gap.emu.reg("ax") == 2


class TestGetTextLength:
    """Tests for ed_get_text_length."""

    def test_empty_buffer(self, gap):
        gap.run_entry("text_length")
        assert gap.emu.reg("ax") == 0

    def test_text_before_gap(self, gap):
        gap.write_text_before_gap(b"Hello")
        gap.run_entry("text_length")
        assert gap.emu.reg("ax") == 5

    def test_text_after_gap(self, gap):
        gap.write_text_after_gap(b"World")
        gap.run_entry("text_length")
        assert gap.emu.reg("ax") == 5

    def test_text_both_sides(self, gap):
        gap.write_text_before_gap(b"He")
        gap.write_text_after_gap(b"llo")
        gap.run_entry("text_length")
        assert gap.emu.reg("ax") == 5


class TestGetCursorOffset:
    """Tests for ed_get_cursor_offset."""

    def test_at_start(self, gap):
        gap.run_entry("cursor_offset")
        assert gap.emu.reg("ax") == 0

    def test_after_inserts(self, gap):
        gap.write_text_before_gap(b"ABC")
        gap.run_entry("cursor_offset")
        assert gap.emu.reg("ax") == 3


class TestCharAtSi:
    """Tests for ed_gap_char_at_si."""

    def test_read_before_gap(self, gap):
        gap.write_text_before_gap(b"Hi")
        gap.write_text_after_gap(b"!")
        # Read from start
        gap.emu.set_reg("si", GAP_BUF_START)
        gap.run_entry("char_at_si")
        assert gap.emu.reg("al") == ord('H')

    def test_read_skips_gap(self, gap):
        # "AB" before gap, "CD" after gap
        gap.write_text_before_gap(b"AB")
        gap.write_text_after_gap(b"CD")
        # Set SI to gap_start — should skip to gap_end and read 'C'
        gap.emu.set_reg("si", gap.get_gap_start())
        gap.run_entry("char_at_si")
        assert gap.emu.reg("al") == ord('C')

    def test_read_past_end_returns_zero(self, gap):
        gap.emu.set_reg("si", GAP_BUF_END + 1)
        gap.run_entry("char_at_si")
        assert gap.emu.reg("al") == 0


class TestGetLineOffset:
    """Tests for ed_get_line_offset."""

    def test_line_zero(self, gap):
        gap.write_text_before_gap(b"Hello\nWorld")
        gap.emu.set_reg("ax", 0)
        gap.run_entry("line_offset")
        assert gap.emu.reg("si") == GAP_BUF_START

    def test_line_one(self, gap):
        # "Hello\n" before gap, "World" after gap
        gap.write_text_before_gap(b"Hello\n")
        gap.write_text_after_gap(b"World")
        gap.emu.set_reg("ax", 1)
        gap.run_entry("line_offset")
        # Line 1 starts after the \n at position 6
        # Since "Hello\n" is before gap, SI should be at GAP_BUF_START + 6
        # But that's inside the gap, so char_at_si would skip to gap_end
        # The line_offset routine uses char_at_si which handles this
        # SI should end up pointing at gap_end (start of "World")
        assert gap.emu.reg("si") == gap.get_gap_end()


# ─── Coverage registration ────────────────────────────────────────────────────

def pytest_collection_finish(session):
    """Register coverage after all tests in this module."""
    pass


@pytest.fixture(autouse=True, scope="module")
def _report_coverage(edit_gap_bin):
    """Report coverage at end of module."""
    yield
    if _binary_size > 0:
        register_coverage(
            "edit_gap",
            _binary_size,
            len(_all_executed),
            edges=_all_edges,
            binary_path=_binary_path,
        )
