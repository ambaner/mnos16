"""Regression guard: BASIC DEF FN (user-defined functions) wired up.

v2.0 added `DEF FN name(param) = expr` and `FN name(arg)`.  DEF must
correctly disambiguate between `DEF SEG` (POKE segment) and `DEF FN`
(user function), and TOK_FN must be reachable from the expression
primary so `FN name(...)` evaluates.
"""

from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BASIC = ROOT / "src" / "programs" / "basic"


def _slurp(name: str) -> str:
    return (BASIC / name).read_text(encoding="utf-8", errors="replace")


def test_def_seg_and_def_fn_tokens_present():
    tokens = _slurp("basic_tokens.inc")
    # Both keywords share the TOK_DEF prefix.
    assert re.search(r"^TOK_DEF\s+equ\b", tokens, re.MULTILINE)
    assert re.search(r"^TOK_SEG\s+equ\b", tokens, re.MULTILINE)
    assert re.search(r"^TOK_FN\s+equ\b", tokens, re.MULTILINE), (
        "TOK_FN equate missing — DEF FN cannot be tokenised."
    )
    assert re.search(r"KW\s+'DEF'\s*,\s*TOK_DEF\b", tokens)
    assert re.search(r"KW\s+'SEG'\s*,\s*TOK_SEG\b", tokens)
    assert re.search(r"KW\s+'FN'\s*,\s*TOK_FN\b", tokens)


def test_def_dispatch_handles_both_branches():
    stmt = _slurp("basic_stmt.inc")
    # DEF must go through the wrapper that picks SEG vs FN.  If it
    # still calls bas_stmt_def_seg directly, DEF FN silently looks like
    # a syntax error (or worse, like DEF SEG).
    assert re.search(r"\.brd_def\b[\s\S]{0,120}?call\s+bas_stmt_def\b", stmt), (
        "DEF dispatch in bas_run_direct should call `bas_stmt_def` (the SEG/FN "
        "dispatcher), not bas_stmt_def_seg directly."
    )


def test_def_fn_module_present():
    defn = BASIC / "basic_defn.inc"
    assert defn.exists(), "src/programs/basic/basic_defn.inc is missing"
    main = _slurp("basic.asm")
    assert re.search(r'%include\s+"basic_defn\.inc"', main), (
        '`%include "basic_defn.inc"` missing from basic.asm'
    )
    body = defn.read_text(encoding="utf-8", errors="replace")
    for label in ("bas_stmt_def:", "bas_stmt_def_fn:",
                  "bas_userfn_register:", "bas_userfn_find:",
                  "bas_userfn_invoke:"):
        assert label in body, f"Required label `{label}` missing from basic_defn.inc"


def test_fn_token_dispatched_in_expr_primary():
    expr = _slurp("basic_expr.inc")
    # The primary must compare against TOK_FN and route to bas_userfn_invoke.
    assert re.search(r"cmp\s+al,\s*TOK_FN", expr), (
        "bas_expr_primary no longer dispatches TOK_FN — `FN name(...)` "
        "would fail with `Syntax error` in expressions."
    )
    assert "bas_userfn_invoke" in expr, (
        "bas_userfn_invoke not referenced from basic_expr.inc"
    )


def test_userfn_state_reset_on_error_and_clear():
    # The recursion-guard byte must be cleared on error recovery AND on
    # NEW/CLEAR; otherwise an error inside FN leaves the guard set and
    # all future FN calls fail with `Too complex`.
    err = _slurp("basic_err.inc")
    assert "bas_userfn_depth" in err, (
        "bas_err_recover no longer clears bas_userfn_depth — an error inside "
        "DEF FN body would permanently disable user functions."
    )
    load = _slurp("basic_load.inc")
    assert "bas_userfn_count" in load and "bas_userfn_depth" in load, (
        "bas_cmd_clear no longer resets the DEF FN table or recursion guard."
    )
