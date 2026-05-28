# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

---

## [0.9.16] - 2026-05-28

### Fixed
- **Nested SYS_SPAWN crash** — spawning multiple levels deep (e.g., mnmon→mnmon→edit)
  then exiting back caused `#UD Invalid Opcode` because each nested spawn
  overwrote `spawn_saved_ret` with the trampoline address instead of preserving
  the original shell return address.  The outermost spawn now installs the
  trampoline exactly once; nested spawns skip trampoline setup entirely.
- **Spawn failure leaves corrupt state** — if SYS_SPAWN's child file lookup
  failed (file not found, not executable, too large), the spawn depth and
  trampoline were already committed.  The caller's next EXIT would incorrectly
  enter the reload path.  Added `spawn_rollback_if_pending` which undoes
  depth increment and trampoline installation on pre-load errors.
- **Trampoline not re-installed after nested unwind** — when a nested parent
  was reloaded (depth > 0 after decrement), the trampoline word at
  `[SHELL_SAVED_SP]` was destroyed by the `int 0x80` FLAGS push.  The reload
  path now re-installs the trampoline for the next unwind level.

### Changed
- **SYS_SPAWN now supports nesting** — replaced single `spawn_parent_fname`
  with a depth-indexed `spawn_parent_stack` (4 levels × 11 bytes).
  `spawn_depth` tracks current nesting level.
- **SYS_EXIT reload logic** — uses `spawn_depth` as index into parent stack;
  distinguishes outermost (restore shell ret) vs. still-nested (re-install
  trampoline) cases.
- `kernel_data.inc` — replaced `spawn_parent_fname` with `spawn_parent_stack`
  (44 bytes), `spawn_depth`, and `spawn_pending` flag.
- `doc/SYSTEM-CALLS.md` — updated SYS_SPAWN semantics for nesting and rollback.
- `doc/PROGRAM-LOADER.md` §6.4 — rewritten to describe stack-based spawn model.
- Version bumped to 0.9.16.

---

## [0.9.15] - 2026-05-26

### Added
- **SYS_EXEC syscall (AH=0x27)** — allows a running program to replace itself
  with another .MNX program (overlay exec). New program inherits shell return
  frame and arguments. Caller is destroyed on success; returns CF+error code
  on failure (file not found, not executable, too large).
- **SYS_SPAWN syscall (AH=0x28)** — spawns a child program and reloads the
  caller from disk when the child exits. One-shot: parent restarts fresh with
  no state preserved. Enables debugger/monitor patterns (MNMON `x` command).
- **MNMON `x` command uses SYS_SPAWN** — launches a program, returns to MNMON
  when it finishes (MNMON survives across child execution).
- **Kernel `exec_parse_args`** — kernel-local argument tokenizer for SYS_EXEC,
  identical semantics to shell_parse_args but operates on kernel scratch space.
- **Kernel exec scratch data** — `exec_fname_buf` (11 bytes), `exec_args_buf`
  (128 bytes), `exec_entry_addr`, `spawn_parent_fname` (11 bytes).
- **17 new unit tests** for exec_parse_args and the SYS_EXEC binary contract
  (total: 234 tests).

### Changed
- `syscalls.inc` — added SYS_EXEC (0x27), SYS_SPAWN (0x28), bumped SYSCALL_MAX to 0x28.
- `kernel_syscall.inc` — added .fn_exec, .fn_spawn handlers; modified .fn_exit
  to check spawn_parent_fname and reload parent if set.
- `kernel_data.inc` — added exec scratch space + spawn_parent_fname (154 bytes).
- `programs/mnmon.asm` — `x` command now uses SYS_SPAWN (was SYS_EXEC);
  added `mnmon_fname` data for parent reload.
- `doc/SYSTEM-CALLS.md` — documented SYS_EXEC and SYS_SPAWN interfaces.
- `doc/ABI.md` — added §Program-to-Program Execution section.
- `doc/PROGRAM-LOADER.md` — added §6.3 SYS_EXEC specification.

---

## [0.9.14] - 2026-05-26

### Added
- **Relocatable system modules** — FS.SYS, MM.SYS, SHELL.SYS now assembled
  with `[ORG 0]` and relocated at load time by the kernel. Modules are packed
  sequentially from 0x0800 upward; no more hardcoded offsets.
- **Relocatable user programs** — EDIT.MNX, MNMON.MNX, SYSINFO.MNX also
  converted to ORG 0 with v2 headers. Shell applies relocations before
  execution — programs are now binary-portable across OS versions.
- **MNEX v2 header format** — 12-byte header with magic, sector count, flags,
  reloc count, entry offset, and relocation table. Used by both system modules
  and user programs.
- **`tools/gen_relocs.py`** — Delta-comparison relocation table generator.
  Assembles module at ORG 0 and ORG 0x100, compares byte-by-byte to find
  absolute references.
- **`tools/pack_module.py`** — Module packager. Takes raw binary + reloc table,
  pre-biases absolute references, produces final binary with v2 header.
- **`doc/ABI.md`** — Formal Application Binary Interface contract guaranteeing
  binary portability for .MNX programs across OS versions.
- **Kernel `apply_relocs` subroutine** — reads v2 reloc table, patches each
  16-bit word by adding the module's actual load base address.
- **Shell relocation patching** — shell_cmd_run.inc detects v2 flag on loaded
  programs, applies relocation table, computes entry from header. Backward
  compatible with legacy v1 programs.
- **`Build-RelocModule` function** in build.ps1 — integrates gen_relocs +
  pack_module into the standard build pipeline for both .SYS and .MNX.
- **35 relocation tests** — `test_relocation.py` covering gen_relocs,
  pack_module, apply_relocs simulation, built-module validation, built-program
  validation, shell reloc logic, and legacy v1 fallback.
- **Boot menu keyboard fix** — defensive 8042 re-enable + buffer flush before
  INT 16h to prevent input hang on some Hyper-V configurations.

### Changed
- **Dynamic module placement** — kernel tracks `next_base` and places modules
  sequentially instead of at fixed addresses. Validates total doesn't exceed
  kernel area (0x5000).
