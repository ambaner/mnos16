"""Unit tests for cmdmatch — command prefix matching routine.

Tests the routine that checks if a command buffer starts with a given
command name followed by a space or NUL terminator.
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
CMD_BUF_ADDR = 0x5000
CMD_NAME_ADDR = 0x5100


def _run(emu: MiniOSEmulator, cmdmatch_bin, cmd_buf: str, cmd_name: str) -> bool:
    """Run cmdmatch with cmd_buf and cmd_name, return True if ZF set (match)."""
    global _binary_size, _code_base, _binary_path
    emu.load(cmdmatch_bin)
    _binary_size = emu.code_size
    _code_base = emu.code_base
    _binary_path = cmdmatch_bin

    emu.write_string(CMD_BUF_ADDR, cmd_buf)
    emu.write_string(CMD_NAME_ADDR, cmd_name)
    emu.set_reg("si", CMD_BUF_ADDR)
    emu.set_reg("di", CMD_NAME_ADDR)

    emu.run()
    _all_executed.update(emu.coverage_in_binary)
    _all_edges.update(emu.edges_in_binary)
    return emu.zf


class TestCmdmatch:
    """Command prefix matching tests."""

    def test_exact_match_no_args(self, emu, cmdmatch_bin):
        """'del' matches 'del' (NUL follows command)."""
        assert _run(emu, cmdmatch_bin, "del", "del") is True

    def test_match_with_space_args(self, emu, cmdmatch_bin):
        """'del file.txt' matches 'del' (space follows)."""
        assert _run(emu, cmdmatch_bin, "del file.txt", "del") is True

    def test_match_copy_with_args(self, emu, cmdmatch_bin):
        """'copy src.txt dst.txt' matches 'copy'."""
        assert _run(emu, cmdmatch_bin, "copy src.txt dst.txt", "copy") is True

    def test_match_ren_with_args(self, emu, cmdmatch_bin):
        """'ren old.txt new.txt' matches 'ren'."""
        assert _run(emu, cmdmatch_bin, "ren old.txt new.txt", "ren") is True

    def test_no_match_longer_word(self, emu, cmdmatch_bin):
        """'delete' does NOT match 'del' (no space/NUL after 'del')."""
        assert _run(emu, cmdmatch_bin, "delete", "del") is False

    def test_no_match_different_cmd(self, emu, cmdmatch_bin):
        """'dir' does NOT match 'del'."""
        assert _run(emu, cmdmatch_bin, "dir", "del") is False

    def test_no_match_prefix_only(self, emu, cmdmatch_bin):
        """'co' does NOT match 'copy' (cmd_buf shorter than cmd_name)."""
        assert _run(emu, cmdmatch_bin, "co", "copy") is False

    def test_no_match_empty_buf(self, emu, cmdmatch_bin):
        """Empty cmd_buf does NOT match 'del'."""
        assert _run(emu, cmdmatch_bin, "", "del") is False

    def test_match_single_char_cmd(self, emu, cmdmatch_bin):
        """'x file' matches 'x'."""
        assert _run(emu, cmdmatch_bin, "x file", "x") is True

    def test_no_match_case_sensitive(self, emu, cmdmatch_bin):
        """'Del file' does NOT match 'del' (case sensitive)."""
        assert _run(emu, cmdmatch_bin, "Del file", "del") is False

    def test_match_trailing_spaces(self, emu, cmdmatch_bin):
        """'del   ' matches 'del' (space follows immediately)."""
        assert _run(emu, cmdmatch_bin, "del   ", "del") is True

    def test_no_match_partial_overlap(self, emu, cmdmatch_bin):
        """'rename old new' does NOT match 'ren' (no space after 'ren')."""
        assert _run(emu, cmdmatch_bin, "rename old new", "ren") is False


@pytest.fixture(autouse=True, scope="module")
def _register_coverage_after_all():
    yield
    if _binary_size > 0:
        in_binary = {a for a in _all_executed if _code_base <= a < _code_base + _binary_size}
        register_coverage("cmdmatch", _binary_size, len(in_binary),
                          edges=_all_edges, binary_path=_binary_path)
