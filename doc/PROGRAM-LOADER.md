# Program Loader Design Document

## Version: v0.9.17 (Implemented)

## 1. Overview

The program loader allows users to run external programs from the shell prompt.
Programs are `.MNX` files stored on disk, loaded on demand into the Transient
Program Area (TPA), executed, and then discarded when they return control to
the shell.

This is the mini-os equivalent of DOS's `COMMAND.COM` loading `.COM` files.

---

## 2. Goals

1. Load and execute user programs from the shell prompt (implicit execution)
2. Isolate user programs from system memory (best-effort, no hardware MMU)
3. Provide syscall interface for user programs (INT 0x80, 0x81, 0x82)
4. Allow programs to return cleanly to the shell
5. Validate programs before execution (multi-layer protection)
6. Support relocatable binaries — programs assembled at ORG 0, patched at load time

---

## 3. Memory Layout

### 3.1 Memory Layout (v0.9.14)

```
0x0000 ─────── IVT + BDA
0x0600 ─────── Boot Info Block (BIB, 16 bytes)
0x0800 ─────── MODULE_FIRST_BASE — System modules packed sequentially:
               ├── FS.SYS   (5 sectors, placed first — always at 0x0800)
               ├── MM.SYS   (2 sectors, placed immediately after FS)
               └── SHELL.SYS (14 sectors, placed immediately after MM)
               (Positions are dynamic — determined at boot by kernel)
0x5000 ─────── KERNEL.SYS (fixed ORG, loaded by LOADER)
0x6C00 ─────── Stack canary zone
0x7C00 ─────── Stack (grows down from here)
0x7FFC ─────── SHELL_ARGS_PTR (2 bytes, ABI slot)
0x7FFE ─────── SHELL_SAVED_SP (2 bytes, ABI slot)
0x8000 ─────── USER_PROG_BASE — Transient Program Area (TPA)
  ...           (program code + data + BSS, relocatable)
0xF7FF ─────── USER_PROG_END — end of TPA
0xF800 ─────── End of usable conventional memory
```

**Note**: Module positions below 0x5000 are no longer hardcoded.  The kernel
loads modules sequentially starting at MODULE_FIRST_BASE (0x0800) and uses
the v2 relocation table to patch absolute addresses at load time.  See
`doc/ABI.md` for the binary format details.

### 3.2 Key Constants

```nasm
USER_PROG_BASE  equ 0x8000      ; Linear address where programs are loaded
USER_PROG_END   equ 0xF7FF      ; Last usable byte in TPA
USER_PROG_MAX   equ 0x7800      ; Maximum program size (30 KB)
```

### 3.3 Heap in HMA (no conventional heap)

The heap resides entirely in the High Memory Area (segment 0xFFFF, offsets
0x0010–0xFF00, ~64 KB).  The old conventional heap at 0x8000 is eliminated,
allowing the TPA to start at 0x8000 instead of 0x9000:
- **TPA expanded**: 26 KB → 30 KB
- **Heap**: ~64 KB in HMA (requires A20, enabled at boot)
- **A20 failure**: Heap disabled (size=0), allocations fail with CF set

---

## 4. MNX File Format

User executables use the **MNEX v2 header format** (v0.9.14+) with `'MNEX'` magic.
Programs are assembled with `[ORG 0]` and relocated at load time by the shell.

### 4.0 v2 Header (Relocatable — current)

```
Offset  Size  Field         Description
──────  ────  ──────────    ─────────────────────────────────────────
0x00    4     magic         'MNEX' — identifies as user executable
0x04    2     size_sectors  File size in 512-byte sectors
0x06    2     flags         Bit 0 (MNEX_V2_FLAG_RELOC): has relocation table
0x08    2     reloc_count   Number of 16-bit relocation entries
0x0A    2     entry_offset  Code entry point (offset from load base)
0x0C    N×2   reloc_table   Array of file-relative offsets to patch
...           code/data     Binary content (assembled at ORG 0)
```

The shell detects v2 format by checking `flags & 0x0001`.  If set:
1. Reads `reloc_count` entries from `reloc_table`
2. For each entry, adds `USER_PROG_BASE` to the 16-bit word at that offset
3. Computes entry: `USER_PROG_BASE + entry_offset`
4. Calls the program via memory-indirect: `call [run_entry_addr]`