- **memory.inc refactored** — removed `SHELL_OFF`, `SHELL_SEG`, `MM_OFF`,
  `MM_SEG`, `MM_MAX_SECTORS`; added `MODULE_FIRST_BASE`, `MODULE_AREA_END`,
  `DIR_SCRATCH_BUF`.
- **Memory layout tests rewritten** — new `TestModuleAreaLayout` class validates
  dynamic constants and module-area invariants.
- Total unit tests: 176 → 196

### Removed
- Fixed module offsets in memory.inc (replaced by dynamic placement)
- Hardcoded sector padding in FS/MM/SHELL (now handled by pack_module.py)
- Module headers inline in source (now generated by pack_module.py)

---

## [0.9.13] - 2026-05-26

### Added
- **SYSINFO.MNX** — standalone user program (6 sectors, 3 KB) displaying 5 pages
  of system information (CPU/CPUID, memory/E820, BDA, video/disk/EDD, IVT).
  Previously a built-in shell command; now loaded via implicit execution.
- **Memory layout consistency tests** — 16 new tests in `test_memory_layout.py`
  validating component non-overlap, stack bounds, TPA placement, and metadata
  positioning. Catches future layout mistakes at build time.

### Changed
- **`sysinfo` extracted from shell** — no longer a built-in command; type
  `sysinfo` at the prompt to run `SYSINFO.MNX` (same UX, smaller shell)
- **SHELL.SYS shrunk** from 19 → 13 sectors (freed 3 KB by removing sysinfo)
- **Memory layout tightened** — KERNEL.SYS relocated from 0x5800 to 0x5000;
  stack canary moved from 0x7400 to 0x6C00; usable stack doubled from ~2 KB
  to ~4 KB; eliminates dead space between shell and kernel
- Shell max allocation reduced from 20 to 16 sectors (still 3 sectors of
  headroom above current 13)
- Total unit tests: 160 → 176

---

## [0.9.12] - 2026-05-22

### Added
- **EDIT.MNX — Full-screen text editor** — DOS EDIT.COM-style editor loaded as
  a standalone MNEX binary (13 sectors, 6.5 KB):
  - **Gap buffer** data structure for O(1) insert/delete at cursor
  - **Menu bar** (File / Edit / Search) with drop-down navigation and hotkey
    highlighting (first letter in red indicates Alt+key accelerator)
  - **Cut/Copy/Paste** with 512-byte clipboard (Ctrl+X/C/V)
  - **Find** (Ctrl+F), **Find Next** (F3), **Replace** (Ctrl+H, single),
    **Replace All** (F4, all occurrences from cursor to end)
  - **Go to Line** (Ctrl+G)
  - **Block selection** (Shift+arrow keys)
  - **Modal dialog boxes** — Find, Replace, Go to Line, and Save-As use a
    centered 4-row modal dialog (title bar, input field, Enter/Esc hint)
  - **File picker** — Open command uses a scrollable file list dialog
  - **File load/save** via INT 0x81 (FS_READ_FILE / FS_WRITE_FILE)
  - **Status bar** showing filename, Ln:Col, modified flag, INS/OVR mode
  - **Help screen** (F1)
  - Insert/Overwrite toggle (Insert key)
  - Tab expansion (8-column stops)
  - Ctrl+Home/End for start/end of file, PgUp/PgDn for page scroll
  - Alt+X to exit with save prompt if modified
- **doc/EDITOR.md** — comprehensive design document for EDIT.MNX
- Launched via implicit execution: type `edit` or `edit MYFILE.TXT` at prompt

### Changed
- **Project rename**: all internal references changed from "mini-os" to "MNOS16"
  - VM name: `MNOS16` (was `mini-os`)
  - VHD output: `MNOS16.vhd` (was `mini-os.vhd`)
  - Raw image: `MNOS16.img` (was `mini-os.img`)
  - COM pipe: `\\.\pipe\MNOS16-SERIAL` (was `\\.\pipe\minios-serial`)
  - VM path default: `C:\HyperV\MNOS16`
  - Build banner: `[MNOS16]`
- HELLO.MNX removed from VHD (source remains as example)

---

## [0.9.11] - 2026-05-19

### Added
- **MNFS write support** — three new filesystem syscalls via INT 0x81:
  - `FS_WRITE_FILE` (AH=0x06): Write/create a file (DS:SI=name, ES:BX=data,
    ECX=size). Returns CF=0 success or CF=1 + AL=error code.
  - `FS_DELETE_FILE` (AH=0x07): Delete a file by name (tombstone-based,
    name[0]=0xE5). System files protected (FS_ERR_PROTECTED).
  - `FS_RENAME_FILE` (AH=0x08): Rename a file (DS:SI=old, ES:DI=new).
    Fails if destination already exists.
- **Error code system** — structured error reporting for write operations:
  FS_ERR_NOT_FOUND (1), FS_ERR_EXISTS (2), FS_ERR_DIR_FULL (3),
  FS_ERR_DISK_FULL (4), FS_ERR_IO (5), FS_ERR_PROTECTED (6)
- **Shell `copy` command** — copy a file (`copy SRC.EXT DST.EXT`); reads
  source into TPA buffer, writes with new name; clears system attribute on copy
- **Shell `del` command** — delete files from the command line
  (`del FILENAME.EXT`)
- **Shell `ren` command** — rename files from the command line
  (`ren OLD.EXT NEW.EXT`)
- **`cmdmatch` routine** — prefix-based command dispatcher for commands with
  arguments (matches command name followed by space or NUL)
- **Unit tests** — 38 new tests: 26 in `test_fs_write.py` (write/delete/rename,
  95% branch coverage), 12 in `test_cmdmatch.py` (100% branch coverage)

### Changed
- `FS.SYS` binary size: release 3→5 sectors, debug 5→8 sectors (write support)
- `SHELL.SYS` binary size: 16→18 sectors (copy + del + ren commands)
- `dir` command updated to skip tombstoned entries (name[0]=0xE5)
- `total_sectors` in directory header recalculated as high-water mark after
  delete (does not shrink to fill gaps)
