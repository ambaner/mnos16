# MNMON — Mini-OS Machine Monitor

**Version:** 1.1
**Status:** Implemented (v0.9.15); nested spawn support added in v0.9.16
**Binary:** `MNMON.MNX` — 5 sectors (2560 bytes)

An interactive memory monitor with **WinDbg-style command syntax**, implemented
as a standalone user program for mini-os.

---

## 1. Heritage

Inspired by Steve Wozniak's 1976 **Woz Monitor** for the Apple I — 256 bytes
of 6502 assembly providing examine, deposit, and run.  Four operations,
enough to bootstrap an entire computer.

MNMON modernizes this concept with WinDbg command syntax (`db`, `eb`, `g`)
familiar to any Windows developer, and extends it with structured display
commands for OS-specific data structures.

---

## 2. Goals

1. **Standalone .MNX user program** — not a shell builtin, loaded on demand
2. **WinDbg-compatible syntax** — `db`, `dw`, `eb`, `ew`, `g` commands
3. **Structured display** — formatted views of BIB, IVT, MCB heap, MNFS directory
4. **Full memory access** — examine/deposit anywhere 0x0000–0xFFFF
5. **Interactive program validation** — exercises the program loader with a real REPL

---

## 3. Why a Standalone Program

| Aspect | Shell builtin | Standalone .MNX |
|--------|---------------|-----------------|
| Shell size impact | +600+ bytes | 0 bytes |
| Availability | Always resident | Loaded on demand |
| Architectural purity | Bloats shell | Clean separation |
| Loader validation | N/A | Proves interactive programs work |

Loaded into the TPA at 0x8000, running in flat segment 0 with full access
to the entire 64 KB address space.

---

## 4. Command Reference

### 4.1 Launch

```
mnos:\> mnmon

mnmon v1.1
* 
```

The `*` prompt indicates readiness.  Type `?` for help, `q` to quit.

### 4.2 Commands

| Command | Syntax | Description |
|---------|--------|-------------|
| `db` | `db [XXXX [YYYY]]` | Display bytes (hex + ASCII), 16/line |
| `dw` | `dw [XXXX [YYYY]]` | Display words (16-bit), 8/line |
| `eb` | `eb XXXX BB [BB ...]` | Enter (write) bytes at address |
| `ew` | `ew XXXX WWWW [...]` | Enter (write) words at address |
| `g` | `g XXXX` | Go — near call to address (ret returns) |
| `bib` | `bib` | Display Boot Info Block (0x0600) |
| `ivt` | `ivt` | Display key interrupt vectors |
| `mcb` | `mcb` | Walk heap MCB chain |
| `dir` | `dir` | Display MNFS directory (raw) |
| `?` | `?` | Help |
| `q` | `q` | Quit to shell |
| *(Enter)* | | Repeat `db` at next address |

All addresses are 1–4 hex digits (case-insensitive).  Byte values are 1–2 hex
digits.  Word values are 1–4 hex digits (stored little-endian).

### 4.3 Display Formats

**`db` — Bytes** (hex + ASCII, WinDbg-style):

```
* db 0600
0600: 80 01 00 08 00 00 00 00-00 00 00 00 00 00 00 00  ................
```

16 bytes/line.  `-` separator between bytes 7–8.  Non-printable → `.`

**`dw` — Words** (16-bit little-endian):

```
* dw 5000
5000: 4E4D 4E4B 0008 0000 0000 0000 0000 0000
```

8 words/line.  Values in register-natural order.

**`bib` — Boot Info Block:**

```
* bib
-- BIB (0600) --
  Drive:   80
  A20:     01
  PartLBA: 00000800
  Mode:    01
  IntDep:  00
```

**`ivt` — Interrupt Vectors:**

```
* ivt
--- CPU Exceptions ---
  INT 00: 0000:XXXX
  INT 01: 0000:XXXX
  ...
--- OS Syscalls ---
  INT 80: 0000:5XXX
  INT 81: 0000:0XXX
  INT 82: 0000:2XXX
```

Shows CPU exception vectors 0x00–0x07 and OS syscall vectors 0x80–0x82
as SEG:OFF pairs.

**`mcb` — Heap Walk:**

```
* mcb
Addr Size Stat Own
8000 1000 FREE 00
```

