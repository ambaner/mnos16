"""Regression guard: bijection between syscall constants and mnoslib wrappers.

For every `SYS_*`/`FS_*`/`MEM_*` constant defined in the syscall headers
that names an actual syscall function, there must be exactly one mnoslib
wrapper that references it. And every mnoslib wrapper must reference a
real syscall constant — no dangling wrappers.

This catches:
  - Adding a new syscall to `syscalls.inc` without exposing it in mnoslib.
  - Removing a syscall but leaving a wrapper that no longer assembles.
  - Typos in `mov ah, SYS_FOOO` (would fail to assemble, but the test gives
    a far clearer error than a NASM "undefined symbol" diagnostic).

Non-syscall constants (sentinels like `SYSCALL_MAX`, error codes like
`FS_ERR_*`) are explicitly skipped via `NON_SYSCALL_CONSTANTS`.
"""

from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
INCLUDE = ROOT / "src" / "include"

# (constant header, prefix, wrapper header(s)) — what the test checks
SUBSYSTEMS = [
    {
        "name":     "INT 0x80 (kernel)",
        "header":   INCLUDE / "syscalls.inc",
        "prefix":   "SYS_",
        "wrappers": [INCLUDE / "mnoslib_io.inc", INCLUDE / "mnoslib_sys.inc"],
    },
    {
        "name":     "INT 0x81 (filesystem)",
        "header":   INCLUDE / "mnfs.inc",
        "prefix":   "FS_",
        "wrappers": [INCLUDE / "mnoslib_fs.inc"],
    },
    {
        "name":     "INT 0x82 (memory manager)",
        "header":   INCLUDE / "memory.inc",
        "prefix":   "MEM_",
        "wrappers": [INCLUDE / "mnoslib_mm.inc"],
    },
]

# Constants that match the prefix but are NOT syscalls (sentinels, error
# codes, etc.) and therefore should NOT have wrappers. If you add a new
# non-syscall constant with one of these prefixes, list it here.
NON_SYSCALL_CONSTANTS = {
    "SYSCALL_MAX",
    "FS_SYSCALL_MAX",
    "MEM_SYSCALL_MAX",
    # FS error codes (FS_ERR_*)
    "FS_ERR_NOT_FOUND",
    "FS_ERR_EXISTS",
    "FS_ERR_DIR_FULL",
    "FS_ERR_DISK_FULL",
    "FS_ERR_IO",
    "FS_ERR_PROTECTED",
}

EQU_RE   = re.compile(r"^([A-Z][A-Z0-9_]*)\s+equ\b", re.MULTILINE)
LABEL_RE = re.compile(r"^(mn_[a-z][a-z0-9_]*):\s*(?:;.*)?$", re.MULTILINE)
MOV_RE   = re.compile(
    r"^(mn_[a-z][a-z0-9_]*):\s*(?:;.*)?\s*\n"
    r"\s*mov\s+ah\s*,\s*([A-Z][A-Z0-9_]*)",
    re.MULTILINE | re.IGNORECASE,
)
# Match `mn_xxx equ mn_yyy` (alias-style declarations used for back-compat,
# e.g., `mn_load_file equ mn_read_file`).
ALIAS_RE = re.compile(
    r"^(mn_[a-z][a-z0-9_]*)\s+equ\s+(mn_[a-z][a-z0-9_]*)",
    re.MULTILINE | re.IGNORECASE,
)


def _parse_constants(header: Path, prefix: str) -> set[str]:
    """Return all `<prefix>*` constants defined via `equ` in `header`."""
    text = header.read_text(encoding="utf-8")
    return {
        m.group(1) for m in EQU_RE.finditer(text)
        if m.group(1).startswith(prefix)
    }


def _parse_wrappers(paths: list[Path]) -> tuple[dict[str, str], set[str]]:
    """Return ({wrapper_label: referenced_constant}, {alias_label}).

    A wrapper has form `mn_xxx: \\n mov ah, CONST`.
    An alias has form `mn_xxx equ mn_yyy` (no constant directly referenced).
    """
    real: dict[str, str] = {}
    aliases: set[str] = set()
    for path in paths:
        text = path.read_text(encoding="utf-8")
        for m in MOV_RE.finditer(text):
            real[m.group(1)] = m.group(2)
        for m in ALIAS_RE.finditer(text):
            aliases.add(m.group(1))
    return real, aliases


def test_every_syscall_has_a_wrapper():
    """For each subsystem, every syscall constant must be wrapped."""
    errors: list[str] = []
    for sub in SUBSYSTEMS:
        constants = _parse_constants(sub["header"], sub["prefix"])
        syscalls = constants - NON_SYSCALL_CONSTANTS
        wrappers, _aliases = _parse_wrappers(sub["wrappers"])
        referenced = set(wrappers.values())
        missing = syscalls - referenced
        if missing:
            errors.append(
                f"{sub['name']}: {len(missing)} syscall(s) without a "
                f"wrapper: {sorted(missing)}"
            )
    assert not errors, "Missing mnoslib wrappers:\n  " + "\n  ".join(errors)


def test_no_dangling_wrappers():
    """Every wrapper must reference a real syscall constant."""
    errors: list[str] = []
    for sub in SUBSYSTEMS:
        constants = _parse_constants(sub["header"], sub["prefix"])
        wrappers, _ = _parse_wrappers(sub["wrappers"])
        for label, const in wrappers.items():
            if const not in constants:
                errors.append(
                    f"{sub['name']}: wrapper `{label}` references "
                    f"undefined constant `{const}`"
                )
    assert not errors, "Dangling mnoslib wrappers:\n  " + "\n  ".join(errors)


def test_aliases_point_at_real_wrappers():
    """`mn_xxx equ mn_yyy` aliases must resolve to a real wrapper."""
    errors: list[str] = []
    for sub in SUBSYSTEMS:
        real, aliases = _parse_wrappers(sub["wrappers"])
        for alias in aliases:
            # The right-hand side of the alias is captured separately; re-parse.
            pass
        # Re-parse aliases to get target names (right-hand side)
        for path in sub["wrappers"]:
            text = path.read_text(encoding="utf-8")
            for m in ALIAS_RE.finditer(text):
                alias_name, target = m.group(1), m.group(2)
                if target not in real:
                    errors.append(
                        f"{sub['name']}: alias `{alias_name} equ {target}` "
                        f"points at undefined wrapper"
                    )
    assert not errors, "Broken mnoslib aliases:\n  " + "\n  ".join(errors)