### 4.1 Legacy v1 Header (Deprecated)

```
Offset  Size  Field         Description
──────  ────  ──────────    ─────────────────────────────────────────
0x00    4     magic         'MNEX' — identifies as user executable
0x04    2     size_sectors  File size in 512-byte sectors
0x06    ...   code          Entry point — execution begins here
```

v1 binaries are detected via a **two-stage check**:
1. If `flags & 0x0001 == 0` → definitely v1 (flag check)
2. If flag is set, verify `entry_offset == 12 + reloc_count * 2` (secondary
   validation).  If this fails → treat as v1 (the first opcode had bit 0 set
   by coincidence, e.g., `jmp short` 0xEB, `push bp` 0x55, `ret` 0xC3).

The shell falls back to the legacy entry at offset 6 (`call USER_PROG_BASE + 6`).
v1 binaries must be assembled with `[ORG USER_PROG_BASE]`.

**Note**: All current programs use v2 format.  v1 support is retained for
backward compatibility with third-party binaries that predate v0.9.14.

### 4.2 Program Requirements (v2)

- Assembled with `[ORG 0]` (zero-based, relocatable)
- Built using `gen_relocs.py` + `pack_module.py` toolchain
- Entry point specified by `entry_offset` field (typically = header + reloc table size)
- Must return to caller via `ret` or invoke `SYS_EXIT` (INT 0x80, AH=0x23)
- May use INT 0x80 (kernel), INT 0x81 (filesystem), INT 0x82 (memory manager)
- Must not write below USER_PROG_BASE or above 0xF7FF
- Must preserve SS:SP (stack is shared with shell)
- Must not assume any particular load address — all absolute references are patched

### 4.3 Minimal Example: HELLO.MNX (v2)

```nasm
; HELLO.MNX — minimal user program (v2 relocatable)
; Assembled with ORG 0, header generated by pack_module.py
;
%ifndef RELOC_BASE
%define RELOC_BASE 0
%endif
%include "memory.inc"
[ORG RELOC_BASE]
[BITS 16]

; No inline header — generated by pack_module.py toolchain

entry:
    mov si, msg_hello
    mov ah, 0x01                    ; SYS_PRINT_STRING
    int 0x80
    ret                             ; Return to shell

msg_hello   db 'Hello, world!', 13, 10, 0
```

Build command:
```
gen_relocs.py hello.asm --nasm tools/nasm/nasm.exe --header-size 0 -o hello.rel
pack_module.py hello.bin hello.rel --magic MNEX --pad-sectors -o HELLO.MNX
```

---

## 5. Shell Program Execution

### 5.1 Syntax

Programs are executed by typing their name directly at the shell prompt.
The `.MNX` extension is optional — the shell auto-appends it when no
extension is provided.  If no `.MNX` file is found, the shell searches
by base name using `FS_FIND_BASE` and checks the file's attributes.

```
mnos:\> HELLO.MNX
mnos:\> HELLO
mnos:\> HELLO arg1 arg2
```

Any unrecognized command is treated as an implicit program execution
attempt — there is no separate `run` command.

### 5.2 Execution Flow

