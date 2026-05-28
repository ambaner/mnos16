# LOADER.SYS — Stage 2 Boot Loader Design Document

> **Module**: `src/loader/loader.asm`  
> **Load address**: 0x0000:0x0800 (2 KB, FS.SYS region — LOADER is replaced after use)  
> **Loaded by**: VBR (`src/boot/vbr.asm`)  
> **Loads**: KERNEL.SYS (or KERNELD.SYS in debug)  
> **MNEX magic**: `MNLD` (4 bytes)  
> **Current size**: 3 sectors (1536 bytes)  
> **Max size**: 16 sectors (8 KB, limited by memory region 0x0800–0x27FF)

---

## 1. Purpose

LOADER.SYS is the stage-2 boot loader.  It runs after the VBR has loaded it
into memory at 0x0800.  Its responsibilities are:

1. **Enable the A20 gate** — three fallback methods (BIOS INT 15h, keyboard
   controller 8042, fast port 0x92), with verification after each attempt.
2. **Present a boot menu** — display available kernel configurations and wait
   for the user's selection *(v0.8.0+)*.
3. **Load the selected kernel** — find the correct KERNEL variant in the MNFS
   directory table, load it at 0x5000, validate its magic, and jump to it.
4. **Populate the Boot Info Block (BIB)** — record boot-time data (A20 status,
   selected boot mode) so downstream components can read it.

The loader is a **transient** component: the kernel overwrites its memory at
0x0800 with FS.SYS shortly after taking control.  By that point, LOADER's
job is done.

---

## 2. Boot Chain Context

```
          MBR (sector 0)
              │
              v
          VBR (partition sector 0–1)
              │  Finds LOADER.SYS via MNFS directory lookup
              │  Loads to 0x0800
              v
       ┌──────────────┐
       │  LOADER.SYS  │  ← this module
       │              │
       │  1. A20 gate │
       │  2. Boot menu│  (v0.8.0+)
       │  3. Load KRNL│
       │  4. Jump     │
       └──────┬───────┘
              │  Jumps to 0x5000 + MNEX_HDR_SIZE (kernel entry)
              v
          KERNEL.SYS (or KERNELD.SYS)
              │
              ├── Loads FS.SYS at 0x0800 + relocates  ← overwrites LOADER
              ├── Loads MM.SYS + SHELL.SYS sequentially + relocates each
              └── Jumps to shell entry (from v2 header)
```

### Position in the Boot Info Block

The **Boot Info Block (BIB)** is a shared data structure at 0x0600, populated
by successive boot stages.  LOADER writes two fields:

| Offset | Size  | Field        | Writer  | Description                         |
|--------|-------|--------------|---------|-------------------------------------|
| 0x0600 | 1     | `boot_drive` | VBR     | BIOS drive number (DL from MBR)     |
| 0x0601 | 1     | `a20_status` | LOADER  | 1 = A20 enabled, 0 = failed         |
| 0x0602 | 4     | `part_lba`   | VBR     | Partition start LBA                 |
| 0x0606 | 1     | `boot_mode`  | LOADER  | 0 = release, 1 = debug *(v0.8.0+)* |

---

## 3. Memory Layout During LOADER Execution

```
Address         Contents                    Notes
──────────────  ─────────────────────────── ──────────────────────────
0x0000–0x03FF   IVT (256 entries × 4 B)     BIOS interrupt vectors
0x0400–0x04FF   BIOS Data Area (BDA)        BIOS working memory
0x0500–0x05FF   Reserved                    Free
0x0600–0x0605   Boot Info Block (BIB)       6 bytes, shared state
0x0606          BIB: boot_mode              (v0.8.0+)
0x0607–0x07FF   Free                        Unused gap

0x0800–0x0FFF   ██ LOADER.SYS ██            ← we are here (3 sectors)
0x1000–0x27FF   (available for growth)      LOADER can grow to 8 KB

0x3000–0x4FFF   Scratch buffer              Used as temp buffer for
                (DIR_SCRATCH_BUF=0x4E00)    MNFS directory read
0x5000–0x6FFF   Kernel load target          KERNEL.SYS loaded here
0x7C00–0x7DFF   VBR (still in memory)       Not used by LOADER
0x7E00–0x9FBFF  Stack + free                Stack grows down from ~0x7C00
```

### Scratch Buffer Strategy

The LOADER needs a 512-byte buffer to read the MNFS directory.  It uses
DIR_SCRATCH_BUF (0x4E00) — a region within the module area that will be
overwritten when the kernel loads system modules.  This is safe because the
loader is finished before the kernel begins module loading.

