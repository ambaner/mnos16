"""Regression guard: no raw BIOS interrupts in user-mode code.

MNOS16 v0.9.18 established the invariant that **no user-mode source file**
(`src/programs/` or `src/shell/`) calls any BIOS interrupt directly. All
disk, video, keyboard, RTC, and equipment access must route through the
kernel's `INT 0x80/0x81/0x82` syscall layer (typically via mnoslib `mn_*`
wrappers).

If anyone adds a raw `int 0x10`/`0x13`/`0x16`/etc. to a user program or
the shell, this test fires and identifies the offending file/line.

The kernel modules (`src/kernel/`, `src/fs/`, `src/mm/`, `src/loader/`,
`src/boot/`) are intentionally NOT scanned — they are where the kernel's
BIOS abstractions are *implemented*, so raw BIOS interrupts are correct
there.
"""

from __future__ import annotations

import re
from pathlib import Path

# Repo root: tests/ lives at <root>/tests/
ROOT = Path(__file__).resolve().parent.parent

# Directories scanned for the "no raw BIOS" invariant
SCANNED_DIRS = [
    ROOT / "src" / "programs",
    ROOT / "src" / "shell",
]

# Matches `    int 0x10` ... `    int 0x1F` (BIOS range) but NOT
# `int 0x80`/`int 0x81`/`int 0x82` (the kernel syscall interrupts).
# Anchored on the start of a logical instruction line; comments stripped first.
BIOS_INT_RE = re.compile(r"^\s*int\s+0x1[0-9a-fA-F]\b", re.IGNORECASE)


def _strip_comment(line: str) -> str:
    """Remove NASM/MASM-style `;` comments from a source line."""
    semi = line.find(";")
    return line if semi == -1 else line[:semi]


def _collect_violations() -> list[str]:
    hits: list[str] = []
    for root_dir in SCANNED_DIRS:
        if not root_dir.exists():
            continue
        for path in sorted(root_dir.rglob("*")):
            if path.suffix.lower() not in (".asm", ".inc"):
                continue
            for lineno, raw in enumerate(
                path.read_text(encoding="utf-8", errors="replace").splitlines(),
                start=1,
            ):
                code = _strip_comment(raw)
                if BIOS_INT_RE.match(code):
                    rel = path.relative_to(ROOT).as_posix()
                    hits.append(f"  {rel}:{lineno}: {raw.strip()}")
    return hits


def test_no_raw_bios_interrupts_in_apps_or_shell():
    """Every BIOS interrupt call from user-mode code must go through the kernel."""
    violations = _collect_violations()
    assert not violations, (
        "Raw BIOS interrupts found in user-mode code (must use mnoslib / "
        "kernel syscalls instead — see doc/MNOSLIB.md):\n"
        + "\n".join(violations)
    )


def test_scanned_dirs_exist():
    """Sanity: the directories the guard scans must exist (catches refactors)."""
    missing = [d for d in SCANNED_DIRS if not d.exists()]
    assert not missing, f"Scanned dirs missing: {missing}"
