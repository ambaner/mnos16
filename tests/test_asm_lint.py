"""pytest wrapper for tools/asm_lint.py — runs the same static checks in CI.

The lint also runs from tools/build.ps1, but having it as a pytest case
means a developer who runs `pytest` standalone (without going through the
build script) still gets the protection.
"""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LINT = ROOT / "tools" / "asm_lint.py"


def test_asm_lint_clean():
    """tools/asm_lint.py must report zero violations on src/programs/basic/."""
    assert LINT.exists(), f"asm_lint.py missing at {LINT}"
    result = subprocess.run(
        [sys.executable, str(LINT), "--verbose"],
        cwd=str(ROOT),
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        msg = (
            f"asm_lint reported violations (exit={result.returncode}).\n\n"
            f"--- stdout ---\n{result.stdout}\n"
            f"--- stderr ---\n{result.stderr}\n"
        )
        raise AssertionError(msg)
    # On success, the verbose output should mention "clean".
    assert "clean" in result.stdout, (
        f"asm_lint exited 0 but stdout did not contain 'clean':\n"
        f"{result.stdout}"
    )