- Emulator harness extended with 32-bit register support (eax/ebx/ecx/edx/esi/edi)
- Total unit tests: 64→102

---

## [0.9.10] - 2026-05-18

### Added
- **HMA (High Memory Area) heap** — dynamic memory allocation moved from 4 KB
  conventional heap to ~64 KB in HMA (segment FFFF:0010–FF00):
  - A20 alias detection at MM init (no fallback — heap disabled if A20 fails)
  - ES-based heap access (DS remains 0 for interrupt safety)
  - MEM_ALLOC now returns AX=segment + BX=offset (callers use ES:BX)
  - New `MEM_QUERY` syscall (AH=0x05): returns heap segment/start/size
  - CLI guards around ES manipulation for interrupt safety
  - 256-byte guard zone at top of HMA to prevent 16-bit wrap bugs
- **TPA expanded** — 26 KB → 30 KB (0x8000–0xF7FF) by reclaiming old heap space
- **Shell `mem` command** — shows heap type ("HMA ~64 KB" or "Conventional 4 KB"),
  pauses before heap section, uses MEM_QUERY + ES: for block walk
- **MNMON `mcb` command** — uses MEM_QUERY + ES: for HMA-aware MCB walk,
  displays heap segment
- **Auto-generated `constants.py`** — `tests/gen_constants.py` extracts NASM
  `equ` definitions from `.inc` files; eliminates manual sync
- **doc/MEMORY-MANAGER.md** updated to v3.0 with HMA architecture

### Changed
- `MM.SYS` binary size: release 1→2 sectors, debug 2→3 sectors
- `MNMON.MNX` binary size: 4→5 sectors (HMA-aware mcb command)
- `USER_PROG_BASE` moved from 0x9000 to 0x8000 (TPA starts earlier)
- `USER_PROG_MAX` increased from 0x6800 (26 KB) to 0x7800 (30 KB)
- `MEM_SYSCALL_MAX` bumped from 0x04 to 0x05
- All MM handlers now use ES: segment override for heap access
- Conventional heap fallback removed (A20 failure = no heap, not 4 KB fallback)

---

## [0.9.9] - 2026-05-15

### Added
- **Unit test framework** — Python + Unicorn Engine testing infrastructure for
  16-bit x86 assembly routines (no QEMU or hardware required):
  - 64 tests across 4 modules (shell_parse_args, run_parse_filename, strcmp, mm_allocator)
  - Statement + branch coverage reporting (Capstone disassembly of conditional jumps)
  - Historical trend tracking (Chart.js graph, last 50 CI runs)
  - CI/CD integration (new `test` job in build.yml, coverage deployed to GitHub Pages)
  - See doc/TESTING.md for the 3-tier test strategy design
- **MM allocator unit tests** — 27 tests covering mm_alloc (first-fit, splits,
  word alignment, OOM), mm_free (validation, forward coalescing), mm_avail
  (fragmentation reporting), and mm_info (block counting)
- **doc/TESTING.md** — unit test framework design document covering the 3-tier
  test strategy (Tier 1: pure logic with Unicorn; Tier 2: syscall hooks; Tier 3:
  QEMU integration)

---

## [0.9.8] - 2026-05-15

### Added
- **Layer 2: Parsed Arguments (argc/argv)** — shell now tokenizes the command
  line into structured arguments before launching programs:
  - `SYS_GET_ARGC` (AH=0x25) — returns argument count in CL
  - `SYS_GET_ARGV` (AH=0x26) — returns pointer to Nth argument (SI) and
    length (CX); sets CF if index is out of bounds
  - Double-quoted strings are treated as a single argument (quotes stripped)
  - Maximum 15 arguments, ~200 bytes total argument storage
- **`shell_parse_args.inc`** — new shell module that parses the raw argument
  string into the argv table at 0x7F00
- **ARGV memory region** (0x7F00–0x7FFB) — structured argc + pointer table +
  NUL-separated string storage
- **Backward compatible** — `SYS_GET_ARGS` (AH=0x24) still returns the raw
  argument string unchanged
- **doc/COMMAND-LINE.md** — 5-layer command-line expansion design document

### Changed
- `SYSCALL_MAX` bumped from 0x24 to 0x26

---

## [0.9.7] - 2026-05-15

### Added
- **MNMON.MNX** — interactive machine monitor (WinDbg-style commands):
  `db` (display bytes with ASCII), `dw` (display words), `eb` (enter bytes),
  `ew` (enter words), `g` (call address).  Standalone user program (3 sectors).
- **doc/MNMON.md** — full design specification for the monitor

---

## [0.9.6] - 2026-05-15

### Added
- **Program Loader** — any unrecognized shell command is treated as a program
  name; loads `.MNX` user programs into 26 KB TPA at 0x9000.  Extension is
  optional — typing `hello` finds and runs `HELLO.MNX` automatically.
- **FS_FIND_BASE (AH=0x05)** — new FS syscall searches MNFS directory by
  8-byte base name only (ignoring extension), writes found extension back to
  caller's buffer for subsequent FS_READ_FILE
- **HELLO.MNX** — first user-mode demo program (Hello, world!)
- **SYS_EXIT (0x23)** — new syscall to terminate a program from any call depth;
  restores shell SP from SHELL_SAVED_SP
- **SYS_GET_ARGS (0x24)** — new syscall returning command-line argument pointer
- **Four-layer validation** — file existence, ATTR_SYSTEM check, ATTR_EXEC
  check, and MNEX magic validation before execution
- **SHELL_SAVED_SP / SHELL_ARGS_PTR** — ABI slots at 0x7FFE/0x7FFC
- **INT nesting depth counter** (`BIB_INT_DEPTH` at 0x0607) — shared byte
  tracks total INT 0x80 / INT 0x81 nesting depth, displayed as `D=xx` in
  all syscall entry/exit traces
