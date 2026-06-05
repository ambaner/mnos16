"""Regression guard: `%include "mnoslib*.inc"` must come AFTER the first label.

A user program (`.MNX`) or relocatable module starts executing at its
first executable byte. If `%include "mnoslib.inc"` appears BEFORE the
program's first label, NASM emits the wrapper bodies first and the CPU
jumps straight into `mov ah, FS_LIST_FILES / int 0x81 / ret` instead of
the program's startup code — a silent, baffling crash on launch.

This is documented as the placement rule in doc/MNOSLIB.md §2 and is the
single biggest mnoslib footgun. The test enforces it across every `.asm`
in `src/programs/` and the shell.

`entry:` is the conventional first label for `.MNX` user programs, but
relocatable system modules (SHELL.SYS) use different labels — so the test
locates the first NASM label in each file and asserts that mnoslib
includes come strictly after it.

Programs that don't use mnoslib at all are simply skipped.
"""

from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

SCANNED_ASMS = list((ROOT / "src" / "programs").rglob("*.asm")) + \
               list((ROOT / "src" / "shell").rglob("*.asm"))

# Any `name:` at column 0 (NASM convention for code labels). Local labels
# starting with `.` are skipped — those don't represent top-level entry points.
FIRST_LABEL_RE = re.compile(r"^([a-zA-Z_][\w$]*)\s*:\s*(?:;.*)?$")
# Match `%include "mnoslib.inc"` or `%include "mnoslib_xx.inc"`.
MNOSLIB_INC_RE = re.compile(
    r'^\s*%include\s+"(mnoslib(?:_[a-z]+)?\.inc)"',
    re.IGNORECASE,
)


def _scan_asm(path: Path) -> tuple[int | None, str | None, list[tuple[int, str]]]:
    """Return (first_label_lineno, first_label_name, [(include_lineno, name), ...])."""
    first_label_line: int | None = None
    first_label_name: str | None = None
    includes: list[tuple[int, str]] = []
    for lineno, raw in enumerate(
        path.read_text(encoding="utf-8", errors="replace").splitlines(),
        start=1,
    ):
        if first_label_line is None:
            m = FIRST_LABEL_RE.match(raw)
            if m:
                first_label_line = lineno
                first_label_name = m.group(1)
        inc = MNOSLIB_INC_RE.match(raw)
        if inc:
            includes.append((lineno, inc.group(1)))
    return first_label_line, first_label_name, includes


def test_mnoslib_include_comes_after_first_label():
    """`%include "mnoslib*.inc"` must appear strictly AFTER the first label."""
    errors: list[str] = []
    for asm in SCANNED_ASMS:
        first_line, first_name, includes = _scan_asm(asm)
        if not includes:
            continue  # Program doesn't use mnoslib — nothing to enforce.
        rel = asm.relative_to(ROOT).as_posix()
        if first_line is None:
            errors.append(
                f"  {rel}: includes {[i[1] for i in includes]} but has no "
                f"code label — review placement manually"
            )
            continue
        for inc_lineno, inc_name in includes:
            if inc_lineno <= first_line:
                errors.append(
                    f"  {rel}:{inc_lineno}: `%include \"{inc_name}\"` "
                    f"appears at or before first label `{first_name}:` "
                    f"(line {first_line}) — the loader will jump into "
                    f"wrapper code instead of program startup. Move "
                    f"include below all code/data."
                )
    assert not errors, (
        "mnoslib include placement violations (see doc/MNOSLIB.md §2):\n"
        + "\n".join(errors)
    )


def test_scan_set_is_nonempty():
    """Sanity: the scan must actually find programs to check."""
    assert SCANNED_ASMS, "No .asm files found under src/programs or src/shell"
