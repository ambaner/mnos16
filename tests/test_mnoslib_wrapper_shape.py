"""Regression guard: every mnoslib wrapper has the canonical shape.

Each mnoslib wrapper is *contractually* a 1:1 thin shim:

    mn_<name>:
        mov ah, <CONSTANT>
        int 0x8[012]
        ret

No extra setup, no extra arithmetic, no internal data references. This
keeps wrappers data-free (no cross-module relocations) and makes their
preservation contract identical to that of the underlying syscall.
See doc/MNOSLIB.md §1 and §6 for the rationale.

This test parses every `mn_*` label in the four split mnoslib headers and
verifies the shape. It also checks that the `int` number matches the
constant's prefix (SYS_→0x80, FS_→0x81, MEM_→0x82).
"""

from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
INCLUDE = ROOT / "src" / "include"

WRAPPER_FILES = [
    INCLUDE / "mnoslib_io.inc",
    INCLUDE / "mnoslib_sys.inc",
    INCLUDE / "mnoslib_fs.inc",
    INCLUDE / "mnoslib_mm.inc",
]

# Constant prefix -> required interrupt number for that subsystem
PREFIX_TO_INT = {
    "SYS_": "0x80",
    "FS_":  "0x81",
    "MEM_": "0x82",
}

LABEL_RE = re.compile(r"^(mn_[a-z][a-z0-9_]*):\s*(?:;.*)?$")
MOV_RE   = re.compile(r"^\s*mov\s+ah\s*,\s*([A-Z][A-Z0-9_]*)\s*(?:;.*)?$",
                      re.IGNORECASE)
INT_RE   = re.compile(r"^\s*int\s+(0x8[012])\s*(?:;.*)?$", re.IGNORECASE)
RET_RE   = re.compile(r"^\s*ret\b\s*(?:;.*)?$", re.IGNORECASE)


def _meaningful(line: str) -> bool:
    """True if the line carries an instruction (not blank/comment/directive/alias)."""
    s = line.strip()
    if not s or s.startswith(";"):
        return False
    if s.startswith("%"):
        # NASM preprocessor directives (`%endif`, `%ifdef`, etc.) — not body.
        return False
    if re.match(r"^mn_[a-z][a-z0-9_]*\s+equ\b", s, re.IGNORECASE):
        # `mn_xxx equ mn_yyy` aliases are declarations, not wrapper body.
        return False
    return True


def _parse_wrappers(path: Path) -> list[tuple[str, list[tuple[int, str]]]]:
    """Return [(wrapper_label, [(lineno, line), ...]), ...] for each label.

    Each block runs from the line AFTER the label up to (exclusive of) the
    next label or end of file, dropping blank/comment-only lines.
    """
    lines = path.read_text(encoding="utf-8").splitlines()
    blocks: list[tuple[str, list[tuple[int, str]]]] = []
    current_label: str | None = None
    current_body: list[tuple[int, str]] = []
    for idx, raw in enumerate(lines, start=1):
        m = LABEL_RE.match(raw)
        if m:
            if current_label is not None:
                blocks.append((current_label, current_body))
            current_label = m.group(1)
            current_body = []
            continue
        if current_label is not None and _meaningful(raw):
            current_body.append((idx, raw))
    if current_label is not None:
        blocks.append((current_label, current_body))
    return blocks


def _check_shape(path: Path) -> list[str]:
    """Return a list of human-readable error messages for malformed wrappers."""
    errors: list[str] = []
    rel = path.relative_to(ROOT).as_posix()
    for label, body in _parse_wrappers(path):
        if len(body) != 3:
            errors.append(
                f"  {rel}: wrapper `{label}` must have exactly 3 instructions "
                f"(mov ah, X / int 0xN / ret); got {len(body)}: "
                f"{[ln for _, ln in body]}"
            )
            continue
        (mov_no, mov_ln), (int_no, int_ln), (ret_no, ret_ln) = body
        mov_match = MOV_RE.match(mov_ln)
        int_match = INT_RE.match(int_ln)
        ret_match = RET_RE.match(ret_ln)
        if not mov_match:
            errors.append(
                f"  {rel}:{mov_no}: wrapper `{label}` line 1 must be "
                f"`mov ah, CONST`, got: {mov_ln.strip()!r}"
            )
            continue
        if not int_match:
            errors.append(
                f"  {rel}:{int_no}: wrapper `{label}` line 2 must be "
                f"`int 0x8[012]`, got: {int_ln.strip()!r}"
            )
            continue
        if not ret_match:
            errors.append(
                f"  {rel}:{ret_no}: wrapper `{label}` line 3 must be `ret`, "
                f"got: {ret_ln.strip()!r}"
            )
            continue

        const = mov_match.group(1)
        actual_int = int_match.group(1)
        prefix = next(
            (p for p in PREFIX_TO_INT if const.startswith(p)), None
        )
        if prefix is None:
            errors.append(
                f"  {rel}:{mov_no}: wrapper `{label}` references unknown "
                f"constant prefix in `{const}` (expected SYS_/FS_/MEM_)"
            )
            continue
        expected_int = PREFIX_TO_INT[prefix]
        if actual_int.lower() != expected_int.lower():
            errors.append(
                f"  {rel}:{int_no}: wrapper `{label}` uses `{const}` "
                f"(prefix {prefix.rstrip('_')}) but calls `int {actual_int}`; "
                f"expected `int {expected_int}`"
            )
    return errors


def test_every_wrapper_has_canonical_shape():
    """Each mn_* wrapper must be exactly mov ah, X / int 0xN / ret."""
    all_errors: list[str] = []
    for path in WRAPPER_FILES:
        assert path.exists(), f"Expected wrapper file missing: {path}"
        all_errors.extend(_check_shape(path))
    assert not all_errors, (
        "mnoslib wrappers violate the canonical shape contract "
        "(see doc/MNOSLIB.md §1):\n" + "\n".join(all_errors)
    )


def test_each_wrapper_file_has_at_least_one_wrapper():
    """Sanity: empty wrapper header would silently allow regressions."""
    empties = []
    for path in WRAPPER_FILES:
        if not _parse_wrappers(path):
            empties.append(path.relative_to(ROOT).as_posix())
    assert not empties, f"Wrapper files with zero wrappers: {empties}"
