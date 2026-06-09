"""
Regression tests for the BASIC TPA memory-layout invariants.

History:
  v0.9.19 a:  `bas_load_buf` lived at TPA offset 0xC000.  When v2.0 grew
              BASIC's code past 0xC000, every LOAD overwrote BASIC's own
              code, producing `#UD Invalid Opcode at 0000:C1D1` on the
              first instruction fetch after the read.  Fix: bumped
              bas_load_buf to 0xC400 and shrank BAS_PROG_BASE.

  v0.9.19 b:  `BAS_BSS_BASE` was 0xB000 — INSIDE the code segment.  All
              BSS scratch buffers (bas_token_buf at 0xB160, bas_line_buf
              at 0xB060, bas_var_table at 0xB280, ...) physically aliased
              executable code bytes.  The interpreter "worked" only
              because the overlapping bytes happened to be unreferenced
              data.  Once code shifted (e.g., extra debug strings), the
              REPL hung silently after the first command because dispatch
              code was scribbled on by the tokenizer writing into
              bas_token_buf.  Fix: moved BAS_BSS_BASE above the code end
              to 0xC400, with a NASM-time TIMES pad-and-assert in
              basic.asm enforcing the invariant.

Invariants under test:
  * BAS_BSS_BASE       >= USER_PROG_BASE + basic.mnx payload size
  * bas_load_buf       >= BAS_BSS_BASE + BAS_BSS_SIZE
  * BAS_PROG_BASE      >= bas_load_buf + BAS_LOAD_BUF_LEN

These also enforced at NASM-time by the TIMES guard at the bottom of
src/programs/basic/basic.asm.  This Python test is a second line of
defence that runs in CI and surfaces a human-readable diff when any
invariant is violated.

The header constants are pulled at runtime from the .inc / .asm sources
so this test doesn't drift when somebody bumps an address.
"""

from __future__ import annotations

import re
import struct
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parent.parent
BASIC_MNX = REPO_ROOT / "build" / "boot" / "basic.mnx"
BASIC_DATA_INC = REPO_ROOT / "src" / "programs" / "basic" / "basic_data.inc"
MEMORY_INC = REPO_ROOT / "src" / "include" / "memory.inc"


def _extract_equ(path: Path, name: str) -> int:
    """Extract an `<name> equ <expr>` value from a NASM source file.

    Handles bare integer literals (hex 0x... or decimal).  Doesn't try to
    evaluate arithmetic expressions — if somebody refactors a constant
    this test cares about into an expression, this regex will need
    updating, but the failure mode is loud (AssertionError).
    """
    src = path.read_text(encoding="utf-8", errors="replace")
    pattern = (
        rf"^\s*{re.escape(name)}\s+equ\s+"
        r"(0x[0-9A-Fa-f]+|\d+)\b"
    )
    m = re.search(pattern, src, re.MULTILINE)
    if not m:
        raise AssertionError(
            f"Could not find `{name} equ <integer literal>` in {path}.  "
            f"If this constant has been renamed, moved, or rewritten as an "
            f"expression, update test_basic_load_buf_headroom."
        )
    literal = m.group(1)
    return int(literal, 0)  # int(..., 0) auto-detects 0x prefix


def _mnex_payload_size(mnx_path: Path) -> int:
    """Parse the MNEX header and return the raw code+data payload size.

    Header layout (see tools/pack_module.py):
        <4s magic 'MNEX'>
        <H   sector_count>
        <H   flags>
        <H   reloc_count>
        <H   entry_offset>
        <reloc_count * H  reloc table>
        <payload bytes>
    """
    data = mnx_path.read_bytes()
    assert len(data) >= 12, f"{mnx_path} too small to contain MNEX header"
    magic, sector_count, flags, reloc_count, entry_offset = struct.unpack_from(
        "<4sHHHH", data, 0
    )
    assert magic == b"MNEX", f"{mnx_path} has bad magic {magic!r}"
    prefix = 12 + 2 * reloc_count
    payload = len(data) - prefix
    assert payload > 0, f"{mnx_path}: computed payload size {payload} <= 0"
    return payload


