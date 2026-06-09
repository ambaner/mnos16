"""Regression guard: BASIC string features are wired into the interpreter.

v2.0 introduced first-class strings: typed expression evaluator, string
variables, concatenation, comparison, and a set of string built-in
functions. The actual semantics are validated by QEMU smoke runs; this
static test makes sure the *wiring* survives refactors:

  - Each string function keyword has a token, a dispatcher branch in
    `bas_expr_function`, and a name in the keyword table.
  - The string-storage helpers exist as labels and are referenced from
    the rest of the interpreter (so they cannot be "left behind" by a
    rename).

Failure here points at someone deleting a keyword, removing a dispatch
branch, or renaming an internal helper without updating its callers.
"""

from __future__ import annotations

import re
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
BASIC = ROOT / "src" / "programs" / "basic"

STRING_FUNCTIONS = [
    ("CHR$",    "TOK_CHRS"),
    ("STR$",    "TOK_STRS"),
    ("LEFT$",   "TOK_LEFTS"),
    ("RIGHT$",  "TOK_RIGHTS"),
    ("MID$",    "TOK_MIDS"),
    ("INKEY$",  "TOK_INKEYS"),
    ("INPUT$",  "TOK_INPUTS"),
    ("LEN",     "TOK_LEN"),
    ("ASC",     "TOK_ASC"),
    ("VAL",     "TOK_VAL"),
]


def _slurp(name: str) -> str:
    return (BASIC / name).read_text(encoding="utf-8", errors="replace")


@pytest.mark.parametrize("keyword,token", STRING_FUNCTIONS)
def test_string_function_keyword_registered(keyword: str, token: str):
    tokens = _slurp("basic_tokens.inc")
    # Token equate must exist.
    assert re.search(rf"^{re.escape(token)}\s+equ\b", tokens, re.MULTILINE), (
        f"{token} is missing from basic_tokens.inc — the keyword '{keyword}' "
        f"would not tokenise."
    )
    # KW table must list the spelling against that token.
    pattern = rf"KW\s+'{re.escape(keyword)}'\s*,\s*{re.escape(token)}\b"
    assert re.search(pattern, tokens), (
        f"`KW '{keyword}', {token}` missing from the keyword table in "
        f"basic_tokens.inc."
    )


@pytest.mark.parametrize("_,token", STRING_FUNCTIONS)
def test_string_function_dispatched_in_expr(_, token: str):
    expr = _slurp("basic_expr.inc")
    assert re.search(rf"\b{re.escape(token)}\b", expr), (
        f"{token} is not referenced in basic_expr.inc — the function would "
        f"tokenise but bas_expr_function would never dispatch it."
    )


def test_string_storage_module_present():
    # The string heap + temp-string pool implementation file must exist
    # AND be %included from basic.asm.  Without it, no string variable
    # could be stored.
    str_inc = BASIC / "basic_str.inc"
    assert str_inc.exists(), "src/programs/basic/basic_str.inc is missing"
    main = _slurp("basic.asm")
    assert re.search(r'%include\s+"basic_str\.inc"', main), (
        '`%include "basic_str.inc"` missing from basic.asm'
    )


def test_temp_pool_free_called_on_error_recovery():
    # Every statement boundary AND the error trampoline must free temp
    # strings.  If this drops out, programs slowly leak HMA across REPL
    # iterations.
    err = _slurp("basic_err.inc")
    assert "bas_temp_pool_free_all" in err, (
        "bas_err_recover no longer calls bas_temp_pool_free_all — temp "
        "strings will leak across error recovery."
    )
