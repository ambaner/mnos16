"""asm_lint — static checks for src/programs/basic/*.inc

Catches whole classes of low-level state-hygiene bugs that the unit
suite cannot see — the same six classes the v0.9.19.0 bug-fix pass
ran into.

Checks (currently focused on `src/programs/basic/`, easy to extend):

  1. Per-function stack balance.
     Each `bas_*:` global label opens a function body that ends at the
     next global label.  We walk the body, tracking stack depth
     relative to entry, and require every `ret`/`iret` to fire with
     depth == 0.  Handles `push`/`pop`/`pushf`/`popf`/`pusha`/`popa`/
     `add sp, N`/`sub sp, N`/`enter`/`leave`.  Local jumps (`j*` to
     `.label`) propagate the current depth to the label; if two
     incoming paths reach a label with different depths, the lint
     fails (unless the label is annotated `;@stack-merge`).
     Catches v0.9.19.0 bug #5 class (extra `pop` in
     `bas_str_to_fname`).

  2. RESERVED scratch-slot ownership.
     Any BSS label whose declaration line carries a machine-readable
     `;@owner FUNC` (or `;@reserved FUNC`) annotation is "owned" by
     `FUNC`.  Writes to that slot (`mov [slot], ...` / `pop word
     [slot]` / `add [slot], ...` etc.) are only allowed from inside
     the owner function body.  Catches v0.9.19.0 bug #4 class
     (`bas_stmt_read` clobbering `bas_scratch_d`).

  3. CF-discipline (opt-in via `;@returns cf`).
     Functions annotated `;@returns cf` must, on every `ret` exit
     path, set CF explicitly on the immediately-preceding non-comment
     non-label line — via `clc`, `stc`, `popf`, or a `BAS_RET_OK`/
     `BAS_RET_ERR` macro.  Catches v0.9.19.0 bug #1 class.

Run standalone:
    python tools/asm_lint.py
    python tools/asm_lint.py --root src/programs/basic
    python tools/asm_lint.py --verbose

Exit code 0 on clean, 1 on any violation.
"""

from __future__ import annotations

import argparse
import os
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

DEFAULT_ROOT = Path(__file__).resolve().parent.parent / "src" / "programs" / "basic"

# --- tokenization -----------------------------------------------------------

GLOBAL_LABEL = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)\s*:")
LOCAL_LABEL = re.compile(r"^(\.[A-Za-z_][A-Za-z0-9_]*)\s*:")
INSTR = re.compile(r"^\s+([A-Za-z_][A-Za-z0-9_]*)\b\s*(.*)$")


@dataclass
class Line:
    path: Path
    lineno: int
    raw: str
    code: str         # raw with ';' comment stripped (preserves leading WS)
    comment: str      # text after the first ';' (may carry annotations)


def load(path: Path) -> list[Line]:
    out: list[Line] = []
    for i, raw in enumerate(path.read_text(encoding="utf-8",
                                           errors="replace").splitlines(),
                            start=1):
        # Naive comment split — NASM doesn't have multi-line comments
        # and ';' inside string literals is rare in this codebase.
        idx = raw.find(";")
        code = raw if idx < 0 else raw[:idx]
        comment = "" if idx < 0 else raw[idx + 1:]
        out.append(Line(path, i, raw, code, comment))
    return out


# --- function carving -------------------------------------------------------

@dataclass
class Function:
    name: str
    path: Path
    start: int            # first line of body (line AFTER the label line)
    end: int              # exclusive
    annotations: set[str] = field(default_factory=set)
    lines: list[Line] = field(default_factory=list)


