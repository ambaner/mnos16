"""Regression guard: BASIC file-I/O channels (OPEN/CLOSE/EOF/PRINT#/INPUT#) wired up.

v2.0 introduced four file channels with INPUT/OUTPUT/APPEND modes.
This static test asserts that:

  - OPEN, CLOSE, OUTPUT, APPEND, EOF keywords exist with tokens.
  - OPEN and CLOSE dispatch to handlers in bas_run_direct.
  - PRINT and INPUT both branch on `#` to their channel variants.
  - The basic_io.inc module is %included from basic.asm.
"""

from __future__ import annotations

import re
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
BASIC = ROOT / "src" / "programs" / "basic"


def _slurp(name: str) -> str:
    return (BASIC / name).read_text(encoding="utf-8", errors="replace")


CHANNEL_TOKENS = [
    ("OPEN",   "TOK_OPEN"),
    ("CLOSE",  "TOK_CLOSE"),
    ("OUTPUT", "TOK_OUTPUT"),
    ("APPEND", "TOK_APPEND"),
    ("EOF",    "TOK_EOF_FN"),
]


@pytest.mark.parametrize("keyword,token", CHANNEL_TOKENS)
def test_channel_keyword_registered(keyword: str, token: str):
    tokens = _slurp("basic_tokens.inc")
    assert re.search(rf"^{re.escape(token)}\s+equ\b", tokens, re.MULTILINE), (
        f"{token} missing from basic_tokens.inc"
    )
    assert re.search(rf"KW\s+'{re.escape(keyword)}'\s*,\s*{re.escape(token)}\b",
                     tokens), (
        f"KW '{keyword}', {token} missing from the keyword table"
    )


def test_open_close_dispatched():
    stmt = _slurp("basic_stmt.inc")
    assert re.search(r"cmp\s+al,\s*TOK_OPEN", stmt), "TOK_OPEN not dispatched"
    assert re.search(r"cmp\s+al,\s*TOK_CLOSE", stmt), "TOK_CLOSE not dispatched"
    assert ".brd_open" in stmt, ".brd_open handler missing"
    assert ".brd_close" in stmt, ".brd_close handler missing"


def test_print_input_have_hash_variants():
    stmt = _slurp("basic_stmt.inc")
    assert "bas_stmt_print_hash" in stmt, (
        "bas_stmt_print_hash branch missing from bas_stmt_print — `PRINT #n,` "
        "would write to the screen instead of the channel."
    )
    assert "bas_stmt_input_hash" in stmt, (
        "bas_stmt_input_hash branch missing from bas_stmt_input — `INPUT #n,` "
        "would read from the keyboard instead of the channel."
    )


def test_io_module_present():
    io_inc = BASIC / "basic_io.inc"
    assert io_inc.exists(), "src/programs/basic/basic_io.inc is missing"
    main = _slurp("basic.asm")
    assert re.search(r'%include\s+"basic_io\.inc"', main), (
        '`%include "basic_io.inc"` missing from basic.asm'
    )


def test_eof_function_dispatched():
    expr = _slurp("basic_expr.inc")
    assert "TOK_EOF_FN" in expr, (
        "TOK_EOF_FN is not referenced in basic_expr.inc — EOF() would tokenise "
        "but bas_expr_function would never dispatch it."
    )