- **DAP hex dump** — full 16-byte Disk Address Packet printed to serial
  before every INT 0x13 call (both kernel and FS paths)
- **`syscall_iret` macro** — replaces all 25 bare `iret` in kernel dispatcher;
  decrements depth counter before returning
- **`syscall_ret_cf` macro** — same for CF-returning syscalls via `retf 2`
- **FS error traces** — `[FS] DAP: ...`, `[FS] INT13 ERR AH=xx`,
  `[FS] RF: not_found` diagnostics
- **doc/PROGRAM-LOADER.md** — program loader design document

### Fixed
- **EDI-clobbers-DI bug in FS read_file** — `mov edi, [es:di + START]`
  destroyed DI before `mov cx, [es:di + SECTORS]`, causing garbage sector
  counts (up to 248) that crossed the 64 KB DMA boundary (BIOS error AH=09).
  Fix: read CX (sectors) before EDI (start LBA).

### Changed
- **Heap reduced** from 30 KB to 4 KB (0x8000-0x8FFF) to make room for 26 KB TPA
- **FS_FIND_FILE** now returns attribute byte in BL (ABI extension)
- **FS.SYS** grown from 2 to 3 sectors (release), 4 to 5 sectors (debug)
  for FS_FIND_BASE implementation
- **SHELL.SYS** grown from 14 to 16 sectors (program execution + parsing)
- **Kernel debug build** grown from 12 to 14 sectors (DAP dump + trace code)
- **Syscall trace format** now includes `D=xx` depth and `CF=x IF=x` flags
- **Shell `mem` command** updated to show 4 KB heap + 26 KB TPA layout
- **Kernel version** bumped to 0x0906
- **DEBUGGING.md** updated to v1.4 with §4.8 (INT depth & DAP diagnostics)
- **MEMORY-LAYOUT.md** BIB table updated with `int_depth` field

---


## [0.9.5] - 2026-05-14

### Changed
- **All system binaries renamed `.BIN` to `.SYS`** - LOADER.SYS, KERNEL.SYS,
  FS.SYS, MM.SYS, SHELL.SYS (and debug variants FSD.SYS, KERNELD.SYS,
  SHELLD.SYS, MMD.SYS).
- **MNFS directory entries** updated with new 8.3 filenames
- **Build pipeline** outputs `.sys` files instead of `.bin`
- **VBR** looks up `LOADER  SYS` instead of `LOADER  BIN`
- **Loader** looks up `KERNEL  SYS` / `KERNELD SYS`
- **Kernel** looks up `FS      SYS`, `MM      SYS`, `SHELL   SYS` (and debug variants)
- **All source comments** updated to use `.SYS` naming
- **Shell memory map** display shows `.SYS` names
- **Establishes `.MNX` convention** for future user-mode executables
- **Documented extension conventions** in DESIGN.md (new section 2.10)
- **Clarified SHELL.SYS rationale** - uses `.SYS` because it is kernel-loaded
  at boot into system memory; `.MNX` reserved for on-demand user programs
- **SHELL.SYS attribute** changed from `ATTR_EXEC` (0x02) to
  `ATTR_SYSTEM | ATTR_EXEC` (0x03); `dir` displays combined type as "SYS+X"
- **SHELL.SYS** 13 to 14 sectors (added combined-attribute display code)

---

## [0.9.2] — 2026-05-14

### Added
- **MCB owner tags** — flags byte bits 1-3 carry a 3-bit owner ID (0-7):
  kernel=1, fs=2, mm=3, shell=4, usr1-3=5-7.  `MEM_ALLOC` (AH=0x01) now
  accepts DL = owner ID.
- **Shell `mem` block detail walk** — walks the MCB chain and prints each
  block's address, size, status (used/free), and owner name.
- **MM debug trace shows owner** — alloc success log now includes `own=N`
  digit in serial output.
- **Owner ID constants** in `memory.inc` — `MCB_OWNER_MASK`, `MCB_OWNER_SHIFT`,
  `MCB_OWNER_KERN` through `MCB_OWNER_USR3`.
- **Owner name table** in shell — 8-entry word pointer table for display.

### Changed
- **MEM_ALLOC ABI** — input: CX = size, DL = owner (was: BX = size only).
  Output: BX = pointer (was: AX = pointer).
- **MEMORY-MANAGER.md** — documented owner ID table, updated §8.2 ABI, updated
  function summary table.

## [0.9.1] — 2026-05-14

### Added
- **Shell `mem` heap statistics** — displays total/used/free/blocks/largest via
  INT 0x82 MEM_INFO + MEM_AVAIL.
- **MM debug serial tracing** — debug builds log all INT 0x82 calls to COM1:
  function number on entry, alloc success/fail with size and pointer,
  free success/fail with pointer.
- **Memory layout update** — `mem` command map now shows MM.BIN at 0x2800
  and HEAP at 0x8000–0xF7FF.
- **`ver` command** — now shows "Memory: INT 0x82 heap (30 KB)" line.

### Changed
- **SHELL.BIN** 12→13 sectors (added heap stats code + strings)
- **DESIGN.md §2.1** — boot sequence diagram updated with MM.BIN step
- **DESIGN.md §2.6** — kernel load sequence lists MM.BIN (steps 4-5)
- **DESIGN.md §2.8** — new MM.BIN section with header format and link to spec
- **README.md** — added `doc/MEMORY-MANAGER.md` link in documentation section
- **`.gitignore`** — removed `doc/MEMORY-MANAGER.md` exclusion (now tracked)

---

## [0.9.0] — 2026-05-13

### Added
- **Memory Manager (MM.BIN)** — MNMM heap allocator at `0x2800`, providing
  dynamic memory allocation via `INT 0x82`.  Manages a 30 KB heap at
  `0x8000`–`0xF7FF` using MCB-style 4-byte block headers.
  - `MEM_ALLOC` (AH=0x01): First-fit allocation with word alignment
  - `MEM_FREE` (AH=0x02): Free with forward coalescing
  - `MEM_AVAIL` (AH=0x03): Query largest free block and total free memory
  - `MEM_INFO` (AH=0x04): Full heap statistics (total/used/free/block count)