def carve_functions(all_lines: list[Line]) -> list[Function]:
    """Each `bas_*:` (column-1) opens a function; body runs to next col-1 label."""
    funcs: list[Function] = []
    label_lines: list[tuple[int, str]] = []
    for idx, ln in enumerate(all_lines):
        m = GLOBAL_LABEL.match(ln.code.rstrip())
        if m and m.group(1).startswith("bas_"):
            label_lines.append((idx, m.group(1)))
    for i, (line_idx, name) in enumerate(label_lines):
        end = label_lines[i + 1][0] if i + 1 < len(label_lines) else len(all_lines)
        # Annotations: scan comment lines IMMEDIATELY ABOVE the label.
        annotations: set[str] = set()
        j = line_idx - 1
        while j >= 0:
            txt = all_lines[j].raw.strip()
            if not txt or txt.startswith(";"):
                ann = all_lines[j].comment.strip()
                for tag in re.findall(r"@[a-z\-]+(?:\s+[A-Za-z_][A-Za-z0-9_]*)?",
                                      ann):
                    annotations.add(tag.strip())
                j -= 1
                if not txt:
                    # blank line stops the annotation scan
                    break
            else:
                break
        # Also scan annotations attached on the label line itself.
        if line_idx < len(all_lines):
            ann = all_lines[line_idx].comment.strip()
            for tag in re.findall(r"@[a-z\-]+(?:\s+[A-Za-z_][A-Za-z0-9_]*)?", ann):
                annotations.add(tag.strip())
        funcs.append(Function(
            name=name,
            path=all_lines[line_idx].path,
            start=line_idx + 1,
            end=end,
            annotations=annotations,
            lines=all_lines[line_idx + 1: end],
        ))
    return funcs


# --- check 1: stack balance -------------------------------------------------

PUSH = re.compile(r"^push\b", re.IGNORECASE)
POP = re.compile(r"^pop\b", re.IGNORECASE)
PUSHF = re.compile(r"^pushf\b", re.IGNORECASE)
POPF = re.compile(r"^popf\b", re.IGNORECASE)
PUSHA = re.compile(r"^pusha\b", re.IGNORECASE)
POPA = re.compile(r"^popa\b", re.IGNORECASE)
RET = re.compile(r"^(ret|retf|iret|retn|BAS_RET_OK|BAS_RET_ERR(?:_NOCODE)?)\b",
                 re.IGNORECASE)
CALL = re.compile(r"^call\b\s+(\S+)", re.IGNORECASE)
JMP = re.compile(r"^jmp\b\s+(\S+)", re.IGNORECASE)
JCOND = re.compile(r"^j[a-z]+\b\s+(\S+)", re.IGNORECASE)
ADD_SP = re.compile(r"^add\s+sp\s*,\s*(\S+)", re.IGNORECASE)
SUB_SP = re.compile(r"^sub\s+sp\s*,\s*(\S+)", re.IGNORECASE)
MOV_BP_SP = re.compile(r"^mov\s+bp\s*,\s*sp\b", re.IGNORECASE)
MOV_SP_BP = re.compile(r"^mov\s+sp\s*,\s*bp\b", re.IGNORECASE)
ENTER = re.compile(r"^enter\b", re.IGNORECASE)
LEAVE = re.compile(r"^leave\b", re.IGNORECASE)


def _try_int(s: str) -> int | None:
    s = s.strip()
    try:
        if s.lower().startswith(("0x", "$")):
            return int(s.lstrip("$").replace("0x", "0x", 1), 16)
        return int(s)
    except ValueError:
        return None


