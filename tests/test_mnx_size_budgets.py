"""Regression guard: MNX binary sizes must stay within their sector budgets.

User-mode `.MNX` programs in MNOS16 are constrained by:
  - Disk layout (the directory entry encodes size in 512-byte sectors)
  - TPA size (USER_PROG_MAX = 30 KB at 0x8000-0xF7FF)
  - Test/cosmetic budgets noted in the CHANGELOG (e.g., basic.mnx grew
    from 21 to 22 sectors in v0.9.18 — any further growth should be a
    conscious decision, not an accidental side effect).

This test asserts each shipped MNX is within its sector budget. A budget
bump is intentional and requires updating BUDGETS below — that single-line
change forces the author to acknowledge the size increase in code review.

Sample-only sources that are intentionally excluded from the standard
build (e.g., `src/programs/hello.asm`, listed in `$SkipPrograms` in
`tools/build.ps1`) are NOT entered into BUDGETS at all — they are not
shipped artifacts.  The reverse test `test_every_shipped_mnx_has_a_budget`
catches any newly-shipped MNX that does not have a budget entry, so the
two tests together cover both directions.

In CI, the `test` job downloads the `MNOS16-build` artifact produced by
the `build` job (see `.github/workflows/build.yml`), so the `.mnx` files
exist when the test runs.  If the download step is missing or the build
job did not produce a file, the test SKIPS rather than fails — matching
the convention used by `test_exec` and `test_relocation`.
"""

from __future__ import annotations

import math
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
BOOT_DIR = ROOT / "build" / "boot"

# Per-binary sector budgets. To raise a budget: edit the value here and
# explain why in the CHANGELOG entry that bumps it.
#
# NOTE: hello.mnx is NOT listed here — `src/programs/hello.asm` is kept
# only as a "how to write a simple MNOS program" example and is excluded
# from the build (see $SkipPrograms in tools/build.ps1) and from the VHD.
SECTOR_SIZE = 512
BUDGETS: dict[str, int] = {
    "mnmon.mnx":      6,    # Bumped via mnoslib migration in v0.9.18.
    "sysinfo.mnx":    7,    # Stable since v0.9.18 mnoslib migration.
    "edit.mnx":      15,    # Stable since v0.9.17.
    "basic.mnx":     21,    # Briefly bumped to 22 mid-v0.9.18 by the mnoslib
                            # umbrella, then returned to 21 after the
                            # edit_find/basic_load debug-syscall sites were
                            # collapsed into `call mn_dbg_*` (each site
                            # shrank by ~2 bytes).
}

# Absolute hard ceiling: MNX programs cannot exceed the TPA size
# (USER_PROG_MAX_SEC = 60 sectors = 30 KB), regardless of budget.
TPA_MAX_SECTORS = 60


@pytest.mark.parametrize("name,max_sectors", sorted(BUDGETS.items()))
def test_mnx_within_sector_budget(name: str, max_sectors: int):
    """Each MNX file must be no larger than its declared sector budget."""
    path = BOOT_DIR / name
    if not path.exists():
        pytest.skip(
            f"{name} not built (checked {path.relative_to(ROOT).as_posix()}). "
            f"In CI this means the build-artifact download step did not run "
            f"or did not include this file; locally, run build.bat first.  "
            f"Consistent with test_exec/test_relocation skip-on-missing."
        )
    size_bytes = path.stat().st_size
    actual_sectors = math.ceil(size_bytes / SECTOR_SIZE)
    assert actual_sectors <= max_sectors, (
        f"{name}: {size_bytes} bytes = {actual_sectors} sectors, "
        f"exceeds budget of {max_sectors} sectors "
        f"({max_sectors * SECTOR_SIZE} bytes). "
        f"If this growth is intentional, raise the budget in "
        f"tests/test_mnx_size_budgets.py and document it in CHANGELOG.md."
    )


@pytest.mark.parametrize("name,max_sectors", sorted(BUDGETS.items()))
def test_mnx_fits_in_tpa(name: str, max_sectors: int):
    """No MNX budget may exceed the TPA hard ceiling (USER_PROG_MAX_SEC)."""
    assert max_sectors <= TPA_MAX_SECTORS, (
        f"{name}: budget of {max_sectors} sectors exceeds the TPA "
        f"ceiling of {TPA_MAX_SECTORS} sectors. The program could not be "
        f"loaded even if the file existed."
    )


def test_every_shipped_mnx_has_a_budget():
    """A newly added MNX without a budget would silently bypass this guard."""
    if not BOOT_DIR.exists():
        pytest.skip("build/boot/ not present — run the build first.")
    actual_mnx = {p.name for p in BOOT_DIR.glob("*.mnx")}
    # Test-only MNX files (suffixed _test) are exempt.
    actual_mnx = {n for n in actual_mnx if "_test" not in n}
    budgeted = set(BUDGETS.keys())
    missing = actual_mnx - budgeted
    assert not missing, (
        f"MNX files in build/boot/ without a sector budget: {sorted(missing)}. "
        f"Add an entry to BUDGETS in tests/test_mnx_size_budgets.py."
    )