---

## 4. MNEX Binary Header

Every loadable binary in mini-os begins with a 6-byte MNEX header:

```
Offset  Size  Field           Example
──────  ────  ─────────────   ──────────────
0       4     Magic signature 'MNLD' (LOADER)
                              'MNKN' (KERNEL / KERNELD)
                              'MNFS' (FS / FSD)
                              'MNEX' (SHELL / SHELLD)
4       2     Sector count    2 (LOADER), 7 (KERNEL release)
6       ...   Code entry      First instruction
```

The loader validates the kernel's magic (`MNKN`) after reading it from disk.
The entry point is always at offset 6, so the loader jumps to
`KERNEL_OFF + MNEX_HDR_SIZE` (0x5006).

---

## 5. A20 Gate Enablement

### 5.1 Why A20 Matters

The 8086 had 20 address lines (A0–A19), limiting addressable memory to 1 MB.
When the 80286 added A20, IBM wired it through the keyboard controller so
8086 programs that depended on address wrap-around would still work.  The
"A20 gate" enables or disables this 21st address line.

Mini-OS enables A20 for future use (protected mode, accessing memory above
1 MB).  Even in real mode, having A20 enabled prevents confusing address
aliasing bugs.

### 5.2 Three-Method Fallback

| Method | Mechanism                      | Ports    | Compatibility       |
|--------|--------------------------------|----------|---------------------|
| 1      | BIOS INT 15h AX=2401h          | —        | Most modern BIOSes  |
| 2      | Keyboard controller (8042)     | 0x64/60  | Classic AT machines  |
| 3      | Fast A20 via port 0x92         | 0x92     | Many chipsets        |

After each method, the loader calls `check_a20` (wrap-around test at
0x0000:0x0500 vs 0xFFFF:0x0510) to verify success.

### 5.3 A20 Verification Algorithm

```
1. Save original bytes at [0x0000:0x0500] and [0xFFFF:0x0510]
2. Write 0x13 to [0xFFFF:0x0510]
3. Write 0x37 to [0x0000:0x0500]
4. Read [0xFFFF:0x0510]:
   - If 0x37 → addresses aliased → A20 disabled (ZF=1)
   - If 0x13 → addresses independent → A20 enabled (ZF=0)
5. Restore original bytes
```

### 5.4 Failure Handling

If all three methods fail, the loader prints a warning but **continues
booting**.  The BIB `a20_status` field is set to 0, and the kernel can
query it later via `SYS_CHECK_A20`.

---

## 6. Boot Menu (v0.8.0 — Implemented)

### 6.1 Design Overview

The boot menu presents the user with a choice of kernel configurations.
After selection, the loader stores the choice in `BIB_BOOT_MODE` and loads
the corresponding kernel variant.

**Current implementation (v0.8.0):** The loader presents a hardcoded two-entry
menu and loads the selected kernel.  Both release and debug variants are on
the same disk.  A single `build.bat` produces one unified VHD.

### 6.2 Menu Display

After A20 enablement and before loading the kernel, the loader clears the
screen and displays:

```
  ┌──────────────────────────────────┐
  │  MNOS Boot Manager               │
  │                                   │
  │  1) MNOS [Release]                │
  │  2) MNOS [Debug]                  │
  │                                   │
  │  Press 1 or 2:                    │
  └──────────────────────────────────┘
```

The user presses `1` or `2`.  Any other key re-prompts.  If no key is
pressed within a configurable timeout *(future enhancement)*, the default
entry (Release) is selected.

### 6.3 File Naming Convention

Each boot-time module has a release and debug variant with distinct 8.3
filenames on the MNFS disk:

| Module   | Release filename | Debug filename   | MNEX Magic |
|----------|------------------|------------------|------------|
| Kernel   | `KERNEL  SYS`    | `KERNELD SYS`    | `MNKN`     |
| FS       | `FS      SYS`    | `FSD     SYS`    | `MNFS`     |
| Shell    | `SHELL   SYS`    | `SHELLD  SYS`    | `MNEX`     |
| Loader   | `LOADER  SYS`    | *(shared)*       | `MNLD`     |

The **Loader is not duplicated** — it is the same binary for both
configurations.  The VBR always loads `LOADER  SYS`.

The `D` suffix convention (KERNELD, FSD, SHELLD) keeps the names
recognizable while fitting within the 8-character limit.

### 6.4 Boot Mode Propagation

The user's selection flows through the boot chain via the BIB:

