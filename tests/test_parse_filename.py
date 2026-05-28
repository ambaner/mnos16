"""Unit tests for run_parse_filename — 8.3 filename parser.

Tests the routine that parses "filename.ext args" into an 11-byte padded
8.3 name, sets run_ext_provided, and sets run_args_ptr.
"""

import pytest
from pathlib import Path
from tests.harness.emulator import MiniOSEmulator
from tests.harness.constants import STRING_AREA
from tests.conftest import register_coverage

_all_executed: set[int] = set()
_all_edges: set[tuple[int, int]] = set()
_binary_size: int = 0
_code_base: int = 0
_binary_path: Path | None = None


def _find_data_addrs(emu: MiniOSEmulator):
    """Find the data labels in the stub binary.

    The stub layout is:
      0x1000: call run_parse_filename
      0x1003: hlt
      0x1004: run_parse_filename code...
      ...after all code: data labels (run_fname_buf, etc.)

    We scan for the data by looking at the binary.  The stub_parse_fname.asm
    places data labels after all code.  We find them by reading the assembled
    binary's listing — but for simplicity, we scan backward from end of binary
    for the run_empty_args NUL byte pattern.

    Actually, the data is at fixed offsets relative to the binary layout.
    Let's calculate from the binary by finding the 11 zero bytes of
    run_fname_buf followed by run_ext_provided(0), run_args_ptr(0,0),
    run_empty_args(0).
    """
    # Search for the data area: 11 bytes of init (fname_buf) + 1 + 2 + 1 = 15 bytes at end
    data = emu.read_bytes(emu.code_base, emu.code_size)
    # The data area is the last 15 bytes of the binary
    data_offset = len(data) - 15
    return {
        "fname_buf": emu.code_base + data_offset,
        "ext_provided": emu.code_base + data_offset + 11,
        "args_ptr": emu.code_base + data_offset + 12,
        "empty_args": emu.code_base + data_offset + 14,
    }


def _run(emu: MiniOSEmulator, parse_fname_bin, input_str: str) -> dict:
    """Run run_parse_filename, return parsed results."""
    global _binary_size, _code_base, _binary_path
    emu.load(parse_fname_bin)
    _binary_size = emu.code_size
    _code_base = emu.code_base
    _binary_path = parse_fname_bin

    # Write input string and set SI to point to it
    emu.write_string(STRING_AREA, input_str)
    emu.set_reg("si", STRING_AREA)

    emu.run()
    _all_executed.update(emu.coverage_in_binary)
    _all_edges.update(emu.edges_in_binary)

    addrs = _find_data_addrs(emu)
    fname_bytes = emu.read_bytes(addrs["fname_buf"], 11)
    fname = fname_bytes.decode("ascii", errors="replace")
    ext_provided = emu.read_byte(addrs["ext_provided"])
    args_ptr = emu.read_word(addrs["args_ptr"])
    args = emu.read_string(args_ptr) if args_ptr else ""

    return {
        "fname": fname,
        "name": fname[:8],
        "ext": fname[8:11],
        "ext_provided": ext_provided,
        "args": args,
    }


class TestParseFilenameBasic:
    """Basic filename parsing."""

    def test_simple_name(self, emu, parse_fname_bin):
        """Simple name without extension."""
        r = _run(emu, parse_fname_bin, "hello")
        assert r["name"] == "HELLO   "
        assert r["ext_provided"] == 0

    def test_name_with_ext(self, emu, parse_fname_bin):
        """Name with extension."""
        r = _run(emu, parse_fname_bin, "hello.mnx")
        assert r["name"] == "HELLO   "
        assert r["ext"] == "MNX"
        assert r["ext_provided"] == 1

    def test_uppercase_passthrough(self, emu, parse_fname_bin):
        """Already uppercase stays uppercase."""
        r = _run(emu, parse_fname_bin, "HELLO.MNX")
        assert r["name"] == "HELLO   "
        assert r["ext"] == "MNX"

    def test_mixed_case(self, emu, parse_fname_bin):
        """Mixed case is uppercased."""
        r = _run(emu, parse_fname_bin, "HeLLo.MnX")
        assert r["name"] == "HELLO   "
        assert r["ext"] == "MNX"


class TestParseFilenameArgs:
    """Filename + argument parsing."""

    def test_name_with_args(self, emu, parse_fname_bin):
        """Name followed by args."""
        r = _run(emu, parse_fname_bin, "test foo bar")
        assert r["name"] == "TEST    "
        assert r["args"] == "foo bar"

    def test_name_ext_with_args(self, emu, parse_fname_bin):
        """Name.ext followed by args."""
        r = _run(emu, parse_fname_bin, "test.mnx foo")
        assert r["name"] == "TEST    "
        assert r["ext"] == "MNX"
        assert r["args"] == "foo"

    def test_no_args(self, emu, parse_fname_bin):
        """Name only — args is empty."""
        r = _run(emu, parse_fname_bin, "hello")
        assert r["args"] == ""


class TestParseFilenameTruncation:
    """Truncation of long names/extensions."""

    def test_long_name(self, emu, parse_fname_bin):
        """Name > 8 chars is truncated."""
        r = _run(emu, parse_fname_bin, "longfilename.mnx")
        assert r["name"] == "LONGFILE"
        assert r["ext"] == "MNX"

    def test_long_ext(self, emu, parse_fname_bin):
        """Extension > 3 chars is truncated."""
        r = _run(emu, parse_fname_bin, "test.abcd")
        assert r["ext"] == "ABC"

    def test_max_padded(self, emu, parse_fname_bin):
        """Exactly 8.3 fills completely."""
        r = _run(emu, parse_fname_bin, "12345678.123")
        assert r["name"] == "12345678"
        assert r["ext"] == "123"


@pytest.fixture(autouse=True, scope="module")
def _register_coverage_after_all():
    yield
    if _binary_size > 0:
        in_binary = {a for a in _all_executed if _code_base <= a < _code_base + _binary_size}
        register_coverage("run_parse_filename", _binary_size, len(in_binary),
                          edges=_all_edges, binary_path=_binary_path)
