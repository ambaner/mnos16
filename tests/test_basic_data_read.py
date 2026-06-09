"""Regression guard: BASIC DATA / READ / RESTORE are wired into the interpreter.

v2.0 added inline DATA statements with a dedicated TOK_DATA_RAW payload
token (0xF4), READ to consume items into variables, and RESTORE to
rewind the cursor.  This test checks the static wiring.
"""

from __future__ import annotations

import re
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
BASIC = ROOT / "src" / "programs" / "basic"


def _slurp(name: str) -> str:
    return (BASIC / name).read_text(encoding="utf-8", errors="replace")


@pytest.mark.parametrize("keyword,token", [
    ("DATA",    "TOK_DATA"),
    ("READ",    "TOK_READ"),
    ("RESTORE", "TOK_RESTORE"),
])
def test_data_keyword_registered(keyword: str, token: str):
    tokens = _slurp("basic_tokens.inc")
    assert re.search(rf"^{re.escape(token)}\s+equ\b", tokens, re.MULTILINE)
    assert re.search(rf"KW\s+'{re.escape(keyword)}'\s*,\s*{re.escape(token)}\b",
                     tokens)


def test_data_raw_payload_token_defined():
    tokens = _slurp("basic_tokens.inc")
    # Payload token must be in the variable-length-token range (>= 0xF0)
    # so the existing skip helpers treat it correctly.
    m = re.search(r"^TOK_DATA_RAW\s+equ\s+(0x[0-9A-Fa-f]+)\b", tokens,
                  re.MULTILINE)
    assert m, "TOK_DATA_RAW equate missing from basic_tokens.inc"
    val = int(m.group(1), 16)
    assert val >= 0xF0, (
        f"TOK_DATA_RAW = {val:#x}; expected ≥ 0xF0 so it shares the variable-"
        f"length skip rules with TOK_STR_LIT etc."
    )


def test_data_read_restore_dispatched():
    stmt = _slurp("basic_stmt.inc")
    for tok in ("TOK_DATA", "TOK_READ", "TOK_RESTORE"):
        assert re.search(rf"cmp\s+al,\s*{tok}", stmt), (
            f"{tok} not dispatched in bas_run_direct"
        )


def test_dataread_module_present():
    dr_inc = BASIC / "basic_dataread.inc"
    assert dr_inc.exists(), "src/programs/basic/basic_dataread.inc is missing"
    main = _slurp("basic.asm")
    assert re.search(r'%include\s+"basic_dataread\.inc"', main), (
        '`%include "basic_dataread.inc"` missing from basic.asm'
    )


def test_data_cursor_reset_in_clear():
    # NEW / CLEAR must reset both halves of the DATA cursor; if not,
    # READ across a NEW would re-pick-up an old cursor.
    load = _slurp("basic_load.inc")
    assert "bas_data_line_ptr" in load and "bas_data_sub_off" in load, (
        "bas_cmd_clear no longer zeros the DATA cursor — stale READ state "
        "would survive NEW/CLEAR."
    )


def test_out_of_data_error_defined():
    err = _slurp("basic_err.inc")
    assert "bas_err_outdata" in err, (
        "bas_err_outdata entry missing from the error table"
    )
    assert re.search(r"BERR_OUTOFDATA\s+equ\b",
                     _slurp("basic_data.inc")), (
        "BERR_OUTOFDATA equate missing from basic_data.inc"
    )