@pytest.mark.skipif(
    not BASIC_MNX.exists(),
    reason="basic.mnx not built — run build.bat first",
)
def test_basic_load_buf_does_not_overlap_code():
    """`bas_load_buf` must sit above the end of BASIC's code+data segment."""
    user_prog_base = _extract_equ(MEMORY_INC, "USER_PROG_BASE")
    bas_load_buf = _extract_equ(BASIC_DATA_INC, "bas_load_buf")
    payload = _mnex_payload_size(BASIC_MNX)

    code_end = user_prog_base + payload  # one-past-last byte of payload
    headroom = bas_load_buf - code_end

    assert headroom >= 0, (
        f"BASIC LOAD buffer overlaps code!\n"
        f"  USER_PROG_BASE = 0x{user_prog_base:04X}\n"
        f"  basic.mnx payload = {payload} bytes (0x{payload:X})\n"
        f"  code spans 0x{user_prog_base:04X}..0x{code_end - 1:04X}\n"
        f"  bas_load_buf   = 0x{bas_load_buf:04X}\n"
        f"  overshoot      = {-headroom} bytes\n"
        f"At runtime, every LOAD will overwrite the tail of BASIC's own "
        f"code segment, causing #UD on the next instruction fetch.  Fix: "
        f"bump `bas_load_buf` (and shrink `BAS_PROG_BASE`) in "
        f"src/programs/basic/basic_data.inc, or shrink BASIC."
    )


@pytest.mark.skipif(
    not BASIC_MNX.exists(),
    reason="basic.mnx not built — run build.bat first",
)
def test_basic_prog_base_above_load_buf():
    """Tokenised-program area must sit above the LOAD scratch buffer.

    The buffer is 4 KB (BAS_LOAD_BUF_LEN).  If BAS_PROG_BASE creeps below
    bas_load_buf + 0x1000, a LOAD that fills the buffer will overwrite the
    start of the user's program.
    """
    bas_load_buf = _extract_equ(BASIC_DATA_INC, "bas_load_buf")
    buf_len = _extract_equ(BASIC_DATA_INC, "BAS_LOAD_BUF_LEN")
    prog_base = _extract_equ(BASIC_DATA_INC, "BAS_PROG_BASE")

    buf_end = bas_load_buf + buf_len
    assert prog_base >= buf_end, (
        f"BAS_PROG_BASE (0x{prog_base:04X}) overlaps bas_load_buf "
        f"(0x{bas_load_buf:04X}..0x{buf_end - 1:04X}).  A LOAD that fills "
        f"the 4 KB scratch buffer would corrupt the tokenised program area."
    )


@pytest.mark.skipif(
    not BASIC_MNX.exists(),
    reason="basic.mnx not built — run build.bat first",
)
def test_basic_bss_does_not_overlap_code():
    """BSS region MUST sit above the end of BASIC's code+data segment.

    Until v0.9.19, BAS_BSS_BASE was 0xB000 — well INSIDE the code
    segment (which ends near 0xC2E9 in current builds).  This worked
    "by accident" because the BSS labels happened to land on bytes
    that no live code path read.  Any shift of code layout (a new
    constant, an extra dbg string, even an alignment change) could
    bring BSS labels into collision with live executable code or with
    referenced read-only tables — and once `bas_token_buf` lands in
    the middle of `bas_handle_input_line`, tokenising any input wipes
    out the dispatcher and the REPL silently no-ops.

    The actual enforcement is the NASM-time `times (BAS_BSS_BASE -
    0x8000) - ($ - $$) db 0` line at the bottom of basic.asm — NASM
    fails the build on overflow with "TIMES value is negative".  This
    Python test verifies that guard is still present (so CI catches
    any well-meaning refactor that removes it).
    """
    basic_asm = (REPO_ROOT / "src" / "programs" / "basic" / "basic.asm").read_text(
        encoding="utf-8", errors="replace"
    )
    # The line must reference BAS_BSS_BASE and use a TIMES with subtractive
    # arithmetic against $-$$ (which is what makes the build fail on overflow).
    pat = re.compile(
        r"^\s*times\s+\(?\s*BAS_BSS_BASE\s*-\s*0x8000\s*\)?\s*-\s*\(\s*\$\s*-\s*\$\$\s*\)\s+db\s+0",
        re.MULTILINE,
    )
    assert pat.search(basic_asm), (
        "Missing NASM-time TIMES guard in src/programs/basic/basic.asm.\n"
        "Expected a line of the form:\n"
        "    times (BAS_BSS_BASE - 0x8000) - ($ - $$) db 0\n"
        "This is the hard build-time assertion that BASIC's code segment\n"
        "stays disjoint from the BSS region.  Without it, code growth can\n"
        "silently push BSS labels (bas_token_buf, bas_line_buf, etc.) into\n"
        "the running interpreter's own code bytes, causing silent REPL\n"
        "failures that are extremely painful to diagnose.\n"
        "\n"
        "If you reworded the line, update the regex in this test too."
    )


