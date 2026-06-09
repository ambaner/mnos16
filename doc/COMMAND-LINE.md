# Command-Line Expansions — Design Document

**Version:** 1.0
**Status:** Layer 2 implemented (v0.9.8); Layers 3–5 proposed
**Prerequisite:** v0.9.7 (program loader, SYS_GET_ARGS infrastructure)

---

## 1. Motivation

User programs today launch with no context — `mnmon` always starts at address
0x0000, `hello` prints the same message.  Command-line expansions make programs
versatile by providing input parameters, environment state, and I/O control
without requiring each program to implement its own input parsing from scratch.

**Goal:** Build a layered command-line system that evolves from simple argument
passing to full shell expansion, modeled on DOS/Unix conventions adapted for
16-bit real mode.

---

## 2. Feature Layers

Each layer builds on the previous.  They can be implemented incrementally
across multiple versions.

```
┌──────────────────────────────────────────────────────────────────────┐
│  Layer 5: I/O Redirection          (> < >> |)                        │
├──────────────────────────────────────────────────────────────────────┤
│  Layer 4: Wildcards / Globbing      (*.MNX, KERN*.SYS)               │
├──────────────────────────────────────────────────────────────────────┤
│  Layer 3: Environment Variables     (%VERSION%, %HEAP%)              │
├──────────────────────────────────────────────────────────────────────┤
│  Layer 2: Parsed Arguments          (argc/argv-style access)         │
├──────────────────────────────────────────────────────────────────────┤
│  Layer 1: Raw Argument String       (everything after program name)  │
└──────────────────────────────────────────────────────────────────────┘
```

---

## 3. Layer 1 — Raw Argument String

**Status:** Already implemented (v0.9.6)

### 3.1 How It Works Today

When the shell launches a program:
1. Shell parses the command line, identifies the program name
2. Everything after the name (with leading spaces stripped) → `run_args_ptr`
3. Pointer stored at fixed address `SHELL_ARGS_PTR` (0x7FFC)
4. Program calls `SYS_GET_ARGS` (INT 0x80, AH=0x24)
5. Returns: SI = pointer to NUL-terminated args string, CX = length

### 3.2 Current Limitations

- Programs must parse the raw string themselves
- No quoting support (spaces always delimit)
- No expansion (variables, wildcards passed literally)
- No way to know how many arguments exist without scanning

### 3.3 Example

```
mnos:\> mnmon 5000
```
Program receives: SI → "5000\0", CX = 4

```
mnos:\> hello world foo bar
```
Program receives: SI → "world foo bar\0", CX = 13

---

## 4. Layer 2 — Parsed Arguments (argc/argv)

**Status:** ✅ Implemented in v0.9.8

### 4.1 Design

The shell pre-parses the argument string into individual tokens and provides
a structured interface for programs to access them.

**New syscall: `SYS_GET_ARGC`** (AH = 0x25)
- Returns: CL = argument count (0 if no args)

**New syscall: `SYS_GET_ARGV`** (AH = 0x26)
- Input: CL = argument index (0-based)
- Returns: SI = pointer to NUL-terminated argument, CX = length
- CF set if index out of bounds

### 4.2 Memory Layout

The shell builds an argument table in the ABI region (0x7F00–0x7FFB):

```
0x7F00: argc (1 byte) — number of arguments (max 15)
0x7F01: reserved (1 byte)
0x7F02: argv[0] pointer (2 bytes) — first arg string
0x7F04: argv[1] pointer (2 bytes) — second arg string
...
0x7F20: argv[15] pointer (2 bytes) — last possible arg
0x7F22–0x7FFB: argument string storage (NUL-separated)
```

Total: 218 bytes for argument storage. Maximum 15 arguments, max total
argument length ~200 characters.

### 4.3 Parsing Rules

- Arguments are whitespace-delimited (spaces, tabs)
- Double-quoted strings preserve spaces: `"hello world"` → one argument
- Quotes are stripped from the argument value
- No escape characters (keep it simple)
- Leading/trailing spaces ignored

### 4.4 Shell Implementation

During command dispatch, after identifying the program name:

```nasm
; After filename parsed, SI → args portion of command line
call shell_parse_args       ; Parse into argv table at 0x7F00
; Then launch program as normal
```

`shell_parse_args` routine (~80 bytes):
1. Set argc = 0
2. Skip leading spaces
3. If at NUL → done
4. If at `"` → read until closing `"` or NUL
5. Else → read until space or NUL
6. Store pointer in argv[argc], NUL-terminate the arg
7. Increment argc, goto 2

