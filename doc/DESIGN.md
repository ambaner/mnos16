# Mini-OS Design Document

## 1. Project Overview

**Mini-OS** is a minimalistic operating system built from scratch, targeting the x86
architecture. The project is educational — designed so anyone can clone the repository,
build a bootable disk image, and run it in a Hyper-V virtual machine with no prior
OS-development experience.

The current milestone is **M11: Unit Test Framework** — the MBR chain-loads a
minimal VBR, which finds and loads LOADER.SYS from the MNFS directory, LOADER
enables A20 and presents a boot menu, the kernel installs INT 0x80 syscalls,
loads FS.SYS (filesystem module with INT 0x81 API), loads MM.SYS (heap
allocator with INT 0x82 API), and finally loads the interactive shell
(SHELL.SYS) — all file locations discovered via directory lookup, no hardcoded
disk offsets.  The shell can load and execute user programs (`.MNX` files) from
disk into a 30 KB Transient Program Area, with structured argc/argv parsing
for command-line arguments.  A Python + Unicorn Engine test framework provides
37 unit tests with coverage reporting.  Debug builds add serial logging,
syscall tracing, user-mode debug syscalls, assertion macros, INT depth tracking,
DAP hex dumps, and CPU fault handlers.  Fault handlers are present in both
release and debug builds (PIC remapped to avoid IRQ/exception vector conflicts).

### Design Principles

| Principle | Rationale |
|-----------|-----------|
| **Zero proprietary tools** | All tooling is publicly available so the repo works for anyone who clones it from GitHub. |
| **Self-bootstrapping build** | `build.bat` downloads NASM automatically; no manual tool installation beyond PowerShell 7. |
| **One-command build** | `build.bat` produces a ready-to-boot VHD. |
| **One-command VM setup** | `setup-vm.bat` creates or updates a Hyper-V VM, including VHD deployment. |
| **Iterate fast** | Rebuild → re-run `setup-vm.bat` → boot. The VHD is swapped in-place; no manual VM reconfiguration. |

---

## 2. Architecture

### 2.1 Boot Sequence

```
┌─────────────┐     ┌──────────────────────────────┐     ┌────────────┐
│  BIOS POST  │────>│  Load MBR (sector 0, 512 B)  │────>│  mbr.asm   │
│             │     │  to 0x0000:0x7C00             │     │  executes  │
└─────────────┘     └──────────────────────────────┘     └────────────┘
                                                               │
                                            ┌──────────────────┘
                                            v
                                     ┌─────────────┐
                                     │  Set up      │
                                     │  segments &  │
                                     │  stack       │
                                     └──────┬──────┘
                                            │
                                            v
                                     ┌─────────────┐
                                     │  Clear       │
                                     │  screen      │
                                     │  (INT 10h)   │
                                     └──────┬──────┘
                                            │
                                            v
                                     ┌──────────────┐
                                     │  Print banner │
                                     │  "In MBR"     │
                                     └──────┬───────┘
                                            │
                                            v
                                     ┌──────────────────┐
                                     │  Scan partition   │
                                     │  table (4 entries)│
                                     │  Print each entry │
                                     └──────┬───────────┘
                                            │
                                            v
                                     ┌──────────────────┐
                                     │  Find active      │
                                     │  partition (0x80)  │
                                     └──────┬───────────┘
                                            │
                                   ┌────────┴────────┐
                                   v                 v
                            ┌────────────┐   ┌──────────────┐
                            │ INT 13h    │   │ "No active   │
                            │ AH=42h LBA │   │  partition"  │
                            │ read VBR   │   │  → halt      │
                            └─────┬──────┘   └──────────────┘
                                  │
                                  v
                            ┌────────────┐
                            │ Copy VBR   │
                            │ to 0x7C00  │
                            │ Jump to it │
                            └─────┬──────┘
                                  │
                                  v
                            ┌────────────┐
                            │  vbr.asm   │
                            │ Find LOADER│
                            │ via MNFS   │
                            │ directory  │
                            │ to 0x0800  │
                            └─────┬──────┘
                                  │
                                  v
                            ┌────────────┐
                            │ LOADER.SYS │
                            │ Enable A20 │
                            │ (3 methods)│
                            │Find KERNEL │
                            │ via MNFS   │
                            │ to 0x5000  │
                            └─────┬──────┘
                                  │
                                  v
                            ┌────────────┐
                            │ KERNEL.SYS │
                            │Install INT │
                            │   0x80     │
                            │ Load FS.SYS│
                            │ to 0x0800  │
                            │ Init INT   │
                            │   0x81     │
                            │ Load MM.SYS│
                            │ to 0x2800  │
                            │ Init INT   │
                            │   0x82     │
                            │ Load SHELL │
                            │ to 0x3000  │
                            └─────┬──────┘
                                  │
                                  v
                            ┌────────────┐
                            │ SHELL.SYS  │
                            │  mnos:\>   │
                            │ (via INT   │
                            │0x80/81/82) │
                            └────────────┘
```