```
User types: HELLO
     │
     ▼
┌─────────────────────────────────────────┐
│ 1. Parse filename from command line     │
│    Convert to 8.3 uppercase padded form │
│    Auto-append MNX if no extension      │
│    e.g., "HELLO" → "HELLO   MNX"       │
└─────────────────────────────────────────┘
     │
     ▼
┌─────────────────────────────────────────┐
│ 2. File search                          │
│    If extension provided: FS_FIND_FILE  │
│    If no extension: FS_FIND_BASE        │
│       (searches by 8-byte name only)    │
│    → fail? "Bad command or file name"   │
└─────────────────────────────────────────┘
     │
     ▼
┌─────────────────────────────────────────┐
│ 3. Attribute validation                 │
│    a) ATTR_SYSTEM must NOT be set       │
│       → fail? "System module, cannot    │
│         run in user mode"               │
│    b) ATTR_EXEC must be set             │
│       → fail? "Not executable           │
│         (.mnx required)"                │
└─────────────────────────────────────────┘
     │
     ▼
┌─────────────────────────────────────────┐
│ 4. Size check                           │
│    File size ≤ USER_PROG_MAX (30 KB)    │
│    → fail? "Program too large"          │
└─────────────────────────────────────────┘
     │
     ▼
┌─────────────────────────────────────────┐
│ 5. Load file to USER_PROG_BASE (0x8000) │
│    via INT 0x81 FS_READ_FILE            │
│    → fail? "Load error"                 │
└─────────────────────────────────────────┘
     │
     ▼
┌─────────────────────────────────────────┐
│ 6. Magic validation                     │
│    Check [0x8000] == 'MNEX' (4 bytes)   │
│    → fail? "Invalid program header"     │
└─────────────────────────────────────────┘
     │
     ▼
┌─────────────────────────────────────────┐
│ 7. Relocation (v2 binaries)            │
│    If flags & 0x0001 (MNEX_V2_FLAG):   │
│    - Read reloc_count entries           │
│    - For each: add USER_PROG_BASE to   │
│      the 16-bit word at that offset    │
│    - Compute entry_addr from header    │
│    If not set: legacy v1 (entry at +6) │
└─────────────────────────────────────────┘
     │
     ▼
┌─────────────────────────────────────────┐
│ 8. Save shell state                     │
│    Save SP to shell_saved_sp            │
│    (for SYS_EXIT recovery)              │
└─────────────────────────────────────────┘
     │
     ▼
┌─────────────────────────────────────────┐
│ 9. Execute program                      │
│    v2: call [run_entry_addr]            │
│        (memory-indirect near call)      │
│    v1: call USER_PROG_BASE + 6          │
│        (direct near call — legacy)      │
└─────────────────────────────────────────┘
     │
     ▼
┌─────────────────────────────────────────┐
│ 10. Program returns (ret or SYS_EXIT)  │
│     Shell regains control               │
│     Print newline, back to prompt       │
└─────────────────────────────────────────┘
```

### 5.3 Four-Layer Protection

Programs must pass ALL checks before execution:

| Layer | Check | Rejects |
|-------|-------|---------|
| 1. File search | File must exist on disk | Typos, nonexistent commands |
| 2. Attribute | `ATTR_SYSTEM` not set | KERNEL.SYS, FS.SYS, MM.SYS, SHELL.SYS |
| 3. Attribute | `ATTR_EXEC` is set | Data files (README.TXT, etc.) |
| 4. Magic | First 4 bytes = `'MNEX'` | Corrupt/truncated/wrong format files |

### 5.4 Error Messages

```
Bad command or file not found
Not executable (.mnx required)
System module, cannot run in user mode
Program too large (30 KB max)
Load error
Invalid program header
```

---

## 6. New Syscalls

### 6.1 SYS_EXIT (INT 0x80, AH=0x23)

Terminates the running program and returns control to the shell immediately,
regardless of call depth.  Used when a program cannot simply `ret` (e.g.,
from a nested function call or after an error).

```
Input:   AH = 0x23
         AL = exit code (0 = success, nonzero = error) [reserved for future use]
Output:  Does not return — jumps directly to shell
```

**Implementation**: CLI → restore DS/ES/SS to 0 → restore SP from
`[SHELL_SAVED_SP]` → STI → `ret` to return to the shell's post-call code.

```nasm
sys_exit:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, [SHELL_SAVED_SP]    ; Restore shell's stack pointer
    sti
    ret                         ; Return to shell (after the call instruction)
```

**Note**: Syscall numbers 0x20–0x22 are occupied by SYS_DBG_PRINT, SYS_DBG_HEX16,
and SYS_DBG_REGS (debug syscalls added in v0.9.4). SYS_EXIT was assigned 0x23.

### 6.2 SYS_GET_ARGS (INT 0x80, AH=0x24)

Returns a pointer to the command-line arguments string (everything after
the filename on the command line).

```
Input:   AH = 0x24
Output:  SI = pointer to null-terminated argument string
         CX = length of argument string (0 if no args)
         CF = clear (always succeeds)
```

**Example**: If user typed `HELLO foo bar`, SI points to `"foo bar"`.

The shell stores the argument pointer at `[SHELL_ARGS_PTR]` (0x7FFC) before
executing the program. SYS_GET_ARGS reads this pointer and calculates the
string length.

### 6.3 SYS_EXEC (INT 0x80, AH=0x27)

Replaces the running program with a new .MNX program (overlay exec).  The
caller is destroyed and the new program takes over the TPA.  On success,
the new program's `ret` returns to the shell — not to the original caller.