### 4.5 Program Usage Example

```nasm
; Get number of arguments
mov ah, SYS_GET_ARGC
int 0x80                    ; CL = argc

; Get first argument
xor cl, cl                  ; Index 0
mov ah, SYS_GET_ARGV
int 0x80                    ; SI → "5000", CX = 4
jc .no_args                 ; CF = no such index
call parse_hex16            ; AX = 0x5000
```

### 4.6 Backward Compatibility

`SYS_GET_ARGS` (0x24) continues to work — returns the full raw string.
Programs can choose raw or parsed access.  Simple programs use raw;
complex programs use argc/argv.

---

## 5. Layer 3 — Environment Variables

**Status:** Proposed for v0.9.9 or v0.9.19

### 5.1 Concept

The shell maintains a small set of named variables that are expanded in
command lines before execution.  This provides dynamic system information
to programs without requiring syscalls.

### 5.2 Variable Syntax

```
%NAME%          — expand variable (DOS-style)
```

Case-insensitive.  Unknown variables expand to empty string (no error).

### 5.3 Built-in Variables

| Variable | Expands to | Source |
|----------|-----------|--------|
| `%VERSION%` | `0907` | OS_VERSION from version.inc |
| `%HEAP%` | `0FA0` | Current free heap bytes (via INT 0x82) |
| `%DRIVE%` | `80` | Boot drive from BIB |
| `%FILES%` | `0B` | MNFS file count (via INT 0x81) |
| `%MODE%` | `00` or `01` | Boot mode (release/debug) |
| `%LAST%` | `00` | Last program exit code |

### 5.4 User-Defined Variables (SET command)

```
mnos:\> set NAME=WORLD
mnos:\> hello %NAME%
```

Program receives: SI → "WORLD\0"

### 5.5 Storage

Environment block at a fixed address (e.g., 0x7E00–0x7EFF, 256 bytes):

```
NAME1=VALUE1\0
NAME2=VALUE2\0
\0              ← double-NUL terminator (end of environment)
```

Maximum: ~12 variables averaging 20 chars each.

### 5.6 Shell `set` Command

| Syntax | Action |
|--------|--------|
| `set` | List all variables |
| `set NAME=VALUE` | Define/update variable |
| `set NAME=` | Delete variable |

### 5.7 Expansion Process

Before argument parsing (Layer 2), the shell scans the command line for
`%...%` patterns and replaces them in-place (or into a scratch buffer):

```
Input:  "hello %VERSION%"
After:  "hello 0907"
```

Then Layer 2 parsing proceeds on the expanded string.

### 5.8 Implementation Size Estimate

| Component | Bytes |
|-----------|-------|
| Environment block (data) | 256 |
| `set` command handler | ~120 |
| Variable expansion routine | ~100 |
| Built-in variable generators | ~80 |
| **Total** | **~556** |

### 5.9 New Syscall

**`SYS_GET_ENV`** (AH = 0x27)
- Input: DS:SI = variable name (NUL-terminated)
- Returns: DS:SI = value string, CX = length; CF set if not found

This allows programs to query environment variables directly, not just
receive them pre-expanded.

---

## 6. Layer 4 — Wildcards / Globbing

**Status:** Proposed for v0.10.x

### 6.1 Concept

The shell expands wildcard patterns against the MNFS directory before
passing arguments to programs.  This enables commands like:

```
mnos:\> type *.SYS         → expanded to all .SYS files
mnos:\> dir KERN*          → matches KERNEL.SYS, KERNELD.SYS
```

### 6.2 Pattern Syntax

| Pattern | Matches |
|---------|---------|
| `*` | Any sequence of characters (0 or more) |
| `?` | Any single character |

No recursive paths (MNFS is flat — no directories).

### 6.3 Expansion Rules

1. Shell scans each argument for `*` or `?`
2. If found, queries MNFS directory (via INT 0x81 FS_LIST_FILES)
3. Matches each filename against the pattern
4. Replaces the wildcard argument with all matching filenames
5. If no matches → pass the literal pattern (or error — TBD)

### 6.4 Example

```
mnos:\> dir *.MNX
```

Shell queries MNFS, finds: HELLO.MNX, MNMON.MNX
Expands to: `dir HELLO.MNX MNMON.MNX` (two arguments via Layer 2)

### 6.5 Pattern Matching Algorithm

Simple state machine (~60 bytes of code):