### 2.2 Memory Layout

> **📄 Deep dive**: See [MEMORY-LAYOUT.md](MEMORY-LAYOUT.md) for the exhaustive
> memory map — every region's purpose, lifetime, stack analysis, reclaimable
> memory, and the roadmap to protected-mode addressing.

| Address | Contents |
|---------|----------|
| `0x0000:0x0000` – `0x0000:0x03FF` | Real-mode Interrupt Vector Table (IVT) |
| `0x0000:0x0400` – `0x0000:0x04FF` | BIOS Data Area (BDA) |
| `0x0000:0x0600` – `0x0000:0x060F` | **Boot Info Block (BIB)** — shared parameters |
| `0x0000:0x0800` – `0x0000:0x27FF` | **FS.SYS** (8 KB max, loaded by kernel; replaces LOADER at runtime) |
| `0x0000:0x2800` – `0x0000:0x2FFF` | **MM.SYS** (2 KB max, loaded by kernel; memory manager INT 0x82) |
| `0x0000:0x3000` – `0x0000:0x4FFF` | **SHELL.SYS** (8 KB max, loaded by kernel) |
| `0x0000:0x5000` – `0x0000:0x6FFF` | **KERNEL.SYS** (8 KB max, 8 sectors used) |
| `0x0000:0x7000` – `0x0000:0x7003` | Stack canary (debug builds only) |
| `0x0000:0x7C00` – `0x0000:0x7FFF` | **VBR** (2 sectors, boot-time only) |
| `0x0000:0x7BFE` ↓ | Stack (grows downward from 0x7C00) |
| `0x0000:0x7F00` – `0x0000:0x7FFB` | **ARGV table** — argc (1 byte) + 16 word pointers + NUL-separated arg strings |
| `0x0000:0x7E00` – `0x0000:0x9DFF` | VBR load buffer (MBR uses this temporarily) |
| `0x0000:0x8000` – `0x0000:0xF7FF` | **TPA** (Transient Program Area, 30 KB — user `.MNX` programs loaded here) |
| `0xFFFF:0x0010` – `0xFFFF:0xFF00` | **HMA HEAP** (~64 KB, managed by MM.SYS via INT 0x82) |

#### Boot Info Block (BIB) — 0x0600

The BIB is a fixed-address parameter block populated by early boot stages and
read by later stages:

| Offset | Size | Field | Set by |
|--------|------|-------|--------|
| 0 | 1 | `boot_drive` | VBR |
| 1 | 1 | `a20_status` | LOADER (1=enabled, 0=failed) |
| 2 | 4 | `part_lba` | VBR (partition start LBA) |

### 2.3 MBR Binary Format

```
Offset   Size   Description
───────  ─────  ──────────────────────────────
0x000    446    Boot code (padded with 0x00)
0x1BE     64    Partition table (4 × 16-byte entries)
0x1FE      2    Boot signature: 0x55, 0xAA
```

