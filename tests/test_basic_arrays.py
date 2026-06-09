"""Regression guard: BASIC arrays (DIM + indexing) are wired into the interpreter.

v2.0 added 1-D numeric and string arrays via `DIM`.  This static test
checks that the keyword/token/handler chain is intact and that the
%include for the array module is present.
"""

from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BASIC = ROOT / "src" / "programs" / "basic"


def _slurp(name: str) -> str:
    return (BASIC / name).read_text(encoding="utf-8", errors="replace")


def test_dim_keyword_registered():
    tokens = _slurp("basic_tokens.inc")
    assert re.search(r"^TOK_DIM\s+equ\b", tokens, re.MULTILINE), (
        "TOK_DIM missing from basic_tokens.inc"
    )
    assert re.search(r"KW\s+'DIM'\s*,\s*TOK_DIM\b", tokens), (
        "KW 'DIM', TOK_DIM missing from the keyword table"
    )


def test_dim_dispatched_in_run_loop():
    stmt = _slurp("basic_stmt.inc")
    # The bas_run_direct dispatcher must compare against TOK_DIM and
    # branch to a handler.
    assert re.search(r"cmp\s+al,\s*TOK_DIM", stmt), (
        "bas_run_direct no longer dispatches TOK_DIM — `DIM` would syntax-error"
    )
    assert re.search(r"\.brd_dim\b", stmt), (
        ".brd_dim handler label missing from basic_stmt.inc"
    )


def test_array_module_present():
    arr_inc = BASIC / "basic_array.inc"
    assert arr_inc.exists(), "src/programs/basic/basic_array.inc is missing"
    main = _slurp("basic.asm")
    assert re.search(r'%include\s+"basic_array\.inc"', main), (
        '`%include "basic_array.inc"` missing from basic.asm'
    )


def test_array_value_types_present():
    # Both BVAR_NUM_ARRAY and BVAR_STR_ARRAY type tags must exist
    # somewhere in the BASIC sources — they are the storage discriminators
    # used by the variable table and freed in CLEAR/NEW.
    blob = "".join(p.read_text(encoding="utf-8", errors="replace")
                   for p in BASIC.glob("*.inc"))
    assert "BVAR_NUM_ARRAY" in blob, "BVAR_NUM_ARRAY type tag missing"
    assert "BVAR_STR_ARRAY" in blob, "BVAR_STR_ARRAY type tag missing"