```nasm
; match_wildcard: DS:SI = pattern, ES:DI = filename
; Returns: CF clear = match, CF set = no match
match_wildcard:
    .loop:
        mov al, [si]
        cmp al, '*'
        je .star
        cmp al, '?'
        je .question
        ; Literal match
        cmp al, [di]
        jne .fail
        test al, al        ; Both NUL = match
        jz .match
        inc si
        inc di
        jmp .loop
    .question:
        cmp byte [di], 0   ; ? doesn't match NUL
        je .fail
        inc si
        inc di
        jmp .loop
    .star:
        inc si             ; Skip *
        ; Try matching rest at every position
        .star_try:
            push si
            push di
            call .loop     ; Recursive attempt
            pop di
            pop si
            jnc .match     ; Found a match
            cmp byte [di], 0
            je .fail
            inc di
            jmp .star_try
    .match:
        clc
        ret
    .fail:
        stc
        ret
```

### 6.6 Interaction with Layers

Expansion order:
1. **Layer 3** (environment variables) — expand `%VAR%` first
2. **Layer 4** (wildcards) — expand `*` and `?` against files
3. **Layer 2** (parsed arguments) — build argc/argv from expanded result

### 6.7 Implementation Size Estimate

| Component | Bytes |
|-----------|-------|
| Pattern matcher | ~80 |
| Directory scan + match loop | ~100 |
| Result assembly (build expanded line) | ~60 |
| **Total** | **~240** |

### 6.8 Limitations

- MNFS has max 15 files — expansion can't produce huge argument lists
- No path separators (flat filesystem)
- Expansion happens in the shell, not in programs
- Max expanded line: 200 characters (argv storage limit)

---

## 7. Layer 5 — I/O Redirection

**Status:** Proposed for v0.10.x or later

### 7.1 Concept

Allow programs to have their standard output sent to a file (or serial port),
and their input read from a file, using DOS/Unix-style redirect operators.

```
mnos:\> sysinfo > INFO.TXT
mnos:\> mnmon < SCRIPT.TXT
mnos:\> dir >> LOG.TXT
```

### 7.2 Operators

| Operator | Meaning |
|----------|---------|
| `> FILE` | Redirect stdout to file (create/overwrite) |
| `>> FILE` | Redirect stdout to file (append) |
| `< FILE` | Redirect stdin from file |
| `\| PROG` | Pipe stdout of left to stdin of right |

### 7.3 Prerequisites

This layer requires features that don't exist yet:

1. **File write support** — MNFS currently read-only (no write syscall)
2. **Stream abstraction** — programs currently call SYS_PRINT_STRING directly;
   need a way to intercept/redirect output
3. **File read as stream** — need SYS_READ_CHAR from a file source

### 7.4 Design: Transparent Redirection via Hook Table

Instead of changing every program, the kernel provides a **hook table** for
standard I/O operations:

```
IO_STDOUT_HOOK  equ 0x7FF8      ; 2 bytes: pointer to output routine
IO_STDIN_HOOK   equ 0x7FF6      ; 2 bytes: pointer to input routine
```

Default values: point to the normal SYS_PRINT_CHAR / SYS_READ_KEY handlers.

When redirection is active:
- Shell sets `IO_STDOUT_HOOK` → routine that writes to file buffer
- Shell sets `IO_STDIN_HOOK` → routine that reads from file buffer
- Kernel's SYS_PRINT_STRING/SYS_PRINT_CHAR call through the hook
- Program code is unchanged — redirection is transparent

### 7.5 Pipe Implementation

Piping (`|`) requires both programs to coexist in memory simultaneously,
which is impossible with a single TPA.  Two alternatives:

**Option A: Sequential with temp file**
```
PROG1 > TEMP.TMP
PROG2 < TEMP.TMP
```
Simple but requires file write support and wastes disk sectors.

**Option B: Line-buffered pipe**
- Run PROG1 until it outputs a line → buffer it
- Suspend PROG1, run PROG2 with buffered input
- Repeat

This requires cooperative multitasking or coroutine support — too complex
for the initial implementation.

**Recommendation:** Implement `>`, `>>`, and `<` first.  Defer `|` until
either temp file support or coroutines exist.

### 7.6 Shell Parsing

Before launching a program, the shell scans for redirect operators:

```nasm
; Scan backward from end of line for > >> < operators
; Strip them from the argument string
; Set up IO hooks accordingly
```

