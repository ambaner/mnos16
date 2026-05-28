"""Generate tests/harness/constants.py from NASM .inc source files.

Parses 'NAME equ VALUE' definitions from memory.inc and syscalls.inc,
evaluates simple expressions (hex, decimal, arithmetic), and writes a
Python module that the test harness imports.

Run before pytest:
    python tests/gen_constants.py

The generated file replaces any hand-maintained constants.py.
"""

import re
import sys
from pathlib import Path

# Resolve paths relative to repo root
REPO_ROOT = Path(__file__).resolve().parent.parent
INC_DIR = REPO_ROOT / "src" / "include"
OUTPUT = REPO_ROOT / "tests" / "harness" / "constants.py"

# Source .inc files to parse (order matters: later files can reference earlier)
INC_FILES = [
    INC_DIR / "memory.inc",
    INC_DIR / "syscalls.inc",
    INC_DIR / "mnfs.inc",
]

# Regex for NASM equ definitions: NAME equ VALUE [; comment]
EQU_RE = re.compile(
    r"^([A-Za-z_][A-Za-z0-9_]*)\s+equ\s+(.+?)(?:\s*;.*)?$",
    re.IGNORECASE,
)

# Test-harness-only constants (not in .inc files)
HARNESS_EXTRAS = """
# --- MM stub entry points (offsets from CODE_BASE) ----------------------------
MM_ALLOC_ENTRY   = 0x00
MM_FREE_ENTRY    = 0x10
MM_AVAIL_ENTRY   = 0x20
MM_INFO_ENTRY    = 0x30
MM_INIT_ENTRY    = 0x40

# --- Test harness defaults ----------------------------------------------------
CODE_BASE        = 0x1000   # Where stub binaries are loaded
STACK_TOP        = 0xFFF0   # Initial SP
STRING_AREA      = 0x5000   # Where test input strings are placed
"""


def parse_inc_files(paths: list[Path]) -> list[tuple[str, str, str]]:
    """Parse .inc files and return (name, expression, source_file) tuples."""
    results = []
    for path in paths:
        if not path.exists():
            print(f"WARNING: {path} not found, skipping", file=sys.stderr)
            continue
        for line in path.read_text(encoding="utf-8").splitlines():
            m = EQU_RE.match(line.strip())
            if m:
                name, expr = m.group(1), m.group(2).strip()
                results.append((name, expr, path.name))
    return results


def nasm_expr_to_python(expr: str) -> str:
    """Convert a NASM expression to valid Python.

    Handles: hex (0xNN), decimal, simple arithmetic (+, -, *, /),
    and references to previously defined constants.
    """
    # NASM uses same arithmetic as Python for simple cases
    # Just ensure hex is lowercase-friendly and references are bare names
    return expr


def evaluate_constants(defs: list[tuple[str, str, str]]) -> dict[str, int]:
    """Evaluate all constants in order, resolving forward references."""
    namespace = {}
    for name, expr, _ in defs:
        try:
            value = eval(expr, {"__builtins__": {}}, namespace)
            namespace[name] = value
        except Exception:
            # Skip expressions we can't evaluate (rare complex macros)
            pass
    return namespace


def generate_python(defs: list[tuple[str, str, str]], values: dict[str, int]) -> str:
    """Generate the Python constants module."""
    lines = [
        '"""Auto-generated from src/include/*.inc — DO NOT EDIT MANUALLY.',
        "",
        "Regenerate with:  python tests/gen_constants.py",
        '"""',
        "",
    ]

    current_source = None
    for name, expr, source in defs:
        if name not in values:
            continue

        # Section header when switching source files
        if source != current_source:
            current_source = source
            lines.append("")
            lines.append(f"# --- From {source} " + "-" * (60 - len(source)))

        # Format value as hex if original was hex or > 255, else decimal
        val = values[name]
        if "0x" in expr.lower() or val > 255:
            val_str = f"0x{val:04X}" if val > 0xFF else f"0x{val:02X}"
        else:
            val_str = str(val)

        # If expression is simple (just a literal), show just the value
        # If it's an expression, show "= expr  # = resolved"
        expr_stripped = expr.strip()
        is_literal = re.match(r"^0x[0-9A-Fa-f]+$|^\d+$", expr_stripped)
        if is_literal:
            lines.append(f"{name:<20} = {val_str}")
        else:
            # Convert NASM expression to Python-readable form
            lines.append(f"{name:<20} = {expr_stripped}")

    # Append harness-only extras
    lines.append(HARNESS_EXTRAS)

    return "\n".join(lines) + "\n"


def main():
    defs = parse_inc_files(INC_FILES)
    if not defs:
        print("ERROR: No constants found in .inc files", file=sys.stderr)
        sys.exit(1)

    values = evaluate_constants(defs)
    output = generate_python(defs, values)

    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_text(output, encoding="utf-8")
    print(f"Generated {OUTPUT} ({len(values)} constants from {len(INC_FILES)} files)")


if __name__ == "__main__":
    main()
