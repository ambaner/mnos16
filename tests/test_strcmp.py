"""Unit tests for strcmp — string comparison routine.

Tests the routine that compares two NUL-terminated strings and sets ZF.
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

# Addresses for test strings
STR1_ADDR = 0x5000
STR2_ADDR = 0x5100


def _run(emu: MiniOSEmulator, strcmp_bin, s1: str, s2: str) -> bool:
    """Run strcmp with two strings, return True if ZF is set (equal)."""
    global _binary_size, _code_base, _binary_path
    emu.load(strcmp_bin)
    _binary_size = emu.code_size
    _code_base = emu.code_base
    _binary_path = strcmp_bin

    emu.write_string(STR1_ADDR, s1)
    emu.write_string(STR2_ADDR, s2)
    emu.set_reg("si", STR1_ADDR)
    emu.set_reg("di", STR2_ADDR)

    emu.run()
    _all_executed.update(emu.coverage_in_binary)
    _all_edges.update(emu.edges_in_binary)
    return emu.zf


class TestStrcmp:
    """String comparison tests."""

    def test_equal_strings(self, emu, strcmp_bin):
        assert _run(emu, strcmp_bin, "hello", "hello") is True

    def test_different_strings(self, emu, strcmp_bin):
        assert _run(emu, strcmp_bin, "hello", "world") is False

    def test_prefix_mismatch(self, emu, strcmp_bin):
        assert _run(emu, strcmp_bin, "hello", "help") is False

    def test_empty_strings_equal(self, emu, strcmp_bin):
        assert _run(emu, strcmp_bin, "", "") is True

    def test_one_empty(self, emu, strcmp_bin):
        assert _run(emu, strcmp_bin, "a", "") is False

    def test_other_empty(self, emu, strcmp_bin):
        assert _run(emu, strcmp_bin, "", "a") is False

    def test_case_sensitive(self, emu, strcmp_bin):
        """strcmp is case-sensitive — 'Hello' != 'hello'."""
        assert _run(emu, strcmp_bin, "Hello", "hello") is False

    def test_single_char_equal(self, emu, strcmp_bin):
        assert _run(emu, strcmp_bin, "x", "x") is True

    def test_single_char_different(self, emu, strcmp_bin):
        assert _run(emu, strcmp_bin, "x", "y") is False

    def test_long_equal(self, emu, strcmp_bin):
        s = "a" * 100
        assert _run(emu, strcmp_bin, s, s) is True

    def test_long_differ_at_end(self, emu, strcmp_bin):
        s1 = "a" * 99 + "b"
        s2 = "a" * 99 + "c"
        assert _run(emu, strcmp_bin, s1, s2) is False


@pytest.fixture(autouse=True, scope="module")
def _register_coverage_after_all():
    yield
    if _binary_size > 0:
        in_binary = {a for a in _all_executed if _code_base <= a < _code_base + _binary_size}
        register_coverage("strcmp", _binary_size, len(in_binary),
                          edges=_all_edges, binary_path=_binary_path)
