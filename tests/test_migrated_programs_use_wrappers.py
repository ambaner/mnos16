"""Regression guard: fully-migrated user programs must use mnoslib wrappers.

MNOS16 v0.9.18 migrated EDIT, BASIC, SYSINFO, and MNMON to use mnoslib
`call mn_*` wrappers exclusively, eliminating raw `int 0x80/0x81/0x82`
syscalls from these programs.

If anyone adds a raw `int 0x8N` to one of these programs (e.g., copy-pastes
old idioms from a tutorial or an un-migrated SHELL command file), this
test fires.

The mnoslib wrapper headers themselves (`src/include/mnoslib_*.inc`) are
NOT scanned — those legitimately contain `int 0x8N` as the body of every
wrapper. They are the *definitions* of the wrappers, not callers.

To migrate another program later: add its directory or file to
MIGRATED_PROGRAM_ROOTS and ensure all its `int 0x8N` sites are converted.
"""

from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# Programs / directories that have been fully migrated to mnoslib.
# Anything under these roots must not contain a raw `int 0x8[012]`.
MIGRATED_PROGRAM_ROOTS = [
    ROOT / "src" / "programs" / "edit",
    ROOT / "src" / "programs" / "basic",
    ROOT / "src" / "programs" / "sysinfo",
    ROOT / "src" / "programs" / "mnmon.asm",
]

# The wrapper definitions themselves — these MUST contain `int 0x8N`.
# Excluded from the scan to avoid self-flagging.
WRAPPER_HEADERS = {
    ROOT / "src" / "include" / "mnoslib_io.inc",
    ROOT / "src" / "include" / "mnoslib_sys.inc",
    ROOT / "src" / "include" / "mnoslib_fs.inc",
    ROOT / "src" / "include" / "mnoslib_mm.inc",
    ROOT / "src" / "include" / "mnoslib.inc",
}

SYSCALL_INT_RE = re.compile(r"^\s*int\s+0x8[012]\b", re.IGNORECASE)


def _strip_comment(line: str) -> str:
    semi = line.find(";")
    return line if semi == -1 else line[:semi]


def _iter_source_files():
    for root in MIGRATED_PROGRAM_ROOTS:
        if not root.exists():
            continue
        if root.is_file():
            yield root
            continue
        for path in sorted(root.rglob("*")):
            if path.suffix.lower() in (".asm", ".inc") and path not in WRAPPER_HEADERS:
                yield path


def _scan() -> list[str]:
    hits: list[str] = []
    for path in _iter_source_files():
        for lineno, raw in enumerate(
            path.read_text(encoding="utf-8", errors="replace").splitlines(),
            start=1,
        ):
            code = _strip_comment(raw)
            if SYSCALL_INT_RE.match(code):
                rel = path.relative_to(ROOT).as_posix()
                hits.append(f"  {rel}:{lineno}: {raw.strip()}")
    return hits


def test_migrated_programs_have_no_raw_syscalls():
    """EDIT, BASIC, SYSINFO, MNMON must use `call mn_*`, never `int 0x8N`."""
    violations = _scan()
    assert not violations, (
        "Raw `int 0x80/0x81/0x82` found in a fully-migrated program. "
        "Use the corresponding mnoslib wrapper (see doc/MNOSLIB.md §4):\n"
        + "\n".join(violations)
    )


def test_migrated_roots_exist():
    """Sanity: each declared migrated root must still exist."""
    missing = [r for r in MIGRATED_PROGRAM_ROOTS if not r.exists()]
    assert not missing, f"Migrated program roots missing: {missing}"


def test_wrapper_headers_exist():
    """Sanity: the wrapper headers excluded from the scan must exist."""
    missing = [h for h in WRAPPER_HEADERS if not h.exists()]
    assert not missing, f"Wrapper headers missing: {missing}"