- **`src/mm/mm.asm`** — new source file for MM.BIN (release: 1 sector,
  debug: 2 sectors)
- **MM constants in `memory.inc`** — `MM_OFF`, `HEAP_START`, `HEAP_END`,
  `MCB_*` header layout constants, `MEM_*` syscall numbers
- **Kernel MM load sequence** — kernel now loads and initializes MM.BIN
  between FS init and SHELL load; boot message "Memory manager (INT 0x82)"

### Changed
- **Kernel** release 7→8 sectors, debug 11→12 sectors (MM load code + strings)
- **MNFS directory** 7→9 files (added MM.BIN + MMD.BIN)
- **Disk layout** 51→57 total data sectors
- **Build pipeline** assembles MM.BIN and MMD.BIN; `create-disk.ps1` accepts
  `-MmPath` and `-MmDbgPath` parameters
- **Boot chain** now: MBR → VBR → LOADER → KERNEL → FS.BIN → MM.BIN → SHELL.BIN

---

## [0.8.1] — 2026-05-13

### Added
- **Stack canary** (debug only) — plants a 4-byte sentinel (0xDEAD × 2) at the
  stack floor (0x7000) during kernel init; verified on every syscall entry.
  Catches stack overflow before it silently corrupts kernel code/data.
  Fatal halt with diagnostic message on screen and serial if triggered.
- **`kernel_stack.inc`** — new source file in `src/kernel/` with `canary_init`,
  `canary_check`, and `CANARY_INIT`/`CANARY_CHECK` call-site macros.

### Changed
- **Debug kernel** grew from 10 → 11 sectors (canary code + strings ≈ 580 bytes)
- **Stack constants** in `memory.inc` — added `STACK_CANARY_ADDR`, `STACK_CANARY_VALUE`,
  `STACK_CANARY_SIZE` with detailed comments
- **Syscall handler** — `CANARY_CHECK` at entry (preserves all registers + FLAGS)
- Release builds unchanged (all canary macros expand to 0 bytes)

---

## [0.8.0] — 2026-05-13

### Added
- **Dual-boot menu** — LOADER presents a boot menu at startup:
  - `1) MNOS [Release]` / `2) MNOS [Debug]` — user selects kernel configuration
  - Both release and debug variants coexist on the same disk image
  - `BIB_BOOT_MODE` field (0x0606) propagates the selection to KERNEL and SHELL
- **Debug file variants on disk** — FSD.BIN, KERNELD.BIN, SHELLD.BIN alongside
  release variants; MNFS directory now has 7 entries
- **Shell boot mode tag** — banner and `ver` command show `[Release]` or `[Debug]`

### Changed
- **Unified build pipeline** — `build.bat` always builds both release and debug
  variants; removed `-DebugBuild` / `/debug` flag; single VHD output
- **LOADER.BIN** — grew from 2 to 3 sectors (menu code + strings)
- **Kernel conditional loading** — reads BIB_BOOT_MODE to select FS/FSD and
  SHELL/SHELLD filenames
- **create-disk.ps1** — accepts 7 MNFS files (3 shared + 3 release + 3 debug)

---

## [0.7.5] — 2026-05-13

### Changed
- **Source file split** — monolithic `kernel.asm` (1450 lines) and `shell.asm`
  (1582 lines) split into focused include files organized by functionality:
  - Kernel: `kernel_syscall.inc`, `kernel_data.inc`, `kernel_fault.inc`
  - Shell: `shell_cmd_simple.inc`, `shell_cmd_dir.inc`, `shell_cmd_mem.inc`,
    `shell_cmd_sysinfo.inc`, `shell_readline.inc`, `shell_data.inc`
- Build script now passes source directory as additional NASM include path (`-I`)
- Binary output is byte-identical to v0.7.4 (no functional changes)

## [0.7.4] — 2026-05-13

### Changed
- **CPU fault handlers now present in both release and debug builds** — a CPU
  exception in any build produces a full crash screen instead of silently looping:
  - Exception name (e.g., `#DE Divide Error`)
  - Faulting CS:IP address
  - Full register dump (AX, BX, CX, DX, SI, DI, BP, SP, DS, ES, SS, FLAGS)
  - Top 4 stack words
  - "System halted." message, then `cli; hlt`
- Debug builds additionally log all fault info to serial (COM1)
- **Master PIC (8259A) remapped**: IRQ 0–7 moved from INT 0x08–0x0F to
  INT 0x20–0x27, freeing INT 0x08 for #DF (Double Fault) exception handler.
  BIOS ISRs are copied to the new vectors transparently.
- 7 vectors now trapped: #DE, #DB, #OF, #BR, #UD, #NM, #DF
- KERNEL.BIN release sector count: 6 → 7 (fault handlers + PIC remap code)
- KERNEL.BIN debug sector count: 9 → 10 (serial output in fault_common)

---

## [0.7.3] — 2026-05-13

### Added
- **CPU exception fault handlers** (debug build only) — trap 7 exception
  vectors and produce a diagnostic dump instead of silently triple-faulting:
  - `#DE` (INT 0x00) — Divide Error
  - `#DB` (INT 0x01) — Debug / Single Step
  - `#OF` (INT 0x04) — Overflow
  - `#BR` (INT 0x05) — Bound Range Exceeded
  - `#UD` (INT 0x06) — Invalid Opcode
  - `#NM` (INT 0x07) — Device Not Available
  - `#DF` (INT 0x08) — Double Fault *(PIC remap added in v0.7.4)*
- On exception: prints `*** CPU EXCEPTION: <name>` with faulting CS:IP to
  both serial and screen, dumps all registers to serial, then halts (`cli; hlt`)
- `install_fault_handlers` subroutine called during kernel init (after INT 0x80)
- Screen hex display helper for fault address output

### Changed
- KERNEL.BIN debug sector count: 8 → 9 (fault handler code adds ~300 bytes)
- Release builds unchanged — all fault handler code under `%ifdef DEBUG`