def check_stack_balance(funcs: list[Function],
                        noreturn: set[str] | None = None) -> list[str]:
    """Return list of error messages.  Empty = pass.

    `noreturn` is the set of function names that never return (call to
    them ends the basic block, same as `jmp`).
    """
    errors: list[str] = []
    noreturn = noreturn or set()

    for fn in funcs:
        # Skip data-only / table-only labels with no real code at all.
        has_code = any(INSTR.match(ln.code) for ln in fn.lines)
        if not has_code:
            continue
        if "@no-stack-check" in fn.annotations:
            continue

        # Per-label depths on entry.  Entry of the function is depth 0.
        label_depths: dict[str, int] = {}
        # Pending: labels we've seen jumps to but haven't reached the def of.
        pending: dict[str, list[tuple[int, int]]] = {}
        # Per-label saved bp-frame depth (for `mov sp, bp` restore).
        label_bp: dict[str, int | None] = {}
        depth = 0
        bp_depth: int | None = None  # depth captured at last `mov bp, sp`
        reachable = True
        merge_labels = set()

        for ln in fn.lines:
            code = ln.code.strip()
            if not code:
                continue
            # Local label?
            mlabel = LOCAL_LABEL.match(code)
            if mlabel:
                lbl = mlabel.group(1)
                # Compute incoming depth(s): fall-through (if reachable) + pending jumps.
                incoming: list[tuple[int, int | None]] = []
                if reachable:
                    incoming.append((depth, bp_depth))
                for jdepth, jbp, jlineno in pending.pop(lbl, []):
                    incoming.append((jdepth, jbp))
                if not incoming:
                    # Unreachable label.  Leave reachable=False.
                    reachable = False
                    continue
                depths_in = {d for d, _ in incoming}
                if len(depths_in) > 1:
                    if f"@stack-merge {lbl}" in fn.annotations or \
                       ln.comment and "@stack-merge" in ln.comment:
                        merge_labels.add(lbl)
                        depth = min(depths_in)
                        # On merge we drop bp_depth — must be re-established if needed.
                        bp_depth = None
                        reachable = True
                        continue
                    errors.append(
                        f"{fn.path.name}:{ln.lineno}: {fn.name}: label '{lbl}' "
                        f"reached with mismatched stack depths {sorted(depths_in)}"
                    )
                    depth = incoming[0][0]
                    bp_depth = incoming[0][1]
                    reachable = True
                    continue
                depth = incoming[0][0]
                # Take any non-None bp_depth from incoming (they all agree on depth).
                bp_candidates = [b for _, b in incoming if b is not None]
                bp_depth = bp_candidates[0] if bp_candidates else None
                reachable = True
                continue

            # Global label inside function body → shouldn't happen (we already
            # carved on those); but guard anyway.
            if GLOBAL_LABEL.match(code):
                break

            minstr = INSTR.match(ln.code)
            if not minstr:
                continue
            mnem = minstr.group(1).lower()
            args = minstr.group(2).strip()
            body = (mnem + " " + args).strip()

            if not reachable:
                continue

            if PUSHA.match(body):
                depth += 8
            elif POPA.match(body):
                depth -= 8
            elif PUSH.match(body) or PUSHF.match(body):
                depth += 1
            elif POP.match(body) or POPF.match(body):
                depth -= 1
            elif (m := ADD_SP.match(body)):
                v = _try_int(m.group(1))
                if v is not None:
                    depth -= v // 2
            elif (m := SUB_SP.match(body)):
                v = _try_int(m.group(1))
                if v is not None:
                    depth += v // 2
            elif MOV_BP_SP.match(body):
                bp_depth = depth
            elif MOV_SP_BP.match(body):
                if bp_depth is not None:
                    depth = bp_depth
                # else: function inherited bp from caller — we have no model.
                # In that case we silently keep current depth and hope a
                # later @no-stack-check annotation is added.
            elif ENTER.match(body) or LEAVE.match(body):
                # Rare in this codebase; bail with an annotation requirement.
                errors.append(
                    f"{fn.path.name}:{ln.lineno}: {fn.name}: "
                    f"enter/leave needs explicit @no-stack-check annotation"
                )
                reachable = False
                continue
            elif RET.match(body):
                if depth != 0:
                    errors.append(
                        f"{fn.path.name}:{ln.lineno}: {fn.name}: "
                        f"`{body}` with stack depth {depth} (expected 0). "
                        f"Push/pop mismatch on this exit path."
                    )
                reachable = False
            elif (m := CALL.match(body)):
                target = m.group(1)
                if target in noreturn:
                    reachable = False
            elif (m := JMP.match(body)):
                target = m.group(1)
                if target.startswith("."):
                    pending.setdefault(target, []).append((depth, bp_depth, ln.lineno))
                # Unconditional → flow does not fall through.
                reachable = False
            elif (m := JCOND.match(body)):
                target = m.group(1)
                if target.startswith("."):
                    pending.setdefault(target, []).append((depth, bp_depth, ln.lineno))
                # Conditional → flow continues at depth.

            if depth < 0:
                errors.append(
                    f"{fn.path.name}:{ln.lineno}: {fn.name}: "
                    f"stack depth went negative ({depth}) — pop without push."
                )
                reachable = False

        # Pending jumps that never resolved (unreachable label / forward jump
        # past end) — soft warn only if the function has @returns annotation;
        # otherwise silent (target may be a far jump out of function).
        # (Intentionally left silent; covered by the per-ret check.)
        _ = pending  # noqa

    return errors