#### Partition Table Entry Format (16 bytes)

| Offset | Size | Field           | Description                          |
|--------|------|-----------------|--------------------------------------|
| 0      | 1    | Status          | `0x80` = active/bootable, `0x00` = inactive |
| 1      | 3    | CHS First       | CHS of first sector (`0xFEFFFF` for LBA) |
| 4      | 1    | Type            | Partition type (`0x7F` = mini-os)    |
| 5      | 3    | CHS Last        | CHS of last sector (`0xFEFFFF` for LBA) |
| 8      | 4    | LBA Start       | Starting sector (little-endian)      |
| 12     | 4    | Size            | Number of sectors (little-endian)    |

The partition table is stamped into the MBR binary by `tools/create-disk.ps1` at build
time. The MBR code scans all 4 entries, prints their info, and chain-loads the VBR
from the first entry marked active (`0x80`).

### 2.4 Volume Boot Record (VBR)

> **📄 Design rationale**: See [BOOT-LAYOUT-RATIONALE.md](BOOT-LAYOUT-RATIONALE.md)
> for why three stages, comparisons with DOS/Windows/Linux boot chains, the LBA
> gap debate, and clobber protection analysis.

The VBR (`src/boot/vbr.asm`) is a minimal loader at the start of the active
partition. It has a self-describing header and loads LOADER.SYS from a fixed
partition offset:

```
VBR Header (starts at byte 0 of the partition):
  Offset 0:   EB xx      JMP SHORT past header
  Offset 2:   90         NOP
  Offset 3:   'MNOS'     Magic identifier (4 bytes)
  Offset 7:   dw 2       VBR size in sectors
  Offset 9:   dd N       Partition start LBA (stamped by create-disk.ps1)
```

The MBR performs a two-phase load:
1. **Phase 1** — Read the first sector to `0x7E00` and parse the header.
2. **Phase 2** — Re-read all N sectors (from the header) to `0x7E00`.
3. Copy N sectors from `0x7E00` to `0x7C00` and jump.

The VBR then:
1. Populates the Boot Info Block (BIB) at 0x0600
2. Reads the MNFS directory (partition sector 2) to find LOADER.SYS
3. Loads LOADER.SYS to 0x0800
4. Verifies the 'MNLD' magic
5. Jumps to LOADER.SYS

### 2.5 LOADER.SYS

The loader (`src/loader/loader.asm`) is loaded by the VBR to 0x0800.  It has a
self-describing header:

```
LOADER Header:
  Offset 0:   'MNLD'    Magic identifier (4 bytes)
  Offset 4:   dw N      Loader size in sectors
```

The loader:
1. Enables the A20 gate (3 fallback methods, see §3.7)
2. Reads the MNFS directory to find KERNEL.SYS
3. Loads KERNEL.SYS to 0x5000
4. Verifies the 'MNKN' magic
5. Jumps to KERNEL.SYS

### 2.6 KERNEL.SYS

The kernel (`src/kernel/kernel.asm`) is loaded by the loader to 0x5000.  It
installs the INT 0x80 syscall handler, then loads FS.SYS, MM.SYS, and SHELL.SYS
via MNFS directory lookup:

