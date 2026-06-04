# MNOS16

A minimalistic 16-bit operating system built from scratch in x86 assembly —
currently at **v0.9.17**.  Features a multi-stage boot loader, a
microkernel-style architecture with separate relocatable modules for filesystem
and memory management, and an interactive shell that can load and run user
programs.  Targets Hyper-V Gen 1 VMs with a unified VHD containing both Release
and Debug configurations.

![MNOS16 booting in Hyper-V](doc/booted.gif)

[![Build](../../actions/workflows/build.yml/badge.svg)](../../actions/workflows/build.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

## Prerequisites

| Tool | Purpose | Install |
|------|---------|---------|
| **NASM** | x86 assembler | [nasm.us](https://www.nasm.us/) — or let `build.bat` download it automatically |
| **PowerShell 7+** | Build system & VHD creation | [aka.ms/powershell](https://aka.ms/powershell) |
| **Python 3.10+** | Unit tests (Unicorn Engine) | [python.org](https://www.python.org/) — deps installed automatically by build |

## Quick Start

```cmd
build.bat           # build + run unit tests
build.bat notest    # build only (skip tests)
build.bat clean     # clean build + tests
```

The build script will:
1. Download NASM into `tools/nasm/` if not already installed
2. Assemble all binaries — release **and** debug variants (11 total)
3. Create `build/boot/MNOS16.vhd` (16 MB fixed VHD with both variants)

### Debug serial output

The unified VHD contains both release and debug kernels — select at the boot
menu.  Debug mode adds serial logging, syscall tracing, assertion macros, and
boot milestone messages via COM1 (115200 baud, 8N1).  Assert failures dump
registers to serial and halt the CPU.  CPU fault handlers are present in
**both** builds — release shows exception name, CS:IP, registers, FLAGS, and
stack on screen; debug additionally logs to serial.  See `doc/DEBUGGING.md`
§3–6 for details.

To read serial output from a debug boot (requires admin — manages VM lifecycle):

```cmd
build.bat
setup-vm.bat
read-serial.bat
```

`read-serial.bat` stops the VM, restarts it, and immediately connects to the
COM1 pipe — capturing boot messages from the first byte.  On VM reboot or
reset, it auto-reconnects.  Press Ctrl+C to stop.

## Running in Hyper-V

```cmd
:: First time — creates the VM and attaches the VHD (requires Admin)
setup-vm.bat

:: After rebuilding — updates the VM's VHD in-place
build.bat
setup-vm.bat
```

The script will prompt for a VM name and location (defaults are fine), then
create a Gen 1 / 32 MB RAM VM with no network adapter and COM1 mapped to
`\\.\pipe\MNOS16-SERIAL` for serial debug output.  On repeat runs it stops the
VM, swaps in the new VHD, and leaves it ready to start.

### Boot menu

On startup, the loader presents a boot menu:

```
  MNOS Boot Manager

  1) MNOS [Release]
  2) MNOS [Debug]

  Press 1 or 2:
```

Press **1** for the release kernel or **2** for the debug kernel (with serial
tracing, assertion macros, and debug syscalls).  Both variants are on the same
VHD — no need to rebuild or swap images.

After the boot chain completes, you'll see the shell:

```
  MNOS v0.9.17 [Release]

mnos:\>
```

Type `help` for a list of commands:

| Command | Description |
|---------|-------------|
| `mem` | Memory diagnostics — conventional/extended RAM, A20 gate, layout, E820 map |
| `dir` | List files on disk (name, type, sectors, bytes) |
| `copy` | Copy a file (`copy SRC.EXT DST.EXT`) |
| `del` | Delete a file (`del FILENAME.EXT`) |
| `ren` | Rename a file (`ren OLD.EXT NEW.EXT`) |
| `ver` | Version, architecture, platform, and build info |
| `help` | List available commands |
| `cls` | Clear screen |
| `reboot` | Warm reboot |

Any unrecognized command is treated as a program name — e.g., typing `edit`
runs `EDIT.MNX`, typing `sysinfo` runs `SYSINFO.MNX`, typing `mnmon` runs
`MNMON.MNX`, typing `basic` runs `BASIC.MNX`.  The `.MNX` extension is optional.

Bundled user programs:

| Program        | Description |
|----------------|-------------|
| `EDIT.MNX`     | Full-screen text editor (gap buffer, find/replace, atomic save) |
| `BASIC.MNX`    | Line-numbered BASIC interpreter (PRINT/INPUT/FOR/GOSUB/LOAD/SAVE/FILES) |
| `MNMON.MNX`    | Machine monitor (db/dw/eb/ew/x/g, DOS-DEBUG-style) |
| `SYSINFO.MNX`  | System info — kernel/shell/memory/disk diagnostics |

```powershell
Start-VM -Name 'MNOS16'           # start the VM
vmconnect localhost 'MNOS16'      # open the console
```

## Project Structure

```
mini-os/
├── .github/
│   ├── ISSUE_TEMPLATE/       # Bug report & feature request templates
│   └── workflows/
│       ├── build.yml         # CI — build & verify on push/PR
│       └── release.yml       # CD — package & release on version tags
├── doc/
│   ├── ABI.md                # Application Binary Interface contract (portability guarantee)
│   ├── DESIGN.md             # Architecture & design document
│   ├── DEBUGGING.md          # Debug infrastructure (serial, asserts, faults, canary)
│   ├── BASIC.md             # BASIC.MNX interpreter reference (language, commands, internals)
│   ├── EDITOR.md             # EDIT.MNX text editor design (gap buffer, dialogs, search)
│   ├── LOADER.md             # Stage-2 loader design (A20, boot menu)
│   ├── FILESYSTEM.md         # MNFS specification & FS.SYS architecture
│   ├── BOOT-LAYOUT-RATIONALE.md  # Boot chain rationale (DOS/Windows/Linux comparisons)
│   ├── MEMORY-LAYOUT.md      # Memory map, stack analysis, protected-mode roadmap
│   ├── MEMORY-MANAGER.md     # Memory manager design & implementation (MM.SYS)
│   ├── COMMAND-LINE.md       # Command-line expansion system (5-layer design)
│   ├── CPU-MODES-AND-TRANSITIONS.md  # 16→32→64-bit journey, BIOS vs UEFI
│   ├── MNEX-BINARY-FORMAT.md # Custom binary format spec, toolchain, build pipeline
│   ├── MNMON.md              # Machine monitor design & command reference
│   ├── PROGRAM-LOADER.md     # Program loader design — implicit execution, TPA, .MNX format
│   ├── SYSINFO.md            # SYSINFO.MNX — system information utility (5 pages)
│   ├── SYSTEM-CALLS.md       # User↔kernel boundary, IVT/IDT/SYSCALL mechanisms
│   └── TESTING.md            # Unit test framework design (3-tier strategy)
├── src/
│   ├── include/               # Shared constants & subroutines (%include)
│   │   ├── bib.inc            # Boot Info Block field addresses
│   │   ├── boot_msg.inc       # Boot progress messages ([  OK  ] / [FAIL])
│   │   ├── debug.inc          # DBG/ASSERT macros (debug build only)
│   │   ├── find_file.inc      # Bootstrap MNFS directory lookup subroutine
│   │   ├── load_binary.inc    # Shared MNEX binary loader subroutine
│   │   ├── memory.inc         # Component load addresses, stack canary, MM constants
│   │   ├── mnfs.inc           # MNFS filesystem constants & INT 0x81 numbers
│   │   ├── mnoslib.inc        # User-mode helper library (mn_save_file, mn_load_file)
│   │   ├── serial.inc         # COM1 serial I/O (debug build only)
│   │   ├── syscalls.inc       # INT 0x80 syscall function numbers
│   │   └── version.inc        # Single source of truth for OS version
│   ├── boot/
│   │   ├── mbr.asm            # MBR — partition table scan + VBR chain-load
│   │   └── vbr.asm            # VBR — finds LOADER.SYS via MNFS directory
│   ├── loader/
│   │   └── loader.asm         # Stage-2 loader — A20 gate, boot menu, loads KERNEL
│   ├── kernel/
│   │   ├── kernel.asm         # 16-bit kernel — INT 0x80 syscalls, module loader + relocation
│   │   ├── kernel_syscall.inc # Syscall dispatcher + 29 handlers (jump table)
│   │   ├── kernel_data.inc    # Kernel string constants, filenames, DAP
│   │   ├── kernel_fault.inc   # CPU exception fault handlers + PIC remap
│   │   └── kernel_stack.inc   # Stack canary (debug-only overflow detection)
│   ├── mm/
│   │   └── mm.asm             # Memory manager — INT 0x82 API, heap (relocatable)
│   ├── fs/
│   │   └── fs.asm             # Filesystem module — INT 0x81 API, MNFS (relocatable)
│   ├── programs/
│   │   ├── edit/              # EDIT.MNX — full-screen text editor
│   │   │   ├── edit.asm       #   Entry point, constants, MNEX header
│   │   │   ├── edit_keys.inc  #   Key dispatch (scancode → command)
│   │   │   ├── edit_draw.inc  #   Screen rendering (menu, edit area, status)
│   │   │   ├── edit_menu.inc  #   Drop-down menu system
│   │   │   ├── edit_gap.inc   #   Gap buffer operations
│   │   │   ├── edit_cursor.inc    # Cursor movement & viewport
│   │   │   ├── edit_editing.inc   # Text manipulation (Enter, BS, Del)
│   │   │   ├── edit_select.inc    # Selection & cursor↔gap sync
│   │   │   ├── edit_clipboard.inc # Cut/Copy/Paste
│   │   │   ├── edit_find.inc  #   Find/Replace/Replace All/GoTo
│   │   │   ├── edit_dialog.inc    # Modal dialogs & file picker
│   │   │   ├── edit_file.inc  #   File I/O (load/save)
│   │   │   ├── edit_exit.inc  #   Exit handler
│   │   │   ├── edit_msg.inc   #   Status messages
│   │   │   └── edit_data.inc  #   State variables, strings, help text
│   │   ├── basic/             # BASIC.MNX — line-numbered BASIC interpreter
│   │   │   ├── basic.asm      #   Entry point, REPL loop, MNEX header
│   │   │   ├── basic_data.inc #   Fixed-address layout, error codes, constants
│   │   │   ├── basic_tokens.inc   # Keyword/function/operator token IDs
│   │   │   ├── basic_lex.inc  #   Tokenizer + detokenizer
│   │   │   ├── basic_err.inc  #   Central error trampoline (ERR/ERL)
│   │   │   ├── basic_edit.inc #   Readline + program-line list ops
│   │   │   ├── basic_load.inc #   LOAD / SAVE / FILES helpers
│   │   │   ├── basic_var.inc  #   Variable storage (A-Z, A$-Z$, arrays)
│   │   │   ├── basic_expr.inc #   Pratt-style expression evaluator
│   │   │   └── basic_stmt.inc #   Statement dispatcher + handler set
│   │   ├── sysinfo/           # SYSINFO.MNX — system information (5 pages)
│   │   │   ├── sysinfo.asm    #   Entry point, MNEX header (6 sectors)
│   │   │   ├── sysinfo_code.inc   # Display logic (CPU, memory, BDA, disk, IVT)
│   │   │   └── sysinfo_data.inc   # Strings & runtime buffers
│   │   ├── hello.asm          # HELLO.MNX — minimal demo program (not shipped)
│   │   └── mnmon.asm          # MNMON.MNX — interactive machine monitor (WinDbg-style)
│   └── shell/
│       ├── shell.asm          # Shell entry point — init, command loop, dispatch (relocatable)
│       ├── shell_cmd_simple.inc   # Simple commands (ver, help, cls, reboot)
│       ├── shell_cmd_dir.inc      # dir command (MNFS directory listing)
│       ├── shell_cmd_fs.inc       # File commands (copy, del, ren)
│       ├── shell_cmd_mem.inc      # mem command (memory diagnostics)
│       ├── shell_cmd_run.inc      # Implicit program execution (loader + validation)
│       ├── shell_parse_args.inc   # Argument tokenizer (Layer 2: argc/argv)
│       ├── shell_readline.inc     # Input handling + utility subroutines (strcmp, cmdmatch)
│       └── shell_data.inc         # String constants + runtime data buffers
├── tools/
│   ├── build.ps1              # Build logic — assembles all binaries, creates VHD
│   ├── gen_relocs.py          # Relocation table generator (delta comparison)
│   ├── pack_module.py         # Module packager (pre-biasing + v2 header)
│   ├── create-disk.ps1        # Partitioned raw disk image creator
│   ├── create-vhd.bat         # VHD tool — batch wrapper
│   ├── create-vhd.ps1        # Raw image → VHD converter (pure PowerShell)
│   ├── setup-vm.ps1           # Hyper-V VM create/update logic
│   ├── read-serial.ps1        # Read COM1 debug output from running VM
│   └── nasm/                  # Auto-downloaded NASM (gitignored)
├── build/                     # Build output (gitignored)
│   └── boot/
│       ├── mbr.bin            # MBR binary
│       ├── vbr.bin            # VBR binary (2 sectors)
│       ├── loader.sys         # LOADER (3 sectors, shared)
│       ├── fs.sys             # FS — release, relocatable (5 sectors)
│       ├── kernel.sys         # KERNEL — release (8 sectors)
│       ├── shell.sys          # SHELL — release, relocatable (13 sectors)
│       ├── mm.sys             # MM — release, relocatable (2 sectors)
│       ├── edit.mnx            # EDIT — text editor (13 sectors)
│       ├── basic.mnx           # BASIC — interpreter (~20 sectors)
│       ├── sysinfo.mnx        # SYSINFO — system info (6 sectors)
│       ├── mnmon.mnx          # MNMON — machine monitor (5 sectors)
│       ├── fsd.sys            # FS — debug (8 sectors)
│       ├── kerneld.sys        # KERNEL — debug (14 sectors)
│       ├── shelld.sys         # SHELL — debug (13 sectors)
│       ├── mmd.sys            # MM — debug (3 sectors)
│       ├── MNOS16.img         # 16 MB raw disk image
│       └── MNOS16.vhd        # Bootable VHD (single unified image)
├── build.bat                  # Build entry point
├── read-serial.bat            # Read serial debug output from VM
├── setup-vm.bat               # Hyper-V VM setup entry point
├── tests/                     # Unit test suite (Python + Unicorn Engine)
│   ├── conftest.py            # pytest fixtures & coverage registration
│   ├── gen_constants.py       # Auto-generates constants.py from .inc files
│   ├── requirements.txt       # Python deps (unicorn, pytest, pytest-html)
│   ├── harness/
│   │   ├── assembler.py       # NASM stub assembly helper
│   │   ├── branch_coverage.py # Branch-level coverage tracking
│   │   ├── constants.py       # Auto-generated from memory.inc + syscalls.inc + mnfs.inc
│   │   ├── coverage.py        # Coverage collector & HTML/JSON reporter
│   │   └── emulator.py        # MiniOSEmulator (Unicorn wrapper)
│   ├── stubs/                 # Minimal NASM harnesses for routines under test
│   │   ├── stub_cmdmatch.asm
│   │   ├── stub_edit_find.asm
│   │   ├── stub_edit_fname.asm
│   │   ├── stub_edit_gap.asm
│   │   ├── stub_exec_parse_args.asm
│   │   ├── stub_fs_write.asm
│   │   ├── stub_mm.asm
│   │   ├── stub_parse_args.asm
│   │   ├── stub_parse_fname.asm
│   │   ├── stub_spawn_state.asm
│   │   └── stub_strcmp.asm
│   ├── test_cmdmatch.py       # 12 tests for cmdmatch (command prefix matching)
│   ├── test_edit_find.py      # Tests for editor search/find and atoi
│   ├── test_edit_fname.py     # Tests for editor 8.3 filename parser
│   ├── test_edit_gap.py       # Tests for editor gap buffer operations
│   ├── test_exec.py           # Tests for SYS_EXEC (overlay execution, arg parsing)
│   ├── test_fs_write.py       # 26 tests for FS write/delete/rename
│   ├── test_memory_layout.py  # 16 tests for memory layout consistency
│   ├── test_mm.py             # 29 tests for memory manager
│   ├── test_parse_args.py     # 15 tests for shell_parse_args
│   ├── test_parse_filename.py # 9 tests for run_parse_filename
│   ├── test_relocation.py     # 39 tests for relocation toolchain and patching
│   ├── test_spawn_state.py    # Tests for SYS_SPAWN state machine (push/rollback/depth)
│   └── test_strcmp.py          # 11 tests for strcmp
├── CHANGELOG.md
├── CODE_OF_CONDUCT.md
├── CONTRIBUTING.md
├── LICENSE
└── README.md
```

## Testing

Unit tests use **Python + Unicorn Engine** to emulate 16-bit x86 routines without
QEMU or hardware. See **[doc/TESTING.md](doc/TESTING.md)** for the full test strategy.

```powershell
# Install dependencies (once)
pip install -r tests/requirements.txt

# Run all tests
python -m pytest tests/ -v

# Run with coverage report (generates coverage/index.html)
python -m pytest tests/ -v    # coverage is auto-generated on session finish
```

**234 tests** across 9 modules: `shell_parse_args` (15), `run_parse_filename` (9),
`strcmp` (11), `mm_allocator` (29), `fs_write` (26), `cmdmatch` (12),
`editor` (58), `memory_layout` (16), `relocation` (39), `exec` (17).
Tests run automatically in CI via GitHub Actions.

**Coverage metrics:**
- **Statement coverage** — % of binary addresses executed by tests
- **Branch coverage** — % of conditional jump outcomes (taken + fall-through) exercised
- **Historical trends** — Chart.js graph tracking coverage across last 50 CI runs

## Design & Architecture

See **[doc/DESIGN.md](doc/DESIGN.md)** for the full architecture document — boot sequence,
memory layout, VHD format, shell internals, disk layout, and project roadmap.

Additional deep-dive documents:

- **[doc/FILESYSTEM.md](doc/FILESYSTEM.md)** — MNFS flat filesystem specification:
  directory format, 8.3 filenames, FS.SYS module architecture, INT 0x81 API,
  bootstrap vs runtime filesystem access, and build pipeline integration.

- **[doc/BOOT-LAYOUT-RATIONALE.md](doc/BOOT-LAYOUT-RATIONALE.md)** — Why the three-stage boot
  chain? Comparisons with DOS 6.22, Windows NT/XP, and Linux/GRUB. Analysis of
  LBA gap vs. partition-internal loading, and clobber protection strategies.

- **[doc/MEMORY-LAYOUT.md](doc/MEMORY-LAYOUT.md)** — Exhaustive real-mode memory map showing
  every region (IVT, BDA, BIB, LOADER, SHELL, stack). Stack sizing analysis,
  transient vs. permanent memory, and the roadmap from A20 to protected mode.

- **[doc/CPU-MODES-AND-TRANSITIONS.md](doc/CPU-MODES-AND-TRANSITIONS.md)** — The complete
  journey from 16-bit real mode to 32-bit protected mode to 64-bit long mode.
  GDT, IDT, paging, hardware drivers (VGA, keyboard, ATA), PIC remapping, and
  a detailed BIOS vs UEFI comparison.

- **[doc/MNEX-BINARY-FORMAT.md](doc/MNEX-BINARY-FORMAT.md)** — The MNOS Executable format
  specification: unified 32-byte MNEX headers for all binaries (16/32/64-bit),
  NASM+Clang toolchain rationale, complete build pipeline, and C kernel code examples.

- **[doc/SYSTEM-CALLS.md](doc/SYSTEM-CALLS.md)** — How user-mode code talks to the kernel.
  Covers the IVT (16-bit), IDT with ring transitions (32-bit), and SYSCALL/SYSRET
  (64-bit). Includes complete handler code, Windows/Linux comparisons, and the
  mini-os syscall table.

- **[doc/DEBUGGING.md](doc/DEBUGGING.md)** — Debug infrastructure: serial logging (COM1),
  syscall tracing, user-mode debug syscalls, assertion macros, CPU fault handlers,
  stack canary, debug build mode.  Covers Hyper-V COM port setup and build integration.

- **[doc/MEMORY-MANAGER.md](doc/MEMORY-MANAGER.md)** — Memory manager design and
  implementation: INT 0x82 API (alloc, free, avail, info), MCB header format,
  first-fit algorithm, forward coalescing, heap layout (0x8000–0xF7FF).

- **[doc/PROGRAM-LOADER.md](doc/PROGRAM-LOADER.md)** — Program loader design: implicit
  execution from the shell prompt, FS_FIND_BASE syscall, TPA memory layout,
  four-layer validation, SYS_EXIT/SYS_GET_ARGS syscalls, and MNEX format loading.

- **[doc/MNMON.md](doc/MNMON.md)** — Machine monitor design: WinDbg-style commands
  (db/dw/eb/ew/g), Wozmon heritage, implementation as standalone .MNX program.

- **[doc/SYSINFO.md](doc/SYSINFO.md)** — System information utility: 5 pages of
  hardware details (CPU, memory, BDA, video/disk, IVT), CPUID detection, EDD queries.

- **[doc/EDITOR.md](doc/EDITOR.md)** — Text editor design: gap buffer internals,
  modular .inc architecture, modal dialog system, Find/Replace engine, memory
  layout, color scheme, key dispatch, and file I/O integration.

- **[doc/ABI.md](doc/ABI.md)** — Application Binary Interface contract: TPA
  layout, syscall conventions, entry state, unknown-syscall error handling,
  and the binary portability guarantee for .MNX programs across OS versions.

## Version History

Each version is a tagged release you can checkout to see the project at that stage.

| Tag | Description | What you'll see |
|-----|-------------|-----------------|
| `v0.1.0` | **M0 — Hello World** | MBR prints "mini-os" and halts |
| `v0.2.0` | **M1 — Partition table + VBR** | MBR scans partition table, chain-loads VBR from active partition |
| `v0.2.1` | **Multi-sector boot area** | VBR header (`MNOS` magic + sector count), MBR two-phase load, heavily commented code |
| `v0.2.2` | **System info display** | VBR shows 4 pages of hardware info (memory, BDA, video/disk, IVT) |
| `v0.2.5` | **M2 — Interactive shell** | `mnos:\>` prompt with `sysinfo`, `help`, `cls`, `reboot` commands |
| `v0.2.6` | **`mem` command** | Detailed memory info: conventional/extended RAM, A20 gate status, memory layout, E820 map |
| `v0.2.7` | **`ver` + CPU/EDD sysinfo** | Version command, CPUID details page, EDD disk info, sysinfo now 5 pages |
| `v0.3.0` | **A20 gate enablement** | VBR enables A20 at boot (BIOS/8042/Fast A20 fallbacks), full memory access above 1 MB |
| `v0.4.0` | **Three-stage boot chain** | VBR -> LOADER.SYS -> SHELL.SYS split; A20 in loader, shell as separate binary, BIB at 0x0600 |
| `v0.5.0` | **16-bit Kernel + Syscalls** | KERNEL.SYS with INT 0x80 syscall interface; shell refactored to user-mode MNEX executable |
| `v0.6.0` | **MNFS Filesystem** | Flat filesystem, FS.SYS module with INT 0x81 API, `dir` command, no hardcoded disk offsets |
| `v0.7.0` | **Serial Debugging** | COM1 serial logging, debug macros, syscall/FS tracing, debug build mode (`build.bat /debug`) |
| `v0.7.1` | **User-Mode Debug Syscalls** | SYS_DBG_PRINT/HEX16/REGS (0x20–0x22) with caller tags, shell tracing |
| `v0.7.2` | **Assert Macros** | ASSERT, ASSERT_CF_CLEAR, ASSERT_MAGIC — halt + register dump on failure; 0 bytes in release |
| `v0.7.3` | **CPU Fault Handlers** | Trap #DE, #DB, #OF, #BR, #UD, #NM, #DF — exception name + CS:IP + register dump; debug only |
| `v0.7.4` | **Release Fault Handlers** | Fault handlers in both builds; PIC remapped (IRQ→0x20); full crash screen with registers, FLAGS, stack; 7 vectors |
| `v0.7.5` | **Source File Split** | Kernel & shell split into focused include files; binary-identical output; build script adds per-module include paths |
| `v0.8.0` | **Dual-Boot Menu** | Boot menu (release/debug); unified VHD with both variants; BIB boot_mode; shell shows [Release]/[Debug] |
| `v0.8.1` | **Stack Canary** | Debug-only stack overflow detection; canary at 0x7000 checked on every syscall; fatal halt with diagnostic on corruption |
| `v0.9.0` | **Memory Manager** | MM.SYS heap allocator at 0x2800; INT 0x82 API (alloc/free/avail/info); 30 KB heap at 0x8000; MCB block headers; first-fit with coalescing |
| `v0.9.1` | **mem + MM tracing** | Shell `mem` shows heap stats (total/used/free/blocks/largest); memory layout includes MM + heap; debug serial tracing for all MM calls |
| `v0.9.2` | **MCB owner tags** | Flags byte bits 1-3 carry 3-bit owner ID; MEM_ALLOC DL=owner; shell `mem` block detail walk with owner names; debug trace logs owner |
| `v0.9.5` | **File extensions** | System binaries renamed `.BIN` → `.SYS` (kernel-loaded, resident); `.MNX` convention for future user-mode executables (shell-loaded, transient) |
| `v0.9.6` | **Program Loader + Debug Diagnostics** | Implicit program execution (type `hello` to run HELLO.MNX); FS_FIND_BASE syscall; INT depth tracking; DAP hex dump; EDI-clobbers-DI bug fix |
| `v0.9.7` | **Machine Monitor (mnmon)** | MNMON.MNX — WinDbg-style memory monitor (db/dw/eb/ew/g); standalone user program; proves interactive program loading |
| `v0.9.8` | **Parsed Arguments (argc/argv)** | Layer 2 command-line parsing; SYS_GET_ARGC/SYS_GET_ARGV syscalls; double-quote support; max 15 args; doc/COMMAND-LINE.md |
| `v0.9.9` | **Unit Test Framework** | Python + Unicorn Engine test harness; 64 tests across 4 modules; statement + branch coverage with Capstone; historical trend tracking (Chart.js); CI/CD test job; doc/TESTING.md |
| `v0.9.10` | **HMA Heap + TPA Expansion** | Dynamic memory moved to HMA (~64 KB); TPA expanded 26→30 KB; auto-generated test constants; shell/MNMON HMA-aware |
| `v0.9.11` | **MNFS Write Support** | FS write/delete/rename syscalls (INT 0x81 AH=0x06–0x08); tombstone deletion; shell `copy`, `del`, `ren` commands; `cmdmatch` prefix dispatcher; 102 unit tests (95% branch on FS); FS.SYS 3→5 sectors; SHELL.SYS 16→18 sectors |
| `v0.9.12` | **Text Editor + MNOS16 Rename** | Full-screen editor (EDIT.MNX); gap buffer; modal dialogs; Find/Replace All (F4); menu hotkeys; project renamed to MNOS16; implicit execution (no built-in `edit` command) |
| `v0.9.13` | **Sysinfo Extraction + Layout Tightening** | `sysinfo` extracted from shell into standalone SYSINFO.MNX (6 sectors); shell shrunk 19→13 sectors; kernel relocated 0x5800→0x5000; stack doubled 2→4 KB; 16 memory-layout consistency tests; 176 total tests |
| `v0.9.14` | **Relocatable Modules + ABI Contract** | All binaries (system modules FS/MM/SHELL AND user programs .MNX) now assembled with ORG 0 and relocated at load time via MNEX v2 headers; gen_relocs.py + pack_module.py toolchain; kernel apply_relocs for modules, shell apply_relocs for programs; dynamic module placement; formal ABI contract (doc/ABI.md); full binary portability; 35 relocation unit tests; 213 total tests |
| `v0.9.15` | **SYS_EXEC + SYS_SPAWN** | Overlay exec (AH=0x27) replaces running program; SYS_SPAWN (AH=0x28) with nested parent reload (4-level stack); trampoline-based shell return; spawn rollback on failure; MNMON `x` command; 234 total tests |
| `v0.9.16` | **Nested spawn fixes** | Fixed `#UD` crash on multi-level SYS_SPAWN; nested spawns reuse outermost trampoline; spawn rollback on pre-load failure; trampoline re-install after nested unwind |
| `v0.9.17` | **BASIC + FS API hardening** | BASIC.MNX interactive interpreter (PRINT/INPUT/FOR/GOSUB/LOAD/SAVE/FILES); new FS_REPLACE_FILE atomic syscall (AH=0x09); mnoslib.inc user-mode helpers (mn_save_file, mn_load_file); FS ABI contract v1 (full 32-bit register preservation); EDIT + BASIC migrated to atomic save; doc/BASIC.md; 254 total tests |

```cmd
git checkout v0.1.0      # see the project at any prior milestone
```

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.

## License

MIT — see [LICENSE](LICENSE).
