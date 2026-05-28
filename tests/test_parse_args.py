"""Unit tests for shell_parse_args — argument tokenizer (Layer 2).

Tests the routine that parses a raw argument string into the structured
argc/argv table at 0x7F00.  See doc/COMMAND-LINE.md §4 for the spec.
"""

import pytest
from pathlib import Path
from tests.harness.emulator import MiniOSEmulator
from tests.harness.constants import (
    SHELL_ARGS_PTR, ARGV_ARGC, ARGV_PTRS, ARGV_MAX_ARGS, STRING_AREA,
)
from tests.conftest import register_coverage


# Accumulate all executed addresses across tests for coverage
_all_executed: set[int] = set()
_all_edges: set[tuple[int, int]] = set()
_binary_size: int = 0
_code_base: int = 0
_binary_path: Path | None = None


def _run(emu: MiniOSEmulator, parse_args_bin, input_str: str | None):
    """Helper: set up memory, run shell_parse_args, track coverage."""
    global _binary_size, _code_base, _binary_path
    emu.load(parse_args_bin)
    _binary_size = emu.code_size
    _code_base = emu.code_base
    _binary_path = parse_args_bin

    if input_str is None:
        # NULL pointer case
        emu.write_word(SHELL_ARGS_PTR, 0)
    else:
        emu.write_string(STRING_AREA, input_str)
        emu.write_word(SHELL_ARGS_PTR, STRING_AREA)

    emu.run()
    _all_executed.update(emu.coverage_in_binary)
    _all_edges.update(emu.edges_in_binary)


def _get_argv(emu: MiniOSEmulator, index: int) -> str:
    """Read argv[index] string from the emulator."""
    ptr = emu.read_word(ARGV_PTRS + index * 2)
    return emu.read_string(ptr)


# ─── Tests ────────────────────────────────────────────────────────────────────

class TestParseArgsBasic:
    """Basic argument parsing scenarios."""

    def test_null_pointer(self, emu, parse_args_bin):
        """SHELL_ARGS_PTR = 0 → argc = 0."""
        _run(emu, parse_args_bin, None)
        assert emu.read_byte(ARGV_ARGC) == 0

    def test_empty_string(self, emu, parse_args_bin):
        """Empty string → argc = 0."""
        _run(emu, parse_args_bin, "")
        assert emu.read_byte(ARGV_ARGC) == 0

    def test_single_arg(self, emu, parse_args_bin):
        """Single word → argc = 1."""
        _run(emu, parse_args_bin, "hello")
        assert emu.read_byte(ARGV_ARGC) == 1
        assert _get_argv(emu, 0) == "hello"

    def test_two_args(self, emu, parse_args_bin):
        """Two words → argc = 2."""
        _run(emu, parse_args_bin, "hello world")
        assert emu.read_byte(ARGV_ARGC) == 2
        assert _get_argv(emu, 0) == "hello"
        assert _get_argv(emu, 1) == "world"

    def test_three_args(self, emu, parse_args_bin):
        """Three words → argc = 3."""
        _run(emu, parse_args_bin, "one two three")
        assert emu.read_byte(ARGV_ARGC) == 3
        assert _get_argv(emu, 0) == "one"
        assert _get_argv(emu, 1) == "two"
        assert _get_argv(emu, 2) == "three"


class TestParseArgsWhitespace:
    """Whitespace handling edge cases."""

    def test_multiple_spaces(self, emu, parse_args_bin):
        """Multiple spaces between args are collapsed."""
        _run(emu, parse_args_bin, "a   b   c")
        assert emu.read_byte(ARGV_ARGC) == 3
        assert _get_argv(emu, 0) == "a"
        assert _get_argv(emu, 1) == "b"
        assert _get_argv(emu, 2) == "c"

    def test_leading_spaces(self, emu, parse_args_bin):
        """Leading spaces are skipped."""
        _run(emu, parse_args_bin, "   hello")
        assert emu.read_byte(ARGV_ARGC) == 1
        assert _get_argv(emu, 0) == "hello"

    def test_trailing_spaces(self, emu, parse_args_bin):
        """Trailing spaces don't create extra args."""
        _run(emu, parse_args_bin, "hello   ")
        assert emu.read_byte(ARGV_ARGC) == 1
        assert _get_argv(emu, 0) == "hello"

    def test_tab_separator(self, emu, parse_args_bin):
        """Tab characters are treated as separators."""
        _run(emu, parse_args_bin, "a\tb")
        assert emu.read_byte(ARGV_ARGC) == 2
        assert _get_argv(emu, 0) == "a"
        assert _get_argv(emu, 1) == "b"

    def test_only_spaces(self, emu, parse_args_bin):
        """String of only spaces → argc = 0."""
        _run(emu, parse_args_bin, "     ")
        assert emu.read_byte(ARGV_ARGC) == 0


class TestParseArgsQuotes:
    """Double-quote handling."""

    def test_quoted_string(self, emu, parse_args_bin):
        """Quoted string is treated as one argument."""
        _run(emu, parse_args_bin, '"hello world" foo')
        assert emu.read_byte(ARGV_ARGC) == 2
        assert _get_argv(emu, 0) == "hello world"
        assert _get_argv(emu, 1) == "foo"

    def test_quoted_at_end(self, emu, parse_args_bin):
        """Quoted argument at end of string."""
        _run(emu, parse_args_bin, 'foo "bar baz"')
        assert emu.read_byte(ARGV_ARGC) == 2
        assert _get_argv(emu, 0) == "foo"
        assert _get_argv(emu, 1) == "bar baz"

    def test_unterminated_quote(self, emu, parse_args_bin):
        """Unterminated quote — content still captured."""
        _run(emu, parse_args_bin, '"hello world')
        assert emu.read_byte(ARGV_ARGC) == 1
        assert _get_argv(emu, 0) == "hello world"

    def test_empty_quotes(self, emu, parse_args_bin):
        """Empty quotes produce an empty argument."""
        _run(emu, parse_args_bin, '"" foo')
        assert emu.read_byte(ARGV_ARGC) == 2
        assert _get_argv(emu, 0) == ""
        assert _get_argv(emu, 1) == "foo"


class TestParseArgsLimits:
    """Boundary and overflow conditions."""

    def test_max_args_15(self, emu, parse_args_bin):
        """Exactly 15 args → argc = 15."""
        args = " ".join(str(i) for i in range(1, 16))
        _run(emu, parse_args_bin, args)
        assert emu.read_byte(ARGV_ARGC) == 15
        assert _get_argv(emu, 0) == "1"
        assert _get_argv(emu, 14) == "15"

    def test_overflow_16_args(self, emu, parse_args_bin):
        """16+ args → argc = 15 (truncated, no crash)."""
        args = " ".join(str(i) for i in range(1, 18))
        _run(emu, parse_args_bin, args)
        assert emu.read_byte(ARGV_ARGC) == 15


# ─── Coverage registration (runs after all tests in this module) ──────────────

@pytest.fixture(autouse=True, scope="module")
def _register_coverage_after_all():
    yield
    if _binary_size > 0:
        in_binary = {a for a in _all_executed if _code_base <= a < _code_base + _binary_size}
        register_coverage("shell_parse_args", _binary_size, len(in_binary),
                          edges=_all_edges, binary_path=_binary_path)