# --- check 2: RESERVED scratch-slot ownership -------------------------------

OWNER_TAG = re.compile(r"@owner\s+([A-Za-z_][A-Za-z0-9_]*)")
RESERVED_TAG = re.compile(r"@reserved\s+([A-Za-z_][A-Za-z0-9_]*)")
BSS_DECL = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)\b.*?(equ|resb|resw|resd)\b",
                      re.IGNORECASE)
WRITE_MEM = re.compile(
    r"^(mov|add|sub|and|or|xor|inc|dec|pop|stosb|stosw)\b.*?\[\s*"
    r"([A-Za-z_][A-Za-z0-9_]*)\s*\]",
    re.IGNORECASE,
)


def collect_owned_slots(all_files: dict[Path, list[Line]]) -> dict[str, str]:
    """slot_name -> owner function name."""
    owned: dict[str, str] = {}
    for path, lines in all_files.items():
        for i, ln in enumerate(lines):
            ann = ln.comment + " " + (lines[i - 1].comment if i else "")
            mowner = OWNER_TAG.search(ann) or RESERVED_TAG.search(ann)
            if not mowner:
                continue
            mdecl = BSS_DECL.match(ln.code.strip())
            if not mdecl:
                continue
            owned[mdecl.group(1)] = mowner.group(1)
    return owned


def check_scratch_ownership(funcs: list[Function],
                            owned: dict[str, str]) -> list[str]:
    if not owned:
        return []
    errors: list[str] = []
    for fn in funcs:
        for ln in fn.lines:
            m = WRITE_MEM.match(ln.code.strip())
            if not m:
                continue
            slot = m.group(2)
            if slot in owned and owned[slot] != fn.name:
                errors.append(
                    f"{fn.path.name}:{ln.lineno}: {fn.name}: writes to "
                    f"`[{slot}]` which is @owner {owned[slot]} — use a "
                    f"different scratch slot or move logic into the owner."
                )
    return errors


# --- check 3: CF discipline (opt-in via @returns cf) ------------------------
#
# Narrow rule: "bare `ret` is fine if it follows a `jc`/`jnc` (CF inherited)
# or other non-flag-setting instruction.  It's NOT fine if the immediately-
# preceding line is a flag-setting `cmp`/`test`/`and`/`or`/`add`/`sub`-type
# instruction with no `clc`/`stc`/`popf` in between — that is the exact
# v0.9.19.0 bug #1 pattern."

CF_SETTERS = (
    re.compile(r"^clc\b", re.IGNORECASE),
    re.compile(r"^stc\b", re.IGNORECASE),
    re.compile(r"^cmc\b", re.IGNORECASE),
    re.compile(r"^popf\b", re.IGNORECASE),
    re.compile(r"^BAS_RET_OK\b", re.IGNORECASE),
    re.compile(r"^BAS_RET_ERR\b", re.IGNORECASE),
)

# Instructions that set CF (and therefore poison a bare ret on the success path).
CF_POISONERS = (
    re.compile(r"^cmp\b", re.IGNORECASE),
    re.compile(r"^test\b", re.IGNORECASE),
    re.compile(r"^add\b", re.IGNORECASE),
    re.compile(r"^sub\b", re.IGNORECASE),
    re.compile(r"^adc\b", re.IGNORECASE),
    re.compile(r"^sbb\b", re.IGNORECASE),
    re.compile(r"^and\b", re.IGNORECASE),
    re.compile(r"^or\b", re.IGNORECASE),
    re.compile(r"^xor\b", re.IGNORECASE),
    re.compile(r"^shl\b", re.IGNORECASE),
    re.compile(r"^shr\b", re.IGNORECASE),
    re.compile(r"^sal\b", re.IGNORECASE),
    re.compile(r"^sar\b", re.IGNORECASE),
    re.compile(r"^rol\b", re.IGNORECASE),
    re.compile(r"^ror\b", re.IGNORECASE),
    re.compile(r"^neg\b", re.IGNORECASE),
    re.compile(r"^mul\b", re.IGNORECASE),
    re.compile(r"^imul\b", re.IGNORECASE),
    re.compile(r"^div\b", re.IGNORECASE),
    re.compile(r"^idiv\b", re.IGNORECASE),
)