---

## [0.7.2] — 2026-05-13

### Added
- **Assert macros** (`src/include/debug.inc`) — three compile-time assertion
  macros for fail-fast debugging in debug builds:
  - `ASSERT reg, cond, val, "msg"` — halt if a register comparison fails
  - `ASSERT_CF_CLEAR "msg"` — halt if carry flag is set after an operation
  - `ASSERT_MAGIC reg, 'XXXX', "msg"` — halt if 4-byte magic at [reg] mismatches
- **Strategic assert placements**:
  - Kernel: after FS.BIN load (magic check), after FS init (CF check), after
    SHELL.BIN load (magic check)
  - FS.BIN: after directory sector read (CF check), after directory magic
    validation
- `ASSERT_HAS_SCREEN` opt-in define — enables screen output on assertion failure
  for binaries that provide a `puts` subroutine (kernel has it, FS does not)
- All assertion failures dump full register state to serial via `DBG_REGS`
- On failure: logs to serial (+ screen if available), dumps registers, then
  `cli; hlt` — CPU halts permanently to prevent corrupted state propagation

### Changed
- FS.BIN debug sector count: 3 → 4 (assert code adds ~150 bytes)
- KERNEL.BIN debug sector count: 7 → 8 (assert code adds ~60 bytes)
- Release builds unchanged — all assert macros compile to 0 bytes

---

## [0.7.1] — 2026-05-13

### Added
- **User-mode debug syscalls** — three new INT 0x80 functions (AH=0x20–0x22)
  allow user-mode programs to emit debug output through the kernel's serial
  port without direct COM1 access:
  - `SYS_DBG_PRINT` (0x20) — print a tagged message: `[TAG] message`
  - `SYS_DBG_HEX16` (0x21) — print a tagged hex value: `[TAG] NNNN`
  - `SYS_DBG_REGS`  (0x22) — dump all registers with tag: `[TAG] AX=... DI=...`
- **Caller-supplied tag** — DS:BX points to a NUL-terminated tag string
  (e.g., `"SHL"`, `"FS"`).  If BX=0, defaults to `"USR"`.
- **Shell debug tracing** — shell.asm now emits `[SHL]` tagged debug messages
  at init, command dispatch (logs the typed command), and unknown-command path
- All debug syscall handlers are **no-ops in release builds** (zero overhead)

### Changed
- `SYSCALL_MAX` raised from 0x1B to 0x22 (jump table extended with gap
  entries 0x1C–0x1F pointing to `sc_unknown`)
- Syscall name table extended with `DBG_PRINT`, `DBG_HEX16`, `DBG_REGS`
  entries for serial trace output

---

## [0.7.0] — 2026-05-17

### Added
- **Serial debug logging** — COM1 output at 115200 baud, 8N1 via pure port I/O
  (`src/include/serial.inc`): `serial_init`, `serial_putc`, `serial_puts`,
  `serial_hex8`, `serial_hex16`, `serial_crlf`
- **Debug macros** (`src/include/debug.inc`): `DBG "msg"`, `DBG_REG "name", reg`,
  `DBG_REGS` — inline string + call pattern, zero bytes in release builds
- **Syscall tracing** — kernel INT 0x80 handler logs `[SYS] AH=xx AX=xxxx BX=xxxx`
  to serial for every syscall invocation (debug build only)
- **Filesystem tracing** — FS.BIN INT 0x81 handler logs `[FS] AH=xx` to serial
  for every filesystem syscall (debug build only)
- **Debug build mode** — `build.bat /debug` or `pwsh tools/build.ps1 -DebugBuild`
  passes `-dDEBUG` to NASM; all debug code compiles to zero bytes in release
- **Separate debug/release VHDs** — release builds produce `mini-os.vhd`, debug
  builds produce `mini-os-debug.vhd`; both can coexist in `build/boot/`
- **Boot milestone logging** — kernel prints serial messages at each init stage:
  serial init, INT 0x80 installed, FS.BIN loaded, INT 0x81 ready, SHELL.BIN loaded
- **Serial reader script** — `read-serial.bat` / `tools/read-serial.ps1` connects
  to the Hyper-V COM1 named pipe and streams debug output to the console

### Changed
- Build scripts (`tools/build.ps1`, `build.bat`) updated for `-DebugBuild` switch
- `setup-vm.ps1` now auto-configures COM1 as named pipe (`\\.\pipe\minios-serial`)
  on both new and existing VMs; prompts for VHD variant (release/debug) when both
  VHDs are present
- Kernel sector count: conditional 6 (release) / 7 (debug) via `%ifdef DEBUG`
- FS.BIN sector count: conditional 2 (release) / 3 (debug) via `%ifdef DEBUG`
- `serial.inc` placed at end of kernel.asm and fs.asm (after all code/data)
  to avoid polluting binary headers at offset 0
- Shell monitor command renamed from `mon` to `mnmon` in DEBUGGING.md
  (follows `mn` prefix convention: mnos, mnfs, mnex, mnmon)

---

## [0.6.0] — 2026-05-12

### Added
- **MNFS Flat Filesystem** — 1-sector directory table at partition sector 2,
  up to 15 files, 32-byte entries with 8.3 names, attributes, and size tracking
- **FS.BIN kernel module** (`src/fs/fs.asm`) — loaded at 0x0800, owns INT 0x81
  filesystem syscall interface with 4 functions:
  - `FS_LIST_FILES (0x01)` — copy cached directory to caller buffer
  - `FS_FIND_FILE (0x02)` — search by 8.3 name, return sector/size
  - `FS_READ_FILE (0x03)` — read file contents via kernel INT 0x80 disk I/O
  - `FS_GET_INFO (0x04)` — return FS version, file count, max entries, used/capacity sectors
- **`dir` shell command** — lists all files on disk with name, type, sectors, bytes,
  total size summary, and disk space statistics (used/free/total KB)
- **`find_file.inc`** — bootstrap directory lookup subroutine used by VBR, LOADER,
  and KERNEL to find files by name without hardcoded offsets