The redirect operators and their target filenames are NOT passed to the
program as arguments — they are consumed by the shell.

### 7.7 Implementation Size Estimate

| Component | Bytes |
|-----------|-------|
| Redirect operator parser | ~80 |
| Output hook (buffer → file write) | ~120 |
| Input hook (file read → buffer) | ~100 |
| Hook installation/teardown | ~40 |
| File write syscall (FS.SYS extension) | ~200 |
| **Total** | **~540** |

### 7.8 File Write — Required FS Extension

New INT 0x81 syscalls needed:

| Function | AH | Description |
|----------|-----|-------------|
| FS_CREATE_FILE | 0x06 | Create new file in MNFS directory |
| FS_WRITE_FILE | 0x07 | Write data to file sectors |
| FS_APPEND_FILE | 0x08 | Append data to existing file |

These modify the on-disk MNFS directory and data sectors — the most
invasive change in this entire document.

---

## 8. Expansion Order (All Layers Combined)

When the user types a command, the shell processes it through this pipeline:

```
┌─────────────────────────────────────────────────────────┐
│  User input: mnmon %HEAP% *.MNX > OUT.TXT              │
└────────────────────┬────────────────────────────────────┘
                     │
            ┌────────▼────────┐
            │ Layer 5: Strip  │  Identifies "> OUT.TXT"
            │ I/O redirects   │  Sets up stdout hook
            └────────┬────────┘
                     │  "mnmon %HEAP% *.MNX"
            ┌────────▼────────┐
            │ Layer 3: Expand │  %HEAP% → "0FA0"
            │ variables       │
            └────────┬────────┘
                     │  "mnmon 0FA0 *.MNX"
            ┌────────▼────────┐
            │ Layer 4: Expand │  *.MNX → "HELLO.MNX MNMON.MNX"
            │ wildcards       │
            └────────┬────────┘
                     │  "mnmon 0FA0 HELLO.MNX MNMON.MNX"
            ┌────────▼────────┐
            │ Layer 2: Parse  │  argc=3, argv[0]="0FA0"
            │ into argv       │  argv[1]="HELLO.MNX", argv[2]="MNMON.MNX"
            └────────┬────────┘
                     │
            ┌────────▼────────┐
            │ Layer 1: Store  │  Raw string also stored for
            │ raw + launch    │  backward compatibility
            └────────┬────────┘
                     │
            ┌────────▼────────┐
            │ Launch program  │  SYS_GET_ARGS → raw string
            │                 │  SYS_GET_ARGC/ARGV → parsed
            └─────────────────┘
```

---

## 9. Implementation Roadmap

| Version | Layer | What ships |
|---------|-------|-----------|
| v0.9.8 ✅ | Layer 2 | argc/argv parsing, SYS_GET_ARGC/SYS_GET_ARGV syscalls, quote support |
| v0.9.9 | Layer 3 | Environment variables, `set` command, expansion, SYS_GET_ENV |
| v0.9.19 | Layer 4 | Wildcard expansion (`*`, `?`) against MNFS |
| v0.10.x | Layer 5 | I/O redirection (`>`, `<`), requires FS write support |

Each version is independently useful.  Programs written for Layer 1 (raw args)
continue to work forever — layers are additive.

---

## 10. Memory Map Impact

```
0x7E00–0x7EFF  Environment block (256 bytes) — Layer 3
0x7F00–0x7F01  argc + reserved (2 bytes) — Layer 2
0x7F02–0x7F21  argv pointers (16 × 2 = 32 bytes) — Layer 2
0x7F22–0x7FF5  Argument string storage (~211 bytes) — Layer 2
0x7FF6–0x7FF7  IO_STDIN_HOOK (2 bytes) — Layer 5
0x7FF8–0x7FF9  IO_STDOUT_HOOK (2 bytes) — Layer 5
0x7FFA–0x7FFB  (reserved)
0x7FFC–0x7FFD  SHELL_ARGS_PTR (2 bytes) — Layer 1 (existing)
0x7FFE–0x7FFF  SHELL_SAVED_SP (2 bytes) — existing
```

This uses the 0x7E00–0x7FFF region (512 bytes) — formerly VBR load area,
free after boot completes.

---

## 11. Shell Size Impact

| Layer | Additional shell code | Additional shell data |
|-------|----------------------|----------------------|
| Layer 2 | ~80 bytes (parser) | 0 (uses shared region) |
| Layer 3 | ~300 bytes (set cmd + expand) | 256 bytes (env block) |
| Layer 4 | ~240 bytes (glob) | 0 (uses dir buffer) |
| Layer 5 | ~220 bytes (redirect parser + hooks) | 0 |
| **Total** | **~840 bytes** | **256 bytes** |