@pytest.mark.skipif(
    not BASIC_MNX.exists(),
    reason="basic.mnx not built — run build.bat first",
)
def test_basic_load_buf_above_bss():
    """LOAD scratch buffer must sit above the BSS region.

    BSS contains stacks, line index, var table, etc.  If bas_load_buf
    overlapped any of these, every LOAD would corrupt interpreter state.
    """
    bss_base = _extract_equ(BASIC_DATA_INC, "BAS_BSS_BASE")
    bss_size = _extract_equ(BASIC_DATA_INC, "BAS_BSS_SIZE")
    bas_load_buf = _extract_equ(BASIC_DATA_INC, "bas_load_buf")

    bss_end = bss_base + bss_size
    assert bas_load_buf >= bss_end, (
        f"bas_load_buf (0x{bas_load_buf:04X}) overlaps BSS region "
        f"(0x{bss_base:04X}..0x{bss_end - 1:04X}).  A LOAD would corrupt "
        f"interpreter state (variable table, stacks, etc.)."
    )


def test_basic_no_stale_pre_bss_absolute_addresses():
    """Catch BSS labels left at pre-v0.9.19.0 addresses.

    v0.9.19.0 moved BAS_BSS_BASE from 0xB000 to 0xC400.  basic_defn.inc
    was missed in that pass — it carried 5 hardcoded equ's at 0xBC60+
    for the DEF FN active-call frame.  Those addresses ended up INSIDE
    the executable code segment (which now extends to 0xC400) and every
    bas_load_file / bas_error wrote a zero byte to 0xBC60, corrupting
    whatever interpreter instruction happened to live there.  Symptom:
    after BASIC loaded a program, the FIRST runtime IF/expression-eval
    in the program failed with a meaningless "?Internal Error" because
    bas_expr_eval's code had been silently nuked.

    Catch this regression for the WHOLE BASIC source tree, not just
    basic_defn.inc: any equ in the BASIC source pointing into the
    pre-v0.9.19 BSS window (0xB000..0xC3FF) is presumed stale.
    """
    basic_dir = REPO_ROOT / "src" / "programs" / "basic"
    bad: list[tuple[Path, int, str]] = []
    # Match `<label> equ 0xB### / 0xC0## .. 0xC3##` — both halves of the
    # stale BSS window (0xB000..0xBFFF was the pre-v0.9.19.0 region, and
    # 0xC000..0xC3FF was the v0.9.19.0 load buffer location).
    pat = re.compile(
        r"^\s*([A-Za-z_][A-Za-z_0-9]*)\s+equ\s+"
        r"(0x[Bb][0-9A-Fa-f]{3}|0x[Cc][0-3][0-9A-Fa-f]{2})\b"
    )
    for inc in sorted(basic_dir.glob("*.inc")) + sorted(basic_dir.glob("*.asm")):
        for lineno, raw in enumerate(
            inc.read_text(encoding="utf-8", errors="replace").splitlines(), 1
        ):
            # Strip a trailing comment so commented-out / annotation
            # addresses (e.g., "; was at 0xB160") don't trip the regex.
            code = raw.split(";", 1)[0]
            m = pat.match(code)
            if m:
                bad.append((inc, lineno, m.group(0).strip()))

    assert not bad, (
        "Found `equ` definitions pointing into the pre-v0.9.19.0 BSS window "
        "(0xB000..0xC3FF).  These addresses now sit INSIDE the executable code "
        "segment and any write to them will corrupt interpreter code.  Move "
        "them into the BSS region (0xC400..0xD3FF) — see basic_data.inc.\n\n"
        + "\n".join(f"  {p.name}:{ln}: {txt}" for p, ln, txt in bad)
    )
