"""Assembler helper — builds NASM stub files into flat binaries for testing."""

import subprocess
import shutil
from pathlib import Path

# Project root (MNOS16/)
PROJECT_ROOT = Path(__file__).resolve().parent.parent.parent

# Directories
STUBS_DIR = PROJECT_ROOT / "tests" / "stubs"
BIN_DIR = PROJECT_ROOT / "tests" / "bin"
INCLUDE_DIRS = [
    PROJECT_ROOT / "src" / "include",
    PROJECT_ROOT / "src" / "shell",
    PROJECT_ROOT / "src" / "kernel",
]


def find_nasm() -> str:
    """Find NASM executable — check tools/nasm/ first, then PATH."""
    local_nasm = PROJECT_ROOT / "tools" / "nasm" / "nasm.exe"
    if local_nasm.exists():
        return str(local_nasm)
    # Try PATH
    nasm = shutil.which("nasm")
    if nasm:
        return nasm
    raise FileNotFoundError(
        "NASM not found. Install it or run build.bat to auto-download."
    )


def assemble_stub(stub_name: str, extra_flags: list[str] | None = None) -> Path:
    """Assemble a stub .asm file into a flat binary.

    Args:
        stub_name: Base name without extension (e.g., "stub_parse_args")
        extra_flags: Additional NASM flags (e.g., ["-l", "listing.lst"])

    Returns:
        Path to the assembled binary file.
    """
    src = STUBS_DIR / f"{stub_name}.asm"
    out = BIN_DIR / f"{stub_name}.bin"
    BIN_DIR.mkdir(parents=True, exist_ok=True)

    if not src.exists():
        raise FileNotFoundError(f"Stub not found: {src}")

    cmd = [find_nasm(), "-f", "bin"]
    for inc_dir in INCLUDE_DIRS:
        cmd.extend(["-I", f"{inc_dir}/"])
    if extra_flags:
        cmd.extend(extra_flags)
    cmd.extend(["-o", str(out), str(src)])

    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(
            f"NASM assembly failed for {stub_name}:\n{result.stderr}"
        )

    return out


def assemble_all_stubs() -> dict[str, Path]:
    """Assemble all stub files in the stubs directory.

    Returns:
        Dict mapping stub name to binary path.
    """
    results = {}
    for asm_file in sorted(STUBS_DIR.glob("stub_*.asm")):
        name = asm_file.stem
        results[name] = assemble_stub(name)
    return results