- **`mnfs.inc`** — shared constants for MNFS directory format, entry fields, and
  INT 0x81 syscall numbers
- **`doc/FILESYSTEM.md`** — complete MNFS specification (14 sections)
- **Linux-style boot messages** — `[OK]`/`[FAIL]` status indicators during boot
  with enhanced 12-register dump (AX-DX, SI/DI/SP/BP, DS/ES/SS/FL) on failure
- **MNFS_HDR_CAPACITY** — directory header field at offset 8 stores partition
  data capacity; stamped by `create-disk.ps1`, returned by `FS_GET_INFO`

### Changed
- **No more hardcoded disk offsets** — all binaries are located via MNFS directory
  lookup at boot time; adding or resizing a file requires no source code changes
- Boot chain now loads FS.BIN before SHELL: MBR → VBR → LOADER → KERNEL → FS.BIN → SHELL
- KERNEL loads FS.BIN at 0x0800 (reuses LOADER's memory), calls init (installs INT 0x81),
  then loads SHELL — both found via `find_file` directory search
- VBR finds LOADER.BIN via directory lookup (was hardcoded partition offset 4)
- LOADER finds KERNEL.BIN via directory lookup (was hardcoded partition offset 20)
- `create-disk.ps1` completely rewritten: packs files contiguously after directory
  sector, generates MNFS directory table automatically from binary sizes
- `build.ps1` assembles 6 binaries (added FS.BIN)
- `disk.inc` replaced by `mnfs.inc` (partition offsets eliminated)
- KERNEL.BIN grew from 4 to 6 sectors (added find_file.inc + fname strings)
- SHELL.BIN grew from 10 to 12 sectors (dir command + 512-byte directory buffer)
- Version banner updated to v0.6.0

### Fixed
- **AH register overlap bugs** — systemic class where `mov ah, SYS_xxx` clobbers
  bits 8-15 of AX/EAX when the same register holds data. Three instances fixed:
  - `SYS_READ_SECTOR` (0x04): LBA input moved from EAX to **EDI**
  - `SYS_PRINT_DEC16` (0x12): value input moved from AX to **DX**
  - `SYS_PRINT_HEX16` (0x11): value input moved from AX to **DX**
- **CF propagation through INT/IRET** — `iret` restores the caller's saved FLAGS,
  silently discarding the handler's carry flag. Created `syscall_ret_cf` macro
  (`sti; retf 2`) applied to 6 CF-returning kernel handlers
- **`dir` column alignment** — numeric columns now right-justified with leading
  spaces via `rjust_dec16` helper routine

## [0.5.0] — 2026-05-11

### Added
- **16-bit kernel** (`KERNEL.BIN`) — loaded by LOADER at 0x5000 (partition offset 20),
  installs an INT 0x80 syscall handler with 27 functions wrapping all BIOS services
- **INT 0x80 syscall interface** — shell no longer makes direct BIOS calls; all
  hardware access goes through kernel syscalls (AH = function number)
- **CPUID syscall (0x18)** — leaf passed via EDI to avoid conflict with AH dispatch byte
- **MNKN magic** — kernel binary self-identifies with 'MNKN' header (4 sectors / 2 KB)

### Changed
- Boot chain extended: MBR → VBR → LOADER → **KERNEL** → SHELL
- LOADER now loads KERNEL.BIN (was SHELL.BIN); kernel loads SHELL.BIN
- Shell refactored to pure **user-mode executable** — magic changed from MNSH to MNEX
- All direct BIOS calls in shell replaced with INT 0x80 syscalls
- Disk layout: kernel at partition offset 20, shell moved to partition offset 36
- build.ps1 assembles 5 binaries: MBR (512 B), VBR (1 KB), LOADER (1 KB),
  KERNEL (2 KB), SHELL (5 KB)
- create-disk.ps1 updated with new `-KernelPath` parameter
- Memory layout: SHELL.BIN at 0x3000 (8 KB max), KERNEL.BIN at 0x5000–0x57FF
- Version banner updated to v0.5.0

## [0.4.0] — 2026-05-11

### Added
- **Three-stage boot chain** — refactored from monolithic VBR to:
  - **VBR** (2 sectors / 1 KB): loads LOADER.BIN from fixed partition offset
  - **LOADER.BIN** (2 sectors / 1 KB): A20 gate enablement, loads SHELL.BIN
  - **SHELL.BIN** (10 sectors / 5 KB): interactive shell with all commands
- **Boot Info Block (BIB)** at 0x0600 — shared parameter block passed between
  boot stages (boot drive, A20 status, partition LBA)
- **Binary headers** — LOADER uses 'MNLD' magic, SHELL uses 'MNSH' magic,
  each with self-describing sector count
- **Partition LBA stamping** — create-disk.ps1 writes the partition start LBA
  into the VBR header at offset 9, enabling partition-relative addressing

### Changed
- VBR shrunk from 16 sectors (8 KB) to 2 sectors (1 KB) — now a pure loader
- A20 enablement moved from VBR to LOADER.BIN
- Shell and all commands moved from VBR to SHELL.BIN (separate binary)
- Memory layout updated: LOADER at 0x0800, SHELL at 0x3000, BIB at 0x0600
- `mem` command layout display updated for new memory map
- `ver` command updated: boot chain shows "MBR -> VBR -> LOADER -> SHELL"
- Version banner updated to v0.4.0

### Fixed
- **MBR boot drive bug** — DL was being restored from memory after `rep movsw`
  had overwritten the MBR data section; now saved to register before the copy

### Technical
- Partition disk layout: VBR at offset 0, LOADER at offset 4, SHELL at offset 20
- Build system: build.ps1 now assembles 4 binaries; create-disk.ps1 places all 3
  within the partition; build.yml validates all binaries
- Shell has room to grow: 10 sectors used of 32 max (16 KB)

## [0.3.0] — 2026-05-11

### Added
- **A20 gate enablement** — VBR now enables the A20 address line at boot, unlocking
  access to memory above 1 MB.  Uses three fallback methods:
  1. BIOS INT 15h AX=2401h (cleanest, most portable)
  2. Keyboard controller 8042 (classic AT method, ports 0x64/0x60)
  3. Fast A20 via port 0x92 (quick but not universal)
- **`check_a20` subroutine** — reusable wrap-around A20 verification used at boot
  and by the `mem` command
- **`mem` command A20 verification** — now shows boot-time result and performs a
  live re-test to confirm A20 is still active

### Changed
- Version banner updated to v0.3.0

## [0.2.7] — 2026-05-11

### Added
- **`ver` command** — displays version, architecture, assembler, platform, boot chain, disk, and source URL
- **`sysinfo` CPU page** — new Page 1 with CPUID-based information:
  - Vendor string (e.g., "GenuineIntel")
  - Family, model, stepping numbers
  - Feature flags (FPU, TSC, MSR, CX8, PGE, CMOV, MMX, SSE, SSE2, SSE3, SSE4.1, SSE4.2)
  - Hypervisor detection and vendor string (e.g., "Microsoft Hv")
- **`sysinfo` EDD disk info** — Enhanced Disk Drive support on the disk page:
  - EDD version number
  - Total sector count (32-bit hex)
  - Bytes per sector

### Changed
- Sysinfo expanded from 4 pages to 5 pages (CPU, Memory, BDA, Video & Disk, IVT)
- Help text updated to include `ver` command
- Version banner updated to v0.2.7

## [0.2.6] — 2026-05-11

### Added
- **`mem` command** — detailed memory information display:
  - Conventional memory (INT 12h)
  - Extended memory (INT 15h AH=88h)
  - A20 gate status (wrap-around test at 0x0000:0x0500 vs 0xFFFF:0x0510)
  - Real-mode memory layout map with sizes (IVT, BDA, free area, boot area, video, ROM)
  - E820 BIOS memory map with type labels

### Changed
- Help text updated to include `mem` command
- Version banner updated to v0.2.6

## [0.2.5] — 2026-05-11

### Added
- **Interactive command shell** — VBR now boots into a `mnos:\>` prompt with keyboard input
- **Shell commands**: `sysinfo`, `help`, `cls`, `reboot`
- **Input handling**: `readline` subroutine with backspace support, case-insensitive (auto-lowercase)
- **String comparison**: `strcmp` subroutine for command dispatch
- **`sysinfo` command** — the 4-page system info display is now invoked on demand (was automatic)

### Changed
- VBR clears screen on boot and displays `MNOS v0.2.5` banner before shell prompt
- System info display moved from boot-time to `sysinfo` shell command
- `reboot` uses warm-reboot (0x0472 flag + far jump to BIOS reset vector)
- After `sysinfo` completes, returns to shell prompt (no longer halts)

## [0.2.2] — 2026-05-11

### Added
- **4-page system information display** — VBR now queries BIOS/hardware and displays:
  - Page 1: CPU & Memory (INT 12h, INT 15h AH=88h, E820 memory map)
  - Page 2: BIOS Data Area (COM/LPT ports, equipment word, video info from BDA)
  - Page 3: Video & Disk (video mode, cursor, video memory base, boot drive geometry)
  - Page 4: IVT Sample (first 8 interrupt vectors with descriptions)
- **VBR subroutines**: `print_hex16`, `print_dec16`, `wait_key`, `puthex8` — reusable utility functions
- **Inter-page navigation**: "Press any key..." between pages with screen clear

### Changed
- VBR now uses full 16-sector (8 KB) boot area — code+data spans sectors 0–1, rest zero-padded
- VBR sector 0 contains header + trampoline + boot signature; code starts in sector 1
- `create-disk.ps1` writes full multi-sector VBR binary (was only writing 512 bytes)
- CI verifies VBR binary size matches header-declared sector count
- Fixed em dash (U+2014) in VBR banner — replaced with ASCII hyphen for correct BIOS rendering

## [0.2.1] — 2026-05-11

### Added
- **Multi-sector VBR loading** — MBR reads boot-area sector count from VBR header, loads all N sectors (default 16 = 8 KB)
- **VBR header** — self-describing format: `JMP SHORT` + `NOP` + `'MNOS'` magic + sector count at offset 7
- CI verification of VBR header magic (`MNOS`) and sector count validity

### Changed
- MBR uses two-phase disk read: load 1 sector → parse header → reload all boot-area sectors
- Heavily commented both `mbr.asm` and `vbr.asm` for educational readability
- Trimmed MBR error messages to fit new loading code within 446-byte limit (17 bytes free)

## [0.2.0] — 2026-05-11

### Added
- **Partition table support** — MBR scans all 4 partition entries and prints type, LBA, size, active status
- **Volume Boot Record (VBR)** — `src/boot/vbr.asm`, chain-loaded from the active partition
- **Disk image tool** — `tools/create-disk.ps1` stamps partition table into MBR and writes VBR at partition LBA
- **LBA extended read** — MBR uses `INT 13h AH=42h` (DAP) for LBA-based disk reads

### Changed
- Build pipeline now: assemble MBR + VBR → create partitioned raw image → wrap as VHD
- CI workflow verifies VBR signature and partition table presence
- Release zip now includes `vbr.bin` alongside `mbr.bin`

## [0.1.0] — 2026-05-09

### Added
- **Master Boot Record** — 16-bit x86 bootloader that prints `In MBR` and halts
- **VHD creation tool** — pure-PowerShell fixed VHD 1.0 image generator (`tools/create-vhd.ps1`)
- **Build system** — `build.bat` / `tools/build.ps1` with automatic NASM download
- **Hyper-V VM setup** — `setup-vm.bat` / `tools/setup-vm.ps1` creates or updates a Gen 1 VM
- **Design document** — `doc/DESIGN.md` covering architecture, VHD format, toolchain, and roadmap
- **GitHub workflows** — CI build on push/PR, release on version tags
- **Community files** — LICENSE (MIT), CONTRIBUTING, CODE_OF_CONDUCT, issue templates