Walks the MCB linked list from HEAP_START (0x8000).  Shows each block's
address, total size (including header), status (USED/FREE), and owner ID.
Stops at HEAP_END or corrupt magic byte.

**`dir` — MNFS Directory:**

```
* dir
Name         At Start Sec
LOADER.SYS   01 0003 0003
FS.SYS       01 0006 0003
KERNEL.SYS   01 0009 0008
...
```

Shows each file's 8.3 name, attribute byte, start sector, and sector count.
Uses INT 0x81 (FS_LIST_FILES) to read the cached directory.

### 4.4 Session Example

```
mnos:\> mnmon

mnmon v1.1
* db 5000 500F
5000: 4D 4E 4B 4E 08 00 00 00-00 00 00 00 00 00 00 00  MNKN............
* eb 0700 48 65 6C 6C 6F 00
* db 0700 0705
0700: 48 65 6C 6C 6F 00                                 Hello.
* bib
-- BIB (0600) --
  Drive:   80
  A20:     01
  PartLBA: 00000800
  Mode:    01
  IntDep:  00
* mcb
Addr Size Stat Own
8000 1000 FREE 00
* q

mnos:\>
```

---

## 5. Memory Layout When Running

```
┌─────────────────────────────┐ 0xF7FF (TPA end)
│                             │
├─────────────────────────────┤ 0x8A00
│  dir buffer (512 bytes)     │        ← temporary, beyond loaded sectors
├─────────────────────────────┤ 0x8000
│  MNMON.MNX (5 sectors)      │        ← code + data + BSS
├─────────────────────────────┤ 0x7F00
│  ARGV table                 │
├─────────────────────────────┤ 0x7000
│  Stack (grows ↓ from 0x7C00)│
├─────────────────────────────┤ 0x5000
│  KERNEL.SYS                 │
├─────────────────────────────┤ 0x3000
│  SHELL.SYS                  │
├─────────────────────────────┤ 0x2800
│  MM.SYS                     │
├─────────────────────────────┤ 0x0800
│  FS.SYS                     │
├─────────────────────────────┤ 0x0600
│  BIB (7 bytes)              │
├─────────────────────────────┤ 0x0000
│  IVT (256 vectors × 4)      │
└─────────────────────────────┘
```

**Warning**: Depositing into kernel/FS/MM/shell code will likely crash the
system.  That's the point — educational tool for understanding memory layout.

---

## 6. Implementation Details

### 6.1 Source

```
src/programs/mnmon.asm          — Single self-contained source file
```

### 6.2 Binary Format

```nasm
[BITS 16]
[ORG USER_PROG_BASE]
db 'MNEX'                       ; Magic — user-mode executable
dw 4                            ; Size in sectors (2048 bytes)
entry:                          ; Code begins at offset 6
```

### 6.3 Includes

```nasm
%include "syscalls.inc"         ; INT 0x80 function numbers
%include "memory.inc"           ; USER_PROG_BASE, MCB layout, heap constants
%include "bib.inc"              ; BIB field addresses
%include "mnfs.inc"             ; MNFS entry structure, FS syscall numbers
```

### 6.4 Syscalls Used

| Interrupt | Function | Purpose |
|-----------|----------|---------|
| INT 0x80 | SYS_PRINT_STRING | Banner, help, error messages |
| INT 0x80 | SYS_PRINT_CHAR | Prompt, spaces, separators |
| INT 0x80 | SYS_READ_KEY | Readline input |
| INT 0x80 | SYS_PRINT_HEX8 | Byte values |
| INT 0x80 | SYS_PRINT_HEX16 | Addresses, word values |
| INT 0x81 | FS_LIST_FILES | `dir` command (copies directory to buffer) |

### 6.5 Architecture

```
entry → mon_loop (REPL)
         ├── mon_readline (echo, backspace, auto-lowercase)
         ├── dispatch: 2-char (db/dw/eb/ew/di), 3-char (bib/ivt/mcb), 1-char (g/?/q)
         ├── cmd_db / cmd_db_range (hex + ASCII display)
         ├── cmd_dw (word display)
         ├── cmd_eb / cmd_ew (memory write)
         ├── cmd_go (near call to address)
         ├── cmd_bib (formatted BIB fields)
         ├── cmd_ivt (IVT vector dump)
         ├── cmd_mcb (heap chain walk)
         └── cmd_dir (MNFS directory via INT 0x81)

Helpers: parse_hex16, parse_hex8, is_hex_char, is_hex_digit, skip_spaces
```