Shell is currently 16 sectors (8192 bytes) with room remaining.
All layers fit without a sector bump.

---

## 12. New Syscalls Summary

| Number | Name | Layer | Description |
|--------|------|-------|-------------|
| 0x24 | SYS_GET_ARGS | 1 | Raw args string (existing) |
| 0x25 | SYS_GET_ARGC | 2 | Argument count |
| 0x26 | SYS_GET_ARGV | 2 | Get argument by index |
| 0x27 | SYS_GET_ENV | 3 | Query environment variable |

These follow the stable ABI rule — numbers never change once assigned.

---

## 13. Program Adoption Examples

### mnmon with start address

```
mnos:\> mnmon 5000
```

mnmon.asm change (~10 bytes):
```nasm
entry:
    ; Check for address argument
    mov ah, SYS_GET_ARGS
    int 0x80
    test cx, cx
    jz .no_arg
    call parse_hex16        ; AX = start address from arg
    mov [mon_addr], ax
.no_arg:
    ; Print banner...
```

### hello with custom message

```
mnos:\> hello World
Hello, World!
```

### Future: BASIC with program file

```
mnos:\> basic GAME.BAS
```

BASIC reads filename argument, uses FS_READ_FILE to load the program.

---

## 14. Quoting and Special Characters

### 14.1 Quoting Rules (Layer 2)

| Input | argv[0] | argv[1] |
|-------|---------|---------|
| `hello world` | `hello` | `world` |
| `hello "big world"` | `hello` | `big world` |
| `"hello world"` | `hello world` | *(none)* |
| `hello ""` | `hello` | *(empty string)* |

### 14.2 Reserved Characters (Future)

| Char | Meaning | Layer |
|------|---------|-------|
| `"` | Quote delimiter | 2 |
| `%` | Variable delimiter | 3 |
| `*` | Wildcard (any chars) | 4 |
| `?` | Wildcard (one char) | 4 |
| `>` | Redirect stdout | 5 |
| `<` | Redirect stdin | 5 |
| `\|` | Pipe | 5 |

### 14.3 Escaping

No escape character defined.  If a program needs a literal `%` or `*`,
the user must use quoting:

```
mnos:\> echo "%VERSION%"    → prints literal %VERSION% (quoted = no expand)
```

Wait — that conflicts with quoting preserving the string.  Resolution:

- Variables expand **inside** quotes (like bash `"$VAR"`)
- Wildcards do **not** expand inside quotes (like bash `"*.txt"`)
- To pass a literal `%`, use `%%` (DOS convention):
  - `%%VERSION%%` → literal `%VERSION%`

---

## 15. Testing Strategy

### Layer 2 Tests (in Hyper-V)

```
mnos:\> hello                  → SYS_GET_ARGC returns 0
mnos:\> hello world            → argc=1, argv[0]="world"
mnos:\> hello "big world"      → argc=1, argv[0]="big world"
mnos:\> hello a b c            → argc=3
mnos:\> mnmon 5000             → starts at 0x5000 (db shows kernel)
```

### Layer 3 Tests

```
mnos:\> set                    → shows built-in variables
mnos:\> set NAME=TEST          → defines variable
mnos:\> hello %NAME%           → program receives "TEST"
mnos:\> set NAME=              → deletes variable
mnos:\> hello %NAME%           → program receives "" (empty)
```

### Layer 4 Tests

```
mnos:\> dir *.MNX              → shows HELLO.MNX, MNMON.MNX
mnos:\> dir KERN*              → shows KERNEL.SYS, KERNELD.SYS
mnos:\> dir ???.SYS            → shows MM.SYS, FS.SYS
```

---

## 16. Design Decisions

| Decision | Rationale |
|----------|-----------|
| DOS-style `%VAR%` not Unix `$VAR` | Matches target audience (Windows devs), avoids conflict with hex prefixes |
| Shell-side expansion | Programs don't need glob/env code; simpler programs |
| Max 15 args | MNFS has max 15 files; wildcard can't exceed this |
| No escape character | Complexity not justified for educational OS |
| Hooks for redirection | Transparent to programs; no code changes needed |
| Sequential pipe (temp file) | True pipes need multitasking; defer |
| Expansion before argv parsing | Natural order; variables can contain spaces |
