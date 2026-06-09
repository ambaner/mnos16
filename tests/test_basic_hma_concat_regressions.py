"""Regression guards for the v0.9.19.0 BASIC runtime bug-fix pass.

A single bug was fixed in v0.9.19.0, but it was severe: a stack-frame
off-by-one in ``bas_str_concat`` that corrupted the HMA heap on every
multi-term string concatenation.  The function's ``push si / push di /
push bp / mov bp, sp`` prologue puts the saved SI at ``[bp+4]`` and
saved DI at ``[bp+2]`` (with the return IP at ``[bp+6]``), but the
LHS/RHS reloads used ``[bp+6]`` and ``[bp+4]`` respectively.

Effect: the "LHS" copy read a descriptor from the return-address
bytes (so the copy source was garbage in the code segment), and the
"RHS" copy used the LHS descriptor's length — which for a three-way
concat like ``S1$ + ", " + S2$ + "!"`` is 12 bytes instead of 1.
That 12-byte write started at ``dst+LLen = 0x840`` and overran the
trailing free block's MCB header at ``0x842``, breaking ``mm_free``'s
forward-coalesce check and stranding the rest of the HMA heap.

See CHANGELOG.md [0.9.19.0] for the narrative.
"""

from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BASIC = ROOT / "src" / "programs" / "basic"


def _slurp(name: str) -> str:
    return (BASIC / name).read_text(encoding="utf-8", errors="replace")


def _carve(src: str, label: str, end_anchor: str) -> str:
    """Return the function body from ``label:`` up to (but excluding)
    ``end_anchor`` (a regex matching the next label or end-of-function)."""
    pattern = rf"^{re.escape(label)}:(.*?)(?={end_anchor})"
    m = re.search(pattern, src, re.MULTILINE | re.DOTALL)
    assert m, f"{label}: body not found in source"
    return m.group(1)


# --- Bug: bas_str_concat stack-frame off-by-one -----------------------------

def test_bas_str_concat_lhs_reload_uses_bp_plus_4():
    """The LHS descriptor reload in bas_str_concat must use ``[bp+4]``.

    After ``push si / push di / push bp / mov bp, sp`` the saved SI
    (passed in as LHS by the caller) lives at ``[bp+4]``.  Reading from
    ``[bp+6]`` instead picks up the return IP, which is what shipped
    in v0.9.19.0 and corrupted the HMA heap.
    """
    body = _carve(_slurp("basic_str.inc"), "bas_str_concat", r"^\.bscat_long:")
    # Exactly one `mov si, [bp+4]` for the LHS reload, and zero `mov si, [bp+6]`.
    assert re.search(r"\bmov\s+si,\s*\[bp\+4\]", body), (
        "bas_str_concat must reload the LHS descriptor from [bp+4] "
        "(the saved SI slot).  See v0.9.19.0 notes."
    )
    assert not re.search(r"\bmov\s+si,\s*\[bp\+6\]", body), (
        "bas_str_concat must NOT read from [bp+6] — that is the saved "
        "return IP, not the LHS descriptor.  The off-by-one regression "
        "from v0.9.19.0 has returned (see CHANGELOG.md [0.9.19.0])."
    )


def test_bas_str_concat_rhs_reload_uses_bp_plus_2():
    """The RHS descriptor reload in bas_str_concat must use ``[bp+2]``.

    With the prologue above, the saved DI (RHS) lives at ``[bp+2]``.
    Reading ``[bp+4]`` (the v0.9.19.0 bug) would re-source the LHS
    descriptor and copy LLen bytes again, overrunning the temp
    buffer by ``LLen - RLen`` bytes into the next MCB header.
    """
    body = _carve(_slurp("basic_str.inc"), "bas_str_concat", r"^\.bscat_long:")
    # Find the RHS reload — must come after the first LHS reload and the
    # first bas_str_copy call.
    after_first_copy = re.split(r"\bcall\s+bas_str_copy\b", body, maxsplit=1)
    assert len(after_first_copy) == 2, (
        "bas_str_concat must contain two bas_str_copy calls (LHS then RHS)."
    )
    rhs_section = after_first_copy[1]
    assert re.search(r"\bmov\s+si,\s*\[bp\+2\]", rhs_section), (
        "bas_str_concat must reload the RHS descriptor from [bp+2] "
        "(the saved DI slot) before the second bas_str_copy call."
    )
    assert not re.search(r"\bmov\s+si,\s*\[bp\+4\]", rhs_section), (
        "bas_str_concat must NOT reload the RHS descriptor from [bp+4] — "
        "that is the LHS slot.  This was the v0.9.19.0 HMA-corruption bug."
    )