1. Installs INT 0x80 syscall handler in the IVT
2. Finds FS.SYS via MNFS directory, loads to 0x0800 (reusing LOADER's memory)
3. Calls FS.SYS init (at offset 6) — installs INT 0x81 filesystem handler
4. Finds MM.SYS via MNFS directory, loads to 0x2800
5. Calls MM.SYS init (at offset 6) — installs INT 0x82 memory manager handler
6. Finds SHELL.SYS via MNFS directory, loads to 0x3000
7. Jumps to SHELL.SYS

### 2.7 FS.SYS

The filesystem module (`src/fs/fs.asm`) is loaded by the kernel to 0x0800.
It owns the INT 0x81 filesystem syscall interface.  Header:

```
FS Header:
  Offset 0:   'MNFS'    Magic identifier (4 bytes)
  Offset 4:   dw N      FS module size in sectors
  Offset 6:   jmp init  Entry point for initialization
```

> **📄 Full specification**: See [FILESYSTEM.md](FILESYSTEM.md) for the complete
> MNFS format, directory structure, INT 0x81 API, and design rationale.

### 2.8 MM.SYS

The memory manager (`src/mm/mm.asm`) is loaded by the kernel to 0x2800.
It owns the INT 0x82 memory management syscall interface.  Header:

```
MM Header:
  Offset 0:   'MNMM'    Magic identifier (4 bytes)
  Offset 4:   dw N      MM size in sectors
```

Provides heap allocation services (first-fit, word-aligned) over a 30 KB
heap region (0x8000–0xF7FF).  Supports alloc, free, available-memory query,
and heap info.

> **📄 Full specification**: See [MEMORY-MANAGER.md](MEMORY-MANAGER.md) for the
> complete INT 0x82 API, MCB header format, and allocation algorithm.

### 2.9 SHELL.SYS

The shell (`src/shell/shell.asm`) is loaded by the kernel to 0x3000.  It provides
the interactive command-line interface.  Header:

```
SHELL Header:
  Offset 0:   'MNEX'    Magic identifier (4 bytes)
  Offset 4:   dw N      Shell size in sectors
```

> **Note**: Despite being the user-facing interface, the shell uses the `.SYS`
> extension because it is part of the system boot chain — the kernel loads it
> directly into system memory at a fixed address (0x3000), it never returns
> control to the kernel, and it has unrestricted access to all INT vectors.
> User-mode programs loaded on demand into the TPA (0x8000+) use the `.MNX`
> extension instead.

### 2.10 File Extension Conventions

| Extension | Meaning | Loaded by | Memory region |
|-----------|---------|-----------|---------------|
| `.SYS`    | System binary — part of the trusted boot chain | Kernel (at boot) | Fixed system addresses (0x0800–0x4FFF) |
| `.MNX`    | User-mode executable (MNEX format) | Shell (implicit execution) | TPA at 0x8000+ |
| (none)    | Raw boot sectors (MBR, VBR) | BIOS / MBR | 0x7C00 |

System binaries (`.SYS`) are loaded at boot time to fixed memory addresses and
remain resident for the lifetime of the OS.  They are marked with
`ATTR_SYSTEM` (bit 0) in the MNFS directory and cannot be executed as user
programs.

User executables (`.MNX`) are loaded on demand into the Transient Program Area
(TPA) at 0x8000 and must contain an MNEX header with the `'MNEX'` magic.  They
are marked with `ATTR_EXEC` (bit 1) in the MNFS directory.

### 2.11 Disk Layout

> **📄 Design rationale**: See [BOOT-LAYOUT-RATIONALE.md](BOOT-LAYOUT-RATIONALE.md)
> for how this layout compares to DOS, Windows, and Linux.
>
> **📄 Filesystem spec**: See [FILESYSTEM.md](FILESYSTEM.md) for the MNFS
> directory format and file packing strategy.

```
Sector 0                → MBR (code + partition table + 0xAA55)
Sectors 1–2047          → Gap (zeroed, reserved)
Sector 2048             → Partition start: VBR (2 sectors)
Sector 2050             → MNFS directory table (1 sector, up to 15 entries)
Sector 2051+            → Files packed contiguously:
                            LOADER.SYS  (3 sectors)
                            FS.SYS      (3 sectors)
                            KERNEL.SYS  (8 sectors)
                            SHELL.SYS   (16 sectors)
                            MM.SYS      (1 sector)
                            FSD.SYS     (5 sectors)
                            KERNELD.SYS (14 sectors)
                            SHELLD.SYS  (16 sectors)
                            MMD.SYS     (2 sectors)
                            HELLO.MNX   (1 sector)
                            MNMON.MNX   (4 sectors)
Remaining sectors       → Zeroed (available for future files)
```

File positions are **not hardcoded** — they are determined at build time by
`create-disk.ps1` and recorded in the MNFS directory table.  Adding or resizing
a file requires no source code changes.

The MBR is a flat 512-byte binary. NASM's `-f bin` output format produces a raw binary
with no headers — exactly what the BIOS expects.

---

## 3. Interactive Shell

After the boot chain (MBR → VBR → LOADER → KERNEL → FS.SYS → MM.SYS → SHELL.SYS), the shell
clears the screen, displays a version banner, and enters an
interactive command loop with a `mnos:\>` prompt.

The shell reads boot parameters (boot drive, A20 status) from the Boot Info
Block (BIB) at 0x0600.  All hardware access goes through INT 0x80 kernel
syscalls; filesystem access uses INT 0x81 (FS.SYS).

### 3.1 Shell Architecture

The shell is a simple read-eval-print loop:

1. Display the prompt `mnos:\>`
2. Read a line of input via `readline` (INT 16h, with backspace and auto-lowercase)
3. Compare the input against known command strings via `strcmp`
4. If recognized, dispatch to the matching handler
5. If unrecognized, attempt implicit program execution (load `.MNX` from disk)
6. Before launching a program, parse the command line into argc/argv (see §3.9)
7. After the command completes, return to step 1

### 3.2 Commands

| Command | Description |
|---------|-------------|
| `sysinfo` | Display 5 pages of system information (CPU, memory, BDA, video/disk, IVT) |
| `mem` | Detailed memory info: conventional/extended RAM, A20 status, layout, E820 map |
| `dir` | List files on disk: name, type (SYS/EXE), sectors, bytes, total summary |
| `ver` | Version, architecture, assembler, platform, boot chain, disk, source URL |
| `help` | List available commands |
| `cls` | Clear the screen and re-display banner |
| `reboot` | Warm-reboot the system (BIOS reset vector) |

Unknown commands are treated as program names — the shell searches for a
matching `.MNX` file and executes it if found (see
[PROGRAM-LOADER.md](PROGRAM-LOADER.md)).

### 3.3 `sysinfo` Command

Displays five pages of system information, with "Press any key..." between each
page and a screen clear before each new page:

| Page | Title | Information |
|------|-------|-------------|
| 1 | CPU Information | CPUID vendor string, family/model/stepping, feature flags (FPU, TSC, MSR, CX8, PGE, CMOV, MMX, SSE/2/3/4.1/4.2), hypervisor detection + vendor |
| 2 | Memory | INT 12h conventional memory, INT 15h AH=88h extended memory, E820 memory map |
| 3 | BIOS Data Area | COM/LPT port addresses, equipment word, video mode, columns, page size |
| 4 | Video & Disk | Current video mode, cursor position, video memory base, boot drive geometry, EDD version/total sectors/bytes per sector |
| 5 | IVT Sample | First 8 interrupt vectors (INT 0-7) with descriptions |

#### CPUID Detection

The CPUID instruction (available on 486+) is detected by attempting to flip bit 21
(the ID flag) in EFLAGS.  If the bit toggles, CPUID is supported.  Leaf 0
returns the 12-byte vendor string; leaf 1 returns the CPU family, model, stepping,
and feature flags in EDX/ECX.  When the hypervisor-present flag (ECX bit 31) is
set, leaf 0x40000000 returns the hypervisor vendor string (e.g., "Microsoft Hv").

#### EDD (Enhanced Disk Drive)

INT 13h AH=41h checks for EDD extension support.  If present, AH=48h returns
an extended parameter block with total sector count (64-bit) and bytes per sector,
providing more detail than the legacy CHS geometry from AH=08h.

### 3.4 `mem` Command

Displays detailed memory information on a single page:

- **Conventional memory** — INT 12h (typically 640 KB)
- **Extended memory** — INT 15h AH=88h (memory above 1 MB)
- **A20 gate status** — Shows the boot-time enablement result and performs a
  live wrap-around re-test to confirm A20 is still active
- **Real-mode memory layout** — Static map showing IVT, BDA, free area, boot
  area, video RAM, ROM area, and extended area
- **E820 memory map** — Full BIOS-reported memory map with base, length, and type

#### A20 Gate — Background

The A20 gate controls whether the CPU's 21st address line (A20) is active.  On
the original IBM PC/AT (1984), this line was disabled at boot to maintain backward
compatibility with the 8086, which only had 20 address lines and naturally wrapped
addresses above 1 MB.  Some old DOS programs relied on this wrapping behavior.

The A20 detection works by testing if two addresses that differ only in bit 20
(0x0000:0x0500 = linear 0x00500, and 0xFFFF:0x0510 = linear 0x100500) point to
the same physical byte.  If writing to one changes the other, addresses are
wrapping — A20 is disabled.

**In practice, most modern systems (including Hyper-V, QEMU, and modern BIOS
firmware) enable A20 by default during POST.**  The A20 gate is essentially a
legacy concern.  You would only see "Disabled" on vintage hardware or emulators
configured for strict 8086 compatibility.

### 3.7 A20 Gate Enablement

As of v0.3.0 (now in LOADER.SYS since v0.4.0), the A20 line is explicitly enabled
at boot before loading the shell.  This ensures access to memory above 1 MB
regardless of the platform.  Three methods are attempted in order, with a
wrap-around verification after each:

| Method | Mechanism | Notes |
|--------|-----------|-------|
| 1. BIOS | INT 15h AX=2401h | Cleanest, supported by modern BIOSes |
| 2. Keyboard controller | 8042 ports 0x64/0x60, set bit 1 of output port | Classic AT method, most compatible |
| 3. Fast A20 | Port 0x92, set bit 1 (clear bit 0 to avoid reset) | Quick but not available on all hardware |

The `check_a20` subroutine performs the wrap-around test: it writes different
values to 0x0000:0x0500 and 0xFFFF:0x0510, then checks if they alias.  The result
is stored in `a20_status` (1 = enabled, 0 = failed) and displayed by the `mem`
command.  If all three methods fail, the shell still runs (within the low 1 MB)
but prints a warning.

### 3.5 `ver` Command

Displays static version and build information:

```
  MNOS v0.9.9
  Arch:      x86 real mode (16-bit)
  Assembler: NASM
  Platform:  Hyper-V Gen 1
  Boot:      MBR -> VBR -> LOADER -> KERNEL -> FS -> MM -> SHELL
  Disk:      16 MB fixed VHD
  Source:    github.com/ambaner/mini-os
```

### 3.8 Shell Subroutines

These subroutines live in SHELL.SYS and are available to all commands:

| Routine | Description |
|---------|-------------|
| `check_a20` | Test A20 status via wrap-around; ZF=0 if enabled, ZF=1 if disabled |
| `readline` | Read line of input into buffer (backspace, auto-lowercase) |
| `strcmp` | Compare two NUL-terminated strings, set ZF if equal |
| `shell_parse_args` | Parse raw command line into argc/argv table at 0x7F00 |
| `puts` | Print NUL-terminated string via INT 10h AH=0Eh |
| `putc` | Print single character |
| `puthex8` | Print AL as two hex digits |
| `print_hex16` | Print AX as four hex digits |
| `print_dec16` | Print AX as unsigned decimal |
| `wait_key` | Print prompt, wait for keypress, clear screen |

### 3.9 Command-Line Parsing (argc/argv)

Before launching a user program, the shell parses the raw command line into
structured arguments via `shell_parse_args` (in `shell_parse_args.inc`).  The
parsed data is stored in the **ARGV table** at 0x7F00:

| Offset | Size | Field |
|--------|------|-------|
| 0x7F00 | 1 | `argc` — argument count (0–15) |
| 0x7F02 | 32 | Pointer table — 16 word pointers to NUL-terminated strings |
| 0x7F22 | 218 | String storage — NUL-separated argument strings |

**Parsing rules:**
- Spaces and tabs are delimiters
- Double-quoted strings are treated as a single argument (quotes stripped)
- Maximum 15 arguments; excess arguments are silently dropped
- The program name is always `argv[0]`

Programs access their arguments via two syscalls:
- `SYS_GET_ARGC` (AH=0x25, INT 0x80) — returns count in CL
- `SYS_GET_ARGV` (AH=0x26, INT 0x80) — index in CL → SI=string, CX=length; CF set if out of bounds

The raw (unparsed) argument string remains available via `SYS_GET_ARGS` (AH=0x24)
for backward compatibility.

> **📄 Full design**: See [COMMAND-LINE.md](COMMAND-LINE.md) for the 5-layer
> command-line expansion roadmap (wildcards, environment variables, pipes, I/O
> redirection).

### 3.10 User Programs

User programs are `.MNX` executables loaded into the Transient Program Area
(TPA) at 0x8000 by the shell.  They use the MNEX binary format with an `'MNEX'`
header.  Two example programs ship with mini-os:

| Program | Description |
|---------|-------------|
| `HELLO.MNX` | Hello World — prints a message and exits (1 sector) |
| `MNMON.MNX` | Machine monitor — WinDbg-style memory inspector with 11 commands: `db`, `dw`, `eb`, `ew`, `g`, `di`, `bib`, `ivt`, `mcb`, `?` (4 sectors) |

> **📄 Full specification**: See [PROGRAM-LOADER.md](PROGRAM-LOADER.md) for
> the program loading mechanism, validation layers, and TPA layout.
>
> **📄 MNMON reference**: See [MNMON.md](MNMON.md) for the machine monitor
> command reference and usage examples.

---

## 4. Testing

> **📄 Full specification**: See [TESTING.md](TESTING.md) for the complete
> 3-tier test strategy, test matrix, and maintenance guide.

Mini-os uses a **Python + Unicorn Engine** unit test framework that emulates
16-bit x86 routines without QEMU or hardware.  Tests run automatically as part
of every build (`build.bat` / `build.ps1`).

### 4.1 Test Architecture (3 Tiers)

| Tier | Scope | Engine | Status |
|------|-------|--------|--------|
| **Tier 1** | Pure logic (no INT calls) | Unicorn Engine | ✅ Implemented (64 tests) |
| **Tier 2** | Syscall-level (INT hooks) | Unicorn + hooks | 🔮 Planned |
| **Tier 3** | Full system (boot-to-shell) | QEMU headless | 🔮 Planned |

### 4.2 Current Test Coverage

| Module | Tests | What's tested |
|--------|-------|---------------|
| `shell_parse_args` | 15 | Null/empty input, single/multi args, spaces, tabs, quoted strings, max overflow |
| `run_parse_filename` | 9 | Simple names, extensions, case conversion, argument extraction, truncation |
| `strcmp` | 11 | Equal, different, prefix mismatch, empty strings, case sensitivity, long strings |

### 4.3 Coverage Reporting

Instruction-level coverage is tracked via Unicorn's code hooks.  Every build
generates:
- `coverage/index.html` — visual dashboard with per-routine bar charts
- `coverage/summary.json` — machine-readable coverage data
- `coverage/badge.json` — shields.io endpoint for README badges

In CI/CD, coverage is deployed to GitHub Pages after each push to `main`.

---

## 5. Disk Image: VHD Format

### 5.1 Why VHD?

Hyper-V natively supports VHD (Virtual Hard Disk) files. The **fixed-size VHD** format
is the simplest variant: raw disk data followed by a 512-byte footer. No dynamic
allocation, no differencing chains, no BAT — just:

```
┌──────────────────────────────────────┐
│        Raw disk data                 │  ← disk_size bytes (16 MB)
│        (MBR at byte 0, rest zeroed)  │
├──────────────────────────────────────┤
│        VHD Footer (512 by

<note>Content truncated. Call the fetch tool with a start_index of 20000 to get more content.</note>