### 6.6 State

```nasm
mon_addr:       dw 0            ; Current address (sticky, auto-increments)
mon_end:        dw 0            ; End address for range displays
mon_line_start: dw 0            ; Line start (for ASCII column calculation)
mon_buf:        times 40 db 0   ; Input buffer (39 chars + NUL)
mon_dir_buf     equ 0x9800      ; 512-byte dir buffer beyond loaded sectors
```

### 6.7 Input Handling

- Own readline (not the shell's): character-by-character via SYS_READ_KEY
- Backspace with visual erase (BS + space + BS)
- Auto-lowercase for uniform command parsing
- 40-char buffer — sufficient for longest valid command
- NUL-terminated; CX = length returned

### 6.8 Command Dispatch

Two-level dispatch based on command length:
1. Single chars: `?`, `q`, `g` (g takes address argument)
2. Two chars: first=operation (d/e), second=width (b/w) + `di` for dir
3. Three chars: `bib`, `ivt`, `mcb`

All comparison is case-insensitive (lowercase before compare).

---

## 7. Size Budget

| Component | Actual bytes |
|-----------|-------------|
| Code (all commands + helpers) | ~1500 |
| String data (all labels) | ~500 |
| BSS (mon_addr + mon_buf etc.) | ~46 |
| **Total (code+data)** | **~2036** |
| **Sector allocation** | **4 sectors (2048 bytes)** |
| **Free space** | **12 bytes** |

The `dir` command's 512-byte buffer lives at 0x9800 (beyond loaded sectors)
to avoid inflating the binary.

---

## 8. Error Handling

| Condition | Response |
|-----------|----------|
| Unknown command | `^ Unknown command` + re-prompt |
| Missing address for `eb`/`ew` | `^ Syntax error` |
| No args for `db`/`dw` | Uses current address (auto-increment) |
| Invalid hex digit | Parsing stops, partial value used |
| `g` target crashes | System unrecoverable (by design) |
| Corrupt MCB magic | `^ Corrupt MCB (bad magic)` + stop walk |

Error format uses WinDbg's `^ ` caret-prefix convention.

---

## 9. The `g` (Go) Command

Executes a **near call** to the target address:

```nasm
call [.go_addr]     ; Near call — ret returns to monitor
```

- Target code with `ret` returns cleanly to mnmon
- No protection — calling random addresses will crash
- Cannot load other programs (same TPA — would overwrite mnmon)

---

## 10. Limitations

| Limitation | Reason |
|------------|--------|
| Cannot load programs from within mnmon | Both would occupy same TPA (0x8000) |
| No breakpoints | Would require INT 3 hook + handler infrastructure |
| No register display | Registers are transient; no saved context to show |
| No disassembly | x86-16 decoder would be 1+ KB alone |
| 16-bit addresses only | Real mode, flat segment 0 |

---

## 11. Build Integration

### Assembly

```
nasm -f bin -I src/include/ -o build/boot/mnmon.mnx src/programs/mnmon.asm
```

Auto-discovered by `build.ps1` (scans `src/programs/*.asm`).

### Disk Image

Packed into MNFS by `create-disk.ps1` with attribute `ATTR_EXEC` (0x02).
Currently: 11 MNFS files, 74 total sectors.

---

## 12. Future Enhancements

| Enhancement | WinDbg equiv | Description |
|-------------|--------------|-------------|
| Display dwords | `dd XXXX` | 32-bit display |
| Display ASCII string | `da XXXX` | Show chars until NUL |
| Fill memory | `f XXXX YYYY BB` | Fill range with pattern |
| Search bytes | `s XXXX YYYY BB...` | Find byte sequence |
| I/O port access | `ib`/`ob` | Read/write I/O ports |
| Register display | `r` | Show saved register state |
| Breakpoints | `bp XXXX` | INT 3 trap back to monitor |
| Fault integration | — | Assert/fault → transfer to mnmon |

Fault integration is the highest-value future feature: instead of `cli; hlt`
on assert failure, transfer control to mnmon for interactive crash inspection.