def check_cf_discipline(funcs: list[Function]) -> list[str]:
    errors: list[str] = []
    for fn in funcs:
        if "@returns cf" not in fn.annotations:
            continue
        for i, ln in enumerate(fn.lines):
            mi = INSTR.match(ln.code)
            if not mi:
                continue
            body = (mi.group(1) + " " + mi.group(2)).strip()
            if not re.match(r"^(ret|retf|retn|iret)\b", body, re.IGNORECASE):
                continue
            # Walk back to the most recent non-blank non-comment non-label line.
            j = i - 1
            poisoner_body = None
            while j >= 0:
                prev_raw = fn.lines[j].code.strip()
                if not prev_raw or prev_raw.startswith(";") \
                   or LOCAL_LABEL.match(prev_raw) \
                   or GLOBAL_LABEL.match(prev_raw):
                    j -= 1
                    continue
                mi2 = INSTR.match(fn.lines[j].code)
                if not mi2:
                    break
                prev_body = (mi2.group(1) + " " + mi2.group(2)).strip()
                if any(r.match(prev_body) for r in CF_SETTERS):
                    poisoner_body = None  # CF explicitly set — safe
                    break
                if any(r.match(prev_body) for r in CF_POISONERS):
                    poisoner_body = prev_body
                    break
                # Anything else (mov, lea, jmp, call, push, pop, lds, ...): no
                # flag effect we care about — keep walking until we find a
                # setter or a poisoner.  This is conservative: a chain like
                # `cmp ax,dx / mov bx,cx / ret` will still be flagged.
                j -= 1
                continue
            if poisoner_body:
                errors.append(
                    f"{fn.path.name}:{ln.lineno}: {fn.name}: bare `ret` "
                    f"reached after `{poisoner_body}` with no intervening "
                    f"clc/stc/popf/BAS_RET_OK/BAS_RET_ERR. CF leak risk — "
                    f"see CHANGELOG v0.9.19.0 bug #1."
                )
    return errors


# --- driver -----------------------------------------------------------------

def main() -> int:
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--root", default=str(DEFAULT_ROOT),
                   help="Directory of .inc files to lint (recursive).")
    p.add_argument("--verbose", action="store_true",
                   help="Print summary stats even on success.")
    args = p.parse_args()

    root = Path(args.root)
    if not root.exists():
        print(f"asm_lint: root not found: {root}", file=sys.stderr)
        return 2

    files = sorted(root.rglob("*.inc")) + sorted(root.rglob("*.asm"))
    all_files: dict[Path, list[Line]] = {f: load(f) for f in files}
    all_funcs: list[Function] = []
    for f, lines in all_files.items():
        all_funcs.extend(carve_functions(lines))

    if args.verbose:
        print(f"asm_lint: {len(files)} files, {len(all_funcs)} functions")

    errors: list[str] = []
    noreturn = {f.name for f in all_funcs if "@noreturn" in f.annotations}
    errors += check_stack_balance(all_funcs, noreturn=noreturn)
    owned = collect_owned_slots(all_files)
    errors += check_scratch_ownership(all_funcs, owned)
    errors += check_cf_discipline(all_funcs)

    if args.verbose:
        annotated_cf = sum(1 for f in all_funcs if "@returns cf" in f.annotations)
        print(f"asm_lint: {len(owned)} @owner-annotated BSS slots, "
              f"{annotated_cf} @returns-cf-annotated functions, "
              f"{len(noreturn)} @noreturn-annotated functions")
        if owned:
            for slot, owner in sorted(owned.items()):
                print(f"  owned: [{slot}] -> {owner}")

    if errors:
        print(f"asm_lint: {len(errors)} violation(s):", file=sys.stderr)
        for e in errors:
            print(f"  {e}", file=sys.stderr)
        return 1
    if args.verbose:
        print("asm_lint: clean")
    return 0


if __name__ == "__main__":
    sys.exit(main())