```
  LOADER                        KERNEL                        SHELL
  ──────                        ──────                        ─────
  User presses "2"              Reads BIB_BOOT_MODE           Reads BIB_BOOT_MODE
  → BIB_BOOT_MODE = 1           → boot_mode == 1?            (via SYS_GET_BIB)
  → Load KERNELD.SYS             → Load FSD.SYS               → Banner shows
                                  → Load SHELLD.SYS              "[Debug]" or
                                                                 "[Release]"
```

The kernel reads `BIB_BOOT_MODE` at startup and selects filenames accordingly:

```nasm
; Pseudocode (in kernel entry)
cmp byte [BIB_BOOT_MODE], 1
je .load_debug_fs
    mov si, fname_fs            ; 'FS      SYS'
    jmp .do_load_fs
.load_debug_fs:
    mov si, fname_fsd           ; 'FSD     SYS'
.do_load_fs:
    call find_file
```

### 6.5 Shell Banner Changes

The shell banner currently shows:

```
  MNOS v0.7.5
```

After v0.8.0, the banner will show the build variant:

| Build mode | Banner display          |
|------------|-------------------------|
| Release    | `MNOS v0.8.0 [Release]` |
| Debug      | `MNOS v0.8.0 [Debug]`   |

The shell determines which string to print by reading `BIB_BOOT_MODE`:

```nasm
; In shell_init, after printing the base banner:
mov ah, SYS_GET_BIB         ; Get BIB address
int 0x80                    ; ES:BX = 0x0000:0x0600
cmp byte [es:bx + 6], 1    ; BIB_BOOT_MODE offset
je .debug_banner
    mov si, msg_release_tag ; " [Release]"
    jmp .print_tag
.debug_banner:
    mov si, msg_debug_tag   ; " [Debug]"
.print_tag:
    mov ah, SYS_PRINT_STRING
    int 0x80
```

Similarly, the `ver` command will append `[Debug]` or `[Release]` to its
output.

### 6.6 Disk Layout (v0.8.0)

With both variants on disk, the MNFS directory holds 7 files:

```
MNFS Directory (partition sector 2):
  Entry  Name          Attr    Start   Sectors   Description
  ─────  ───────────   ────    ─────   ───────   ─────────────────────
  0      LOADER  SYS   SYS     3       3         Stage-2 loader (shared)
  1      FS      SYS   SYS     6       2         Filesystem (release)
  2      KERNEL  SYS   SYS     8       7         Kernel (release, 3.5 KB)
  3      SHELL   SYS   EXEC    15      12        Shell (release, 6 KB)
  4      FSD     SYS   SYS     27      4         Filesystem (debug)
  5      KERNELD SYS   SYS     31      11        Kernel (debug, 5.5 KB)
  6      SHELLD  SYS   EXEC    42      12        Shell (debug, 6 KB)

  Total: 7 entries (max 15), 52 data sectors
```

*(Exact sector offsets will vary as files are packed contiguously by
`create-disk.ps1`.)*

### 6.7 Build Pipeline Changes (v0.8.0)

The `build.bat /debug` flag is **removed**.  Instead, `build.ps1` always
assembles both variants and produces a single unified disk image:

```
build.ps1 execution flow:
  1. Assemble shared binaries (MBR, VBR, LOADER)
  2. Assemble release binaries (FS, KERNEL, SHELL) — no -dDEBUG
  3. Assemble debug binaries (FS, KERNEL, SHELL) — with -dDEBUG
  4. create-disk.ps1 packs all 7 files + VBR into one image
  5. create-vhd.ps1 converts to a single VHD
```

Output: one `mini-os.vhd` containing both variants.  No more
`mini-os-debug.vhd`.

---

## 7. Error Handling

### 7.1 Kernel Not Found

If `find_file` fails (CF set), the loader calls `boot_fail` which:
1. Prints `[FAIL] KERNEL.SYS` (or `KERNELD.SYS`) to screen
2. Dumps all registers (via `boot_msg.inc` with `BOOT_REGDUMP`)
3. Halts the CPU (`cli` + `hlt` loop)

### 7.2 Kernel Magic Mismatch

If the loaded binary doesn't start with `MNKN`, `load_mnex` sets CF and the
loader falls through to the same `boot_fail` path.

### 7.3 Kernel Too Large

The `load_mnex` function takes a maximum sector count parameter (currently 16
sectors = 8 KB).  If the kernel's MNFS entry claims more sectors than this
limit, the load fails with CF set.

---

## 8. Shared Subroutines

The loader includes three shared modules from `src/include/`:

