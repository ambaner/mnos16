"""Regression guards for the v0.9.19.0 BASIC runtime bug-fix pass.

Six low-level state-hygiene bugs were fixed in v0.9.19.0.  All are
flag- or scratch-slot interactions that only manifest in multi-line
BASIC programs, so they were invisible to the existing static suite.
These guards re-fail if the same patterns sneak back into the source.

See CHANGELOG.md [0.9.19.0] for the narrative.
"""

from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BASIC = ROOT / "src" / "programs" / "basic"


def _slurp(name: str) -> str:
    return (BASIC / name).read_text(encoding="utf-8", errors="replace")


# --- Bug #1: bas_expr_eval epilogue CF leak ---------------------------------

def test_bug1_expr_eval_clears_cf_on_success():
    """bas_expr_eval must end with `clc` on the success path.

    Bug: the final `cmp ..., BVAR_STRING` left CF=1 whenever the result
    wasn't a string, so callers' `jc` spuriously branched to error.
    The fix is an explicit `clc` at `.bee_clear_cf` before the shared
    `.bee_done: ret`.
    """
    src = _slurp("basic_expr.inc")
    # Carve out the bas_expr_eval body up through .bee_done.
    m = re.search(
        r"^bas_expr_eval:(.*?)^\.bee_done:\s*\n\s*ret",
        src, re.MULTILINE | re.DOTALL,
    )
    assert m, "bas_expr_eval body not found — refactor or rename?"
    body = m.group(1)
    # `.bee_clear_cf:` label must be present and immediately followed by
    # an explicit CF-clearer: either `clc` or the BAS_RET_OK macro (which
    # expands to `clc / ret`).
    assert re.search(
        r"^\.bee_clear_cf:\s*\n\s*(clc\b|BAS_RET_OK\b)", body, re.MULTILINE
    ), (
        "bas_expr_eval success path must `clc` (or use BAS_RET_OK) before "
        "falling into .bee_done — the final `cmp ..., BVAR_STRING` leaks "
        "CF=1 otherwise (bug #1)."
    )


# --- Bug #2: be_cmp_emit flags clobbered by dispatch ------------------------

def test_bug2_cmp_emit_preserves_flags_across_dispatch():
    """`.be_cmp_emit` must save/restore flags around its `cmp bl, op` chain.

    Bug: the dispatcher's own `cmp bl, op` series clobbered the flags
    produced by the value comparison, so the per-operator emit-handlers
    couldn't trust `jl/jg/je`.
    """
    src = _slurp("basic_expr.inc")
    m = re.search(
        r"^\.be_cmp_emit:(.*?)(?=^[A-Za-z\.])",
        src, re.MULTILINE | re.DOTALL,
    )
    assert m, ".be_cmp_emit body not found"
    body = m.group(1)
    assert "pushf" in body and "popf" in body, (
        ".be_cmp_emit must bracket its operator-dispatch chain with "
        "pushf/popf or the value-comparison flags are clobbered (bug #2)."
    )


# --- Bug #3: lex_mode multi-line-ref list -----------------------------------

def test_bug3_lex_mode4_for_multi_lineref_lists():
    """`bas_lex_after_emit` must promote ON+GOTO/GOSUB into mode 4.

    Bug: mode 1 (single line-ref) is reset to 0 after the first
    LINEREF emit, so `ON C GOTO 1010, 2010, 3010` emitted 2010 and
    3010 as TOK_INT_LIT.  Fix introduced mode 4 (multi-ref list,
    persistent across commas).
    """
    src = _slurp("basic_lex.inc")
    assert re.search(r"\.lae_set4\b", src), (
        ".lae_set4 (mode-4 setter) missing — multi-line-ref lists in "
        "ON C GOTO/GOSUB will collapse after the first ref (bug #3)."
    )
    # mode==4 must be checked in .lnm_done or its skip-reset logic, i.e.
    # the file must reference the literal byte value 4 in mode-compare
    # context.  We just look for a `cmp ... , 4` near a mode label.
    assert re.search(r"bas_lex_mode\b.*?,\s*4\b", src, re.DOTALL) or \
           re.search(r",\s*4\b.*?bas_lex_mode", src, re.DOTALL), (
        "Mode 4 must be referenced somewhere in basic_lex.inc — see "
        ".lae_set1 / .lnm_done."
    )


# --- Bug #4: bas_stmt_read must not write bas_scratch_d ---------------------

