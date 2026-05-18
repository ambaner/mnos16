# Program Loader Design Document

## Version: v0.9.6 (Implemented)

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
6. Keep implementation simple — flat binary, no relocation

---

## 3. Memory Layout

### 3.1 Memory Layout (v0.9.6)

```
0x0000 ─────── IVT + BDA
0x0600 ─────── Boot Info Block (16 bytes)
0x0800 ─────── FS.SYS (2 sectors, 1 KB)
0x2800 ─────── MM.SYS (2 KB max)
0x3000 ─────── SHELL.SYS (16 sectors, 8 KB)
0x5000 ─────── KERNEL.SYS (8 sectors, 4 KB)
0x7000 ─────── (gap)
0x7C00 ─────── Stack (grows down from here)
0x7FFC ─────── SHELL_ARGS_PTR (2 bytes, ABI slot)
0x7FFE ─────── SHELL_SAVED_SP (2 bytes, ABI slot)
0x8000 ─────── USER_PROG_BASE — Transient Program Area (TPA)
  ...           (program code + data + BSS)
0xF7FF ─────── USER_PROG_END — end of TPA
0xF800 ─────── End of usable conventional memory
```

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

User executables use the standard MNEX header format with `'MNEX'` magic:

```
Offset  Size  Field         Description
──────  ────  ──────────    ─────────────────────────────────────────
0x00    4     magic         'MNEX' — identifies as user executable
0x04    2     size_sectors  File size in 512-byte sectors
0x06    ...   code          Entry point — execution begins here
```

### 4.1 Program Requirements

- Assembled with `[ORG USER_PROG_BASE]` (currently 0x8000)
- Entry point at offset 6 (immediately after the 6-byte header)
- Must return to caller via `ret` or invoke `SYS_EXIT` (INT 0x80, AH=0x23)
- May use INT 0x80 (kernel), INT 0x81 (filesystem), INT 0x82 (memory manager)
- Must not write below USER_PROG_BASE or above 0xF7FF
- Must preserve SS:SP (stack is shared with shell)

### 4.2 Minimal Example: HELLO.MNX

```nasm
; HELLO.MNX — minimal user program
; Prints "Hello, world!" and returns to shell
;
%include "memory.inc"
[ORG USER_PROG_BASE]
[BITS 16]

; --- MNEX Header (6 bytes) ---
hello_magic     db 'MNEX'           ; Magic identifier
hello_sectors   dw 1                ; Size in sectors

; --- Entry point (offset 6) ---
entry:
    mov si, msg_hello
    mov ah, 0x01                    ; SYS_PRINT_STRING
    int 0x80
    ret                             ; Return to shell

msg_hello   db 'Hello, world!', 13, 10, 0

; Pad to sector boundary
times 512 - ($ - $$) db 0
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
│ 7. Save shell state                     │
│    Save SP to shell_saved_sp            │
│    (for SYS_EXIT recovery)              │
└─────────────────────────────────────────┘
     │
     ▼
┌─────────────────────────────────────────┐
│ 8. Execute program                      │
│    call USER_PROG_BASE + 6              │
│    (near call — return addr on stack)   │
└─────────────────────────────────────────┘
     │
     ▼
┌─────────────────────────────────────────┐
│ 9. Program returns (ret or SYS_EXIT)    │
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

---

## 7. Build Pipeline Changes

### 7.1 New Source Directory

```
src/programs/
├── hello.asm       # "Hello, world!" demo program
└── (future programs)
```

### 7.2 Build Script Updates (`build.ps1`)

Add a user programs assembly phase:

```powershell
# --- User programs ---
$programsDir = Join-Path $SrcDir "programs"
if (Test-Path $programsDir) {
    $programs = Get-ChildItem $programsDir -Filter "*.asm"
    foreach ($prog in $programs) {
        $outName = [System.IO.Path]::GetFileNameWithoutExtension($prog.Name) + ".mnx"
        $outPath = Join-Path $OutputDir $outName
        & $NasmPath -f bin -I "$IncludeDir/" -o $outPath $prog.FullName
        # Validate: must be multiple of 512 bytes
    }
}
```

### 7.3 Disk Image Updates (`create-disk.ps1`)

Add user programs to the MNFS directory with `ATTR_EXEC`:

```powershell
@{ Name = 'HELLO   MNX'; Attr = $MNFS_ATTR_EXEC; Bytes = $helloBytes }
```

---

## 8. Implementation Summary

All phases were completed for v0.9.6.

### Phase 1: Memory Layout + Heap Resize ✓
- `USER_PROG_*` constants in `memory.inc`
- `mm_init` uses 0x8FFF as heap end (4 KB heap)
- Shell `mem` command shows TPA in memory map

### Phase 2: Shell Program Execution ✓
- Implicit execution via shell unknown-command handler
- Filename parsing with auto-append MNX (no extension required)
- `FS_FIND_BASE` syscall for base-name-only file search
- Attribute checks (no SYSTEM, has EXEC), size check, MNEX magic validation
- Near `call` to `USER_PROG_BASE + 6` with SP save/restore for SYS_EXIT

### Phase 3: New Syscalls ✓
- SYS_EXIT (AH=0x23) — terminate program, restore shell SP
- SYS_GET_ARGS (AH=0x24) — return pointer to command-line arguments

### Phase 4: HELLO.MNX Demo Program ✓
- `src/programs/hello.asm` — prints "Hello, world!" and returns
- `build.ps1` assembles user programs, `create-disk.ps1` includes them

### Phase 5: Documentation + Release ✓
- PROGRAM-LOADER.md, README.md, CHANGELOG.md, SYSTEM-CALLS.md updated
- Version bumped to v0.9.6

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

### 9.6 Why ORG USER_PROG_BASE?

Programs must know their load address for absolute references (strings,
jump tables).  `[ORG USER_PROG_BASE]` tells NASM to generate addresses
relative to the TPA base (0x8000).  This mirrors DOS `.COM` files using
`[ORG 0x0100]`.

---

## 10. Future Enhancements

- **User heap within TPA** — programs get their own heap above their BSS
- **Program arguments via PSP** — DOS-style Program Segment Prefix
- **Multiple program execution** — `A.MNX && B.MNX` chaining
- **Background programs** — TSR-style (Terminate and Stay Resident)
- **Return code checking** — shell `%ERRORLEVEL%` equivalent
- **Relocation support** — load programs at arbitrary addresses
- **Stack isolation** — separate stack for user programs (prevents corruption)

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

1. **HELLO.MNX** — basic load + print + return
2. **Run nonexistent file** — "Bad command or file name" error
3. **Run KERNEL.SYS** — "System module, cannot run in user mode"
4. **Run file > 30 KB** — "Program too large" error
5. **Run corrupt .MNX** — "Invalid program header" (bad magic)
6. **Program using SYS_EXIT** — clean return from nested call
7. **Program using SYS_GET_ARGS** — verify argument passing
8. **Run after run** — verify TPA is reusable (no state leak)
9. **`mem` command** — verify reduced heap and updated memory map