| Include           | Functions           | Purpose                          |
|-------------------|---------------------|----------------------------------|
| `find_file.inc`   | `find_file`         | MNFS directory scan by filename  |
| `load_binary.inc` | `load_mnex`         | Load + validate MNEX binary      |
| `boot_msg.inc`    | `boot_ok` `boot_fail` | Linux-style `[  OK]` / `[FAIL]` |

These are textually included by NASM and assembled into the LOADER binary.
The same code is also included in the kernel and VBR.

---

## 9. Future Enhancements

### 9.1 Configuration File Boot Menu (v1.0+ candidate)

**Current approach** (v0.8.0): Boot menu entries are hardcoded in the LOADER
assembly source.  Adding or changing entries requires reassembling.

**Future approach**: A text configuration file (`BOOT.CFG`) stored on the MNFS
disk defines the menu entries.  The loader reads and parses this file at boot
time.

#### BOOT.CFG Format (Draft)

```ini
# MNOS Boot Configuration
# Lines starting with # are comments
# Format: label=kernel,fs,shell

default=1
timeout=5

Release=KERNEL  SYS,FS      SYS,SHELL   SYS
Debug=KERNELD SYS,FSD     SYS,SHELLD  SYS
```

#### Implementation Requirements

| Requirement                    | Complexity | Notes                       |
|--------------------------------|------------|-----------------------------|
| MNFS file lookup for BOOT.CFG  | Low        | Reuse existing `find_file`  |
| Text parser (line-by-line)     | Medium     | ~80 lines of string logic   |
| Field extractor (split on `=`,`,`) | Medium | ~50 lines                  |
| Dynamic menu rendering         | Low        | ~30 lines (print entries)   |
| Timeout with BIOS tick counter | Medium     | INT 1Ah for timer           |
| Error handling for bad config  | Medium     | Graceful fallback to first  |
| Sector budget impact           | Low→Medium | LOADER grows to 3–4 sectors |

**Total estimate**: ~200 lines of new assembly, LOADER grows from 2 to 3–4
sectors.

**Why defer**: The hardcoded approach costs ~40 lines and stays in 2 sectors.
A config file parser is worth implementing when:
- Third-party kernels or test kernels need to be bootable
- Users want to add entries without rebuilding
- The project reaches a scale where rebuild-to-add-entry is a real friction

### 9.2 Boot Timeout with Default Selection

A countdown timer on the boot menu that auto-selects the default entry after
N seconds of inactivity.  Uses BIOS tick counter (INT 1Ah AH=00h, ~18.2
ticks/sec).

### 9.3 Chainloading External Loaders

Load and jump to an arbitrary binary from the MNFS directory, enabling
alternative kernels or diagnostic tools to be booted.

---

## 10. Source File Reference

### 10.1 Current (v0.7.5)

```
src/loader/
  └── loader.asm        Monolithic source (A20 + kernel load + data + padding)
```

### 10.2 Planned (v0.8.0)

```
src/loader/
  └── loader.asm        Manifest with boot menu logic added (~30 lines growth)
```

The loader is small enough (~250 lines) that splitting into includes is not
warranted at this time.

### 10.3 Key Labels

| Label            | Description                                    |
|------------------|------------------------------------------------|
| `loader_magic`   | MNEX header: `'MNLD'`                          |
| `loader_sectors` | MNEX header: sector count                      |
| `loader_start`   | Entry point (jumped to from VBR)               |
| `enable_a20`     | A20 gate enablement with 3-method fallback     |
| `check_a20`      | A20 verification (wrap-around test)            |
| `load_kernel`    | MNFS lookup + load + jump to kernel            |
| `boot_menu`      | Boot menu display + selection *(v0.8.0+)*      |
| `fname_kernel`   | 8.3 filename `'KERNEL  SYS'`                   |
| `fname_kerneld`  | 8.3 filename `'KERNELD SYS'` *(v0.8.0+)*       |

### 10.4 Build Command

```
nasm -f bin -I src/include/ -I src/loader/ -o build/boot/LOADER.SYS src/loader/loader.asm
```

---

## 11. Revision History

| Version | Changes                                                       |
|---------|---------------------------------------------------------------|
| v0.3.0  | Initial LOADER — A20 gate + kernel load                       |
| v0.6.0  | MNFS integration — kernel found via directory lookup          |
| v0.7.0  | Linux-style boot messages (`[  OK]` / `[FAIL]`)              |
| v0.7.5  | Build script adds `-I src/loader/` include path               |
| v0.8.0  | *(Planned)* Boot menu — dual-boot release/debug from one disk |
| v1.0+   | *(Future)* BOOT.CFG file-based menu configuration             |