```
Input:   AH = 0x27
         DS:SI = 11-byte filename (8.3 padded uppercase)
         DS:DI = pointer to NUL-terminated argument string (or 0 for no args)
Output:  Does not return on success
         On failure: CF = 1, AX = error code:
           1 = file not found
           2 = file is system / not executable
           3 = file too large
           4 = read error
           5 = bad MNEX header
```

**Safety guarantee**: On failure (error codes 1–3), the caller's TPA is
completely intact.  The kernel validates everything before loading.  Errors
4–5 occur after the TPA is overwritten; in these cases, control returns to
the shell with an error message (not to the caller).

**Implementation details**:
1. Copy filename (11 bytes) and args (up to 127 chars) to kernel scratch
2. Validate: find file, check attrs (not SYSTEM, has EXEC), check size
3. Load file to USER_PROG_BASE via INT 0x81 FS_READ_FILE
4. Validate MNEX magic at load address
5. Apply v2 relocations (same algorithm as shell's program loader)
6. Set SHELL_ARGS_PTR to kernel's args copy, parse into ARGV table
7. Restore SP from SHELL_SAVED_SP, clear direction flag, jump to entry

```nasm
; SYS_EXEC — program overlay execution
; Kernel scratch: exec_fname_buf (11 bytes), exec_args_buf (128 bytes)
mov ah, 0x27
mov si, my_filename    ; 11-byte "EDIT    MNX"
mov di, my_args        ; "file.txt" or 0
int 0x80
; Only reaches here on failure (CF=1, AX=error code)
```

### 6.4 SYS_SPAWN (INT 0x80, AH=0x28)

Spawns a child program in the TPA.  When the child exits (via `ret` or
SYS_EXIT), the kernel reloads the **caller** from disk and restarts it fresh.
This enables monitor/debugger patterns where a parent program survives across
child execution without requiring multiple TPA slots.

```
Input:   AH = 0x28
         DS:SI = child's 11-byte filename (8.3 padded uppercase)
         DS:DI = child's argument string (or 0)
         DS:BX = caller's own 11-byte filename (for reload after child exits)
Output:  Does not return on success
         On failure: CF = 1, AX = error code (same as SYS_EXEC)
```

**Semantics**:
1. Kernel pushes BX (parent filename) onto `spawn_parent_stack[depth]` and
   increments `spawn_depth`.
2. If this is the **outermost** spawn (depth was 0): saves the shell return
   address from `[SHELL_SAVED_SP]` to `spawn_saved_ret` and overwrites it
   with a trampoline (`mov ah, SYS_EXIT; int 0x80`).  Nested spawns skip
   this step — the trampoline is already installed.
3. Executes the child identically to SYS_EXEC (same load/relocate/jump).
4. When child exits (via `ret` → trampoline → SYS_EXIT, or direct SYS_EXIT):
   - SYS_EXIT decrements `spawn_depth` and reloads `spawn_parent_stack[depth]`.
   - If depth reaches 0 (outermost parent): restores `spawn_saved_ret` to
     the stack and clears the trampoline.  Parent's `ret` returns to shell.
   - If still nested: re-installs the trampoline for the next unwind level.

**Nesting**: Up to `SPAWN_MAX_DEPTH` (4) levels of nested spawn are supported.
Each level adds 11 bytes to the parent stack (e.g., mnmon → mnmon → edit).
Exceeding the limit returns CF=1, AX=4.

**Parent state**: Not preserved.  The parent restarts fresh (entry point,
clean registers).  Any state the parent needs across spawns must be saved
to a file or passed via args.

**Error handling**: Same as SYS_EXEC.  Pre-load failures (file not found,
not executable, too large) invoke `spawn_rollback_if_pending` which undoes
the depth increment and trampoline installation, allowing the caller to
continue.  If the parent cannot be reloaded after child exit (file missing,
disk error), control returns to the shell.

```nasm
; SYS_SPAWN — spawn child, return to parent on exit
; MNMON pattern: launch program, get reloaded when it finishes
mov si, child_fname    ; 11-byte "HELLO   MNX"
mov di, child_args     ; args string or 0
mov bx, my_own_fname   ; 11-byte "MNMON   MNX"
mov ah, 0x28
int 0x80
; Only reaches here on failure (CF=1, AX=error code)
```

---

## 7. Build Pipeline Changes

### 7.1 New Source Directory

```
src/programs/
├── hello.asm       # "Hello, world!" demo program
└── (future programs)
```

### 7.2 Build Script Updates (`build.ps1`)

User programs are built as relocatable v2 binaries using the `Build-RelocModule`
function (same toolchain as system modules):

```powershell
# --- User programs (relocatable v2) ---
Build-RelocModule -Source "src/programs/sysinfo/sysinfo.asm" `
                  -Output "output/SYSINFO.MNX" `
                  -Magic "MNEX"

Build-RelocModule -Source "src/programs/mnmon.asm" `
                  -Output "output/MNMON.MNX" `
                  -Magic "MNEX"

Build-RelocModule -Source "src/programs/edit/edit.asm" `
                  -Output "output/EDIT.MNX" `
                  -Magic "MNEX"
```

The `Build-RelocModule` function:
1. Assembles at ORG 0 (raw binary, no header)
2. Runs `gen_relocs.py` (delta comparison at ORG 0 vs ORG 0x100)
3. Runs `pack_module.py` (pre-biases relocations, constructs v2 header, pads to sector boundary)

### 7.3 Disk Image Updates (`create-disk.ps1`)

Add user programs to the MNFS directory with `ATTR_EXEC`:

```powershell
@{ Name = 'HELLO   MNX'; Attr = $MNFS_ATTR_EXEC; Bytes = $helloBytes }
```

---

## 8. Implementation Summary

All phases were completed for v0.9.6, with major updates in v0.9.14.

### Phase 1: Memory Layout + Heap Resize ✓ (v0.9.6)
- `USER_PROG_*` constants in `memory.inc`
- Heap moved to HMA (64 KB), TPA expanded to 30 KB
- Shell `mem` command shows TPA in memory map

### Phase 2: Shell Program Execution ✓ (v0.9.6, updated v0.9.14)
- Implicit execution via shell unknown-command handler
- Filename parsing with auto-append MNX (no extension required)
- `FS_FIND_BASE` syscall for base-name-only file search
- Attribute checks (no SYSTEM, has EXEC), size check, MNEX magic validation
- **v0.9.14**: v2 relocation patching before execution
- **v0.9.14**: Memory-indirect `call [run_entry_addr]` replaces direct `call`

### Phase 3: New Syscalls ✓ (v0.9.6)
- SYS_EXIT (AH=0x23) — terminate program, restore shell SP
- SYS_GET_ARGS (AH=0x24) — return pointer to command-line arguments

### Phase 4: Relocatable Programs ✓ (v0.9.14)
- All programs converted to `[ORG 0]` (SYSINFO, MNMON, EDIT)
- Built via `gen_relocs.py` + `pack_module.py` toolchain
- Shell detects v2 header, applies relocations at load time
- Entry point from header's `entry_offset` field (not fixed offset 6)
- Legacy v1 fallback preserved for backward compatibility

### Phase 5: Documentation + Release ✓
- PROGRAM-LOADER.md, ABI.md, README.md, CHANGELOG.md updated
- Version bumped to v0.9.14

---

## 9. Design Decisions

### 9.1 Why `call` instead of `jmp`?

Using `call` pushes a return address on the stack, allowing programs to
simply `ret` to return to the shell.  This is the simplest possible
convention — no special cleanup needed for well-behaved programs.

### 9.2 Why save SP for SYS_EXIT?

Programs may call nested functions.  A `ret` from deep in the call stack
would not return to the shell.  SYS_EXIT provides an escape hatch by
restoring the shell's SP and executing `ret`, effectively unwinding the
entire program stack in one step.

### 9.3 Why eliminate the conventional heap?

The heap now lives entirely in the HMA (~64 KB at segment 0xFFFF).  Since the
old 4 KB conventional heap at 0x8000 is no longer needed, that region is
reclaimed for the TPA — expanding it from 26 KB (0x9000–0xF7FF) to 30 KB
(0x8000–0xF7FF).

If A20 is non-functional (extremely rare on any modern system), the heap is
disabled entirely (size=0, all allocations return CF).  This is acceptable
because mini-os already requires A20 for its boot process.

### 9.4 Why not segment-based isolation?

x86 real mode segments could theoretically limit program access.  However:
- The shell and kernel share segment 0x0000 (flat model)
- Switching segment models mid-execution adds complexity
- Educational value of simplicity outweighs protection guarantees
- Programs are "trusted" in the same way DOS .COM files were

### 9.5 Why four protection layers?

No single check is sufficient:
- Extension alone: could be faked by renaming
- Attribute alone: could miss files not in directory (edge case)
- Magic alone: doesn't prevent running system files
- Combined: defense-in-depth, catches mistakes and corruption

### 9.6 Why relocatable binaries? (v0.9.14)

Programs are assembled with `[ORG 0]` and patched at load time.  This provides:

1. **Binary portability** — a program built for one version of MNOS16 runs on
   future versions, even if the kernel changes where things live in memory.
2. **Consistent toolchain** — both system modules and user programs use the
   same `gen_relocs.py` + `pack_module.py` pipeline.
3. **No hardcoded assumptions** — programs don't encode `0x8000` into their
   instructions; the loader patches them.  If TPA ever moves, only the loader
   changes.

The delta-comparison technique (assemble at ORG 0 vs ORG 0x100, find words
that differ by exactly 0x100) automatically discovers all absolute references
without manual annotation.

### 9.7 Why memory-indirect call for v2 programs?

The original `call USER_PROG_BASE + 6` is a relative (E8) near call.  If
the called program uses the v2 header (where the entry point varies based on
relocation table size), the old fixed-offset approach is wrong.  More subtly,
encoding a `call` to a fixed external address inside a relocatable binary
creates a false relocation hit (the E8 displacement changes with ORG).

The fix: store the computed entry address in a variable (`run_entry_addr`)
and use `call [run_entry_addr]` (FF /2 memory-indirect).  The instruction
encoding doesn't change with ORG, and it supports variable entry points.

### 9.8 Why keep v1 fallback?

Third-party programs built before v0.9.14 use the v1 format (6-byte header,
entry at offset 6, assembled with `[ORG 0x8000]`).  The shell detects v1
by checking whether `flags & 0x0001 == 0` and falls back to the direct call.
This ensures binary compatibility with older programs.

### 9.9 Why does the TPA address (0x8000) remain fixed?

Even though programs are now relocatable, USER_PROG_BASE remains at 0x8000:
- Existing programs have their relocations pre-biased for this address
- The TPA is a stable ABI guarantee (see doc/ABI.md)
- There's no benefit to moving it — the space below is needed for OS modules
- If we ever need a different load address, the relocation system makes it
  trivial without recompiling user programs

---

## 10. Future Enhancements

- **User heap within TPA** — programs get their own heap above their BSS
- **Program arguments via PSP** — DOS-style Program Segment Prefix
- ~~**Multiple program execution** — `A.MNX && B.MNX` chaining~~ Partially done: SYS_EXEC enables A→B overlay (v0.9.15)
- **Background programs** — TSR-style (Terminate and Stay Resident)
- **Return code checking** — shell `%ERRORLEVEL%` equivalent
- ~~**Relocation support** — load programs at arbitrary addresses~~ ✓ Done (v0.9.14)
- **Stack isolation** — separate stack for user programs (prevents corruption)
- **v2 header versioning** — add a format version field for future header evolution

---

## 11. Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| Program corrupts system memory | Medium | High | Best-effort: no hardware protection in real mode; document constraints |
| Heap too small after resize | Low | Medium | Current system usage < 1 KB; monitor with `mem` command |
| Stack overflow in user program | Low | High | Document stack constraints; programs share shell's stack |
| Program hangs (infinite loop) | Medium | Medium | Future: watchdog timer via PIT; current: Ctrl+Alt+Del |
| Large program exceeds TPA | Low | Low | Size check in loader prevents this |

---

## 12. Testing Strategy

1. **MNMON.MNX** — basic load + interactive commands + quit
2. **Run nonexistent file** — "Bad command or file name" error
3. **Run KERNEL.SYS** — "System module, cannot run in user mode"
4. **Run file > 30 KB** — "Program too large" error
5. **Run corrupt .MNX** — "Invalid program header" (bad magic)
6. **Program using SYS_EXIT** — clean return from nested call
7. **Program using SYS_GET_ARGS** — verify argument passing
8. **Run after run** — verify TPA is reusable (no state leak)
9. **`mem` command** — verify reduced heap and updated memory map