def test_bug4_stmt_read_does_not_clobber_scratch_d():
    """READ must use bas_scratch_b, never bas_scratch_d.

    Bug: bas_run_program_from_cur owns bas_scratch_d (it holds the
    precomputed next-line offset across each bas_run_direct call).
    READ writing scratch_d caused the runtime to jump to a bogus line
    after every READ.
    """
    src = _slurp("basic_dataread.inc")
    m = re.search(
        r"^bas_stmt_read:(.*?)(?=^bas_stmt_|\Z)",
        src, re.MULTILINE | re.DOTALL,
    )
    assert m, "bas_stmt_read body not found"
    body = m.group(1)
    # Filter out comment lines (anything starting with ;).
    code_only = "\n".join(
        line for line in body.splitlines()
        if not line.lstrip().startswith(";")
    )
    assert "bas_scratch_d" not in code_only, (
        "bas_stmt_read must not touch bas_scratch_d — that slot is owned "
        "by bas_run_program_from_cur for the next-line offset (bug #4). "
        "Use bas_scratch_b instead."
    )


def test_bug4_scratch_d_marked_reserved():
    """The bas_scratch_d declaration must carry the RESERVED warning."""
    src = _slurp("basic_data.inc")
    # Find the line declaring bas_scratch_d and its surrounding comment.
    idx = src.find("bas_scratch_d")
    assert idx >= 0, "bas_scratch_d declaration missing"
    # Look back/forward ~400 chars for the RESERVED marker.
    window = src[max(0, idx - 400): idx + 400]
    assert "RESERVED" in window, (
        "bas_scratch_d must be annotated as RESERVED for "
        "bas_run_program_from_cur (bug #4) so no future statement handler "
        "trips on the same alias."
    )


# --- Bug #5: bas_str_to_fname stack discipline ------------------------------

def test_bug5_str_to_fname_stack_balanced():
    """bas_str_to_fname's success path must pop exactly the same items
    as the .bstf_bad2 error path (both reach the same stack state).

    Bug: an extra `pop ds` on the success path consumed `es` into `ds`
    and misaligned all subsequent pops; `ret` jumped to garbage.
    """
    src = _slurp("basic_io.inc")
    m = re.search(
        r"^bas_str_to_fname:(.*?)(?=^[A-Za-z_])",
        src, re.MULTILINE | re.DOTALL,
    )
    assert m, "bas_str_to_fname body not found"
    body = m.group(1)
    # Both .bstf_ok (or equivalent success ret) and .bstf_bad2 must exist
    # and execute the same epilogue pop sequence (es, di, si, cx, bx, ax).
    epilogue = (r"pop\s+es\s*\n"
                r"\s*pop\s+di\s*\n"
                r"\s*pop\s+si\s*\n"
                r"\s*pop\s+cx\s*\n"
                r"\s*pop\s+bx\s*\n"
                r"\s*pop\s+ax")
    matches = re.findall(epilogue, body)
    assert len(matches) >= 2, (
        "bas_str_to_fname must have at least two matching 6-pop epilogues "
        "(one for success, one for the .bstf_bad2 error path); found "
        f"{len(matches)}.  An off-by-one pop here corrupts the return "
        "address (bug #5)."
    )


# --- Bug #6: EOF(N) dispatch must not re-consume '(' ------------------------

def test_bug6_eof_path_does_not_double_consume_lparen():
    """`.bef_eof_path` must not look for `(` again — the common
    function-dispatch prelude already consumed it (same as LEFT$/MID$).

    Bug: doubled `(` check made every `EOF(n)` call return ?Syntax,
    breaking `WHILE NOT EOF(n)` loops.
    """
    src = _slurp("basic_expr.inc")
    m = re.search(
        r"^\.bef_eof_path:(.*?)(?=^[A-Za-z\.])",
        src, re.MULTILINE | re.DOTALL,
    )
    assert m, ".bef_eof_path body not found"
    body = m.group(1)
    # The body must start (after any comments) with `call bas_expr_eval_int`,
    # NOT with a `cmp al, '('`.
    assert not re.search(r"cmp\s+al,\s*'\('", body), (
        ".bef_eof_path must not re-check for `(` — the prelude in "
        "bas_expr_func has already consumed it (bug #6).  See the "
        "LEFT$/MID$/RIGHT$ paths for the right pattern."
    )
    assert "call bas_expr_eval_int" in body, (
        ".bef_eof_path must call bas_expr_eval_int to read the channel#."
    )
