# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

---

## [0.9.19.0] - 2026-06-09

This release lands **BASIC 2.0** plus the multi-pass runtime fixes,
build-pipeline hardening, and the final HMA-corruption fix that the
post-merge `TESTBAS.BAS` exercise surfaced.  Everything previously
tracked as `v0.9.19-a..f` during development is consolidated under one
shipping label.

### Added — **BASIC 2.0** (language)

A long-planned expansion of `BASIC.MNX` to the language surface of a
period-correct microcomputer BASIC.  Older v1.x `.BAS` files continue
to run.

- **Strings as a first-class type.**  Typed expression evaluator
  (`bas_expr_result` carries `{type, length, value}`); string variables
  `A$`..`Z$` and `A0$`..`Z9$`; concatenation with `+`; comparison via
  `=`, `<>`, `<`, `<=`, `>`, `>=`; full string I/O through `PRINT`,
  `INPUT`, and `LET`.  Backed by a 2 KB HMA string-variable heap (80 B
  slot per name → ~25 distinct string variables) plus a 32-entry HMA
  temp-string pool flushed at every statement boundary.
- **String built-in functions:** `LEN`, `ASC`, `VAL`, `CHR$`, `STR$`,
  `LEFT$`, `RIGHT$`, `MID$`, `INKEY$`, `INPUT$`.
- **`DIM` + 1-D arrays** for both numeric (`BVAR_NUM_ARRAY`) and
  string (`BVAR_STR_ARRAY`) values.  Array storage lives in HMA via
  `mn_alloc`, freed on `NEW` / `CLEAR` / error recovery.
- **File I/O channels.**  `OPEN "FILE" FOR INPUT|OUTPUT|APPEND AS #n`,
  `CLOSE [#n[,#n...]]`, `PRINT #n,`, `INPUT #n,`, `EOF(n)`.  Four
  channels (`#1..#4`), each with a 4 KB HMA buffer.  `OUTPUT` /
  `APPEND` channels flush atomically via `mn_replace_file` on `CLOSE`;
  any error closes all channels with the buffer discarded.
- **`DATA` / `READ` / `RESTORE`.**  Dedicated `TOK_DATA_RAW` (= 0xF4)
  payload keeps the inline DATA text as raw bytes (quote-aware, with
  `:`-inside-quotes preserved) so `LIST` and `SAVE` round-trip the
  source verbatim.  `RESTORE` accepts an optional starting line.
- **`DEF FN name(param) = expr`** with `FN name(arg)` in expressions.
  Up to 16 single-argument numeric user functions; recursion rejected
  with `Too complex`.
- **`WHILE` / `WEND`** now actually run (tokens existed in v1.x but the
  dispatcher returned `Syntax error`).

### Fixed — **BASIC 2.0 runtime correctness pass**

The first end-to-end run of `TESTBAS.BAS` surfaced six low-level
state-hygiene bugs in the runtime that the unit suite couldn't see
(all flag- or scratch-slot interactions that only manifest across
multiple statements).  All six are fixed:

1. **`bas_expr_eval` epilogue leaked CF.**  Final `cmp` against
   `BVAR_STRING` left CF=1 on success path → callers that did
   `call bas_expr_eval; jc ...` spuriously branched to error.
   Fixed with explicit `clc` (`basic_expr.inc` `.bee_clear_cf`).
2. **`.be_cmp_emit` dispatcher clobbered comparison flags** between
   `cmp dx, cx` and the consuming `j*c`/`j*g`.  Fixed with
   `pushf`/`popf` around the dispatch (`basic_expr.inc`).
3. **`bas_lex_after_emit` mode-1 reset broke multi-line-ref lists.**
   `ON expr GOTO 1010, 2010, 3010` lost 2010/3010 as `TOK_LINEREF`.
   Added mode 4 ("multi-ref list") that persists across commas
   (`basic_lex.inc`).
4. **`bas_stmt_read` aliased `bas_scratch_d`** (owned by
   `bas_run_program_from_cur`'s next-line offset).  Re-routed READ
   through `bas_scratch_b`; annotated `bas_scratch_d` `;@owner ...`
   (`basic_dataread.inc`, `basic_data.inc`).
5. **`bas_str_to_fname` success path had an extra `pop ds`** (7
   pushes, 8 pops on success), making `ret` jump to whatever AX
   happened to be — OPEN silently hung.  Removed the extraneous pop
   (`basic_io.inc`).
6. **`EOF(N)` dispatch ate an already-consumed `(`**, breaking every
   `WHILE NOT EOF(n)` loop.  Removed redundant `(` check; matches the
   LEFT$ convention (`basic_expr.inc`).

### Fixed — **`bas_str_concat` stack-frame off-by-one (HMA corruption)**

The function's prologue is:

```nasm
bas_str_concat:
    push si        ; saved SI → [bp+4]   (LHS desc ptr)
    push di        ; saved DI → [bp+2]   (RHS desc ptr)
    push bp        ; saved BP → [bp+0]
    mov  bp, sp
```

After the prologue the return IP sits at `[bp+6]`, the saved SI at
`[bp+4]`, and the saved DI at `[bp+2]`.  The first BASIC 2.0 build
reloaded the LHS from `[bp+6]` (the return address!) and the RHS from
`[bp+4]` (the LHS slot), one word off in both places.

For a three-way concat like `LET S3$ = S1$ + ", " + S2$ + "!"` the
third concat sized its temp at LLen+RLen = 12+1 = 13 bytes, but the
"RHS" copy used the LHS descriptor's length (12) instead of 1, writing
12 bytes starting at `dst + LLen = 0x840` and straddling `0x842..0x84B`
— directly through the MCB header of the trailing free block.
`mm_free`'s forward-coalesce check then saw a zero size word or a
non-`'M'` magic byte and skipped the merge, stranding ~63 KB of HMA
behind a broken header.

Visible symptom: `TESTBAS.BAS` test 6 (FILEIO) `?Out of memory in 6030`
after running tests 1-5 (`bas_chan_alloc` couldn't find 4 KB).

**Fix** (`src/programs/basic/basic_str.inc`): LHS reload `mov si,
[bp+4]`, RHS reload `mov si, [bp+2]`.  Comments rewritten.
`bas_str_cmp_desc` has the same prologue but is safe because it
consumes live SI/DI immediately.  Backstopped by
`tests/test_basic_hma_concat_regressions.py` (two static regex guards
that fail the build if the offsets ever regress).

### Added — **build-pipeline structural prevention**

To make the above classes catchable at build time:

#### `tools/asm_lint.py` — static asm linter (new)

Walks `src/programs/basic/*.{asm,inc}`, carves each `bas_*:` function
body, and runs three checkers.  Invoked from `tools/build.ps1`
immediately after NASM-path discovery; any violation aborts the build
before a byte of code is assembled.  Also exposed as a pytest case
(`tests/test_asm_lint.py`).

1. **Stack-balance check.**  Models `push`/`pop`/`pushf`/`popf`/
   `pusha`/`popa`/`add sp,N`/`sub sp,N`/`enter`/`leave` plus the
   `mov bp,sp`/`mov sp,bp` frame-pointer idiom.  Fails if any `ret`
   leaves the function with non-zero stack depth, if a local label is
   reached from two paths with different depths (bug #5 shape), or if
   a basic block goes negative.  Functions can opt out per-label with
   `;@stack-merge` or whole-function with `;@no-stack-check`.
   `;@noreturn` tells the checker that callers' fall-through is dead.
2. **Scratch-slot ownership check.**  Reads `;@owner FUNC` /
   `;@reserved FUNC` annotations on BSS declarations and fails any
   other function that writes the slot (bug #4 shape).
3. **Carry-flag discipline check.**  Functions annotated `;@returns
   cf` opt into a "no flag-poisoner immediately before bare ret"
   rule (bug #1 shape).

#### `src/programs/basic/basic_macros.inc` — convention macros (new)

| Macro                  | Expands to                  | Purpose                                                              |
| ---------------------- | --------------------------- | -------------------------------------------------------------------- |
| `BAS_RET_OK`           | `clc / ret`                 | success exit — impossible to forget the clear (bug #1).              |
| `BAS_RET_ERR <code>`   | `mov al, code / stc / ret`  | error exit with code.                                                |
| `BAS_RET_ERR_NOCODE`   | `stc / ret`                 | error exit when AL is already populated.                             |
| `BAS_DISPATCH_BEGIN`   | `pushf`                     | entry to a dispatcher chain that must preserve incoming flags.       |
| `BAS_DISPATCH_END`     | `popf`                      | per-leaf exit of such a chain.                                       |

Header-guarded with `BASIC_MACROS_INC`; included from `basic.asm`
immediately after `basic_data.inc`.  The lint's RET / CF-setter
recognizers know about these tokens.

### Added — **permanent HMA-diag instrumentation in `basic.mnx`**

- `basic_stmt.inc` `.brd_next_stmt`: conditional `mn_avail` dump
  (`BAS:diag.heap-largest / total / cur_line`) whenever the free total
  shifts by ≥ 256 B vs the previous statement.
- `basic_io.inc` `bas_chan_alloc`: pre-`mn_alloc` dump
  (`BAS:diag.chan-largest / total / want`) so any OPEN failure tells
  you how fragmented the heap was at the moment.

Both call sites use `mn_dbg_hex16`, whose kernel-level syscall handler
is `%ifdef DEBUG`-gated — output only appears under KERNELD.  Cost in
release `basic.mnx`: a handful of `int 0x80` cycles per statement and
~80 B of code; `basic.mnx` is still 35/35 sectors.  The
`bas_dbg_last_avail` BSS slot at `0xC452` is carved from the prior
`bas_str_lhs` headroom (12 B remaining).

### Added — infrastructure & tests

- `doc/BASIC.md` rewritten for v2.0: full statement/function tables,
  examples for file I/O and DATA/READ, DEF FN semantics, updated error
  table, internal-module map.
- **Tier-0 structural regression tests** under `tests/` — pure static,
  no QEMU:
  - `test_basic_strings.py` — string keyword/function wiring
  - `test_basic_arrays.py` — DIM dispatch + array module
  - `test_basic_fileio.py` — channel keywords/dispatchers/hash variants
  - `test_basic_data_read.py` — DATA/READ/RESTORE wiring + `TOK_DATA_RAW`
  - `test_basic_def_fn.py` — DEF SEG vs DEF FN dispatch + TOK_FN in primary
  - `test_basic_runtime_regressions.py` — guards for the 6 runtime bugs
  - `test_basic_hma_concat_regressions.py` — guards for the `bas_str_concat` BP offsets
  - `test_basic_load_buf_headroom.py` — code/BSS/load_buf/prog_base headroom invariants
  - `test_asm_lint.py` — runs the asm-lint pass on `src/programs/basic`

### Changed

- BSS layout: `BAS_MAX_LINES` 256→192 and `BAS_MAX_VARS` 128→96 to
  free 768 B of code space; `BSS_BASE` shifted from 0xAC00 to 0xAEC0,
  later to 0xC400 with `BAS_BSS_SIZE = 0x1000` after the BSS/code
  overlap fix.  The previously reserved 0xBDC0..0xBFFF region now
  holds `bas_expr_result`, the string-heap descriptor, `bas_data_*`
  cursor, `bas_userfn_*` table.
- `bas_cmd_clear` (called by `NEW` and `CLEAR`) closes every open
  channel, walks the var table to free array + string-heap
  allocations, rewinds the DATA cursor, and empties the DEF FN table.
- `basic.mnx` size budget raised from **21 → 35 sectors** to fit the
  new feature set (current: 35 sectors).
- `data/TESTBAS.BAS` option 3 (STRINGS) is now self-verifying — each
  expected value is printed inline via `| <expected>` annotations.

### Removed

- Temporary debug instrumentation from the bug-hunt phase
  (`bsp_dbg_enter` / `bsp_dbg_preE` / `bsp_dbg_postE` /
  `bee_dbg_pre` / `bee_dbg_post`).  Replaced by the permanent
  `.brd_next_stmt` dump (debug builds only, COM1).

### Internal

- New source modules under `src/programs/basic/`: `basic_str.inc`,
  `basic_array.inc`, `basic_io.inc`, `basic_dataread.inc`,
  `basic_defn.inc`, `basic_macros.inc`.  `basic.asm` `%include`
  order is now var → str → array → io → dataread → defn → expr →
  stmt (the dependency chain required for primary to call
  `bas_userfn_invoke`).
- **LOAD-buffer overlap fix.**  The 4 KB conventional-memory scratch
  buffer for `LOAD`/`SAVE`/file-channel I/O lived at TPA offset
  0xC000.  When v2.0 BASIC grew past 0xC000, every `LOAD` overwrote
  the tail of BASIC's own code.  Moved `bas_load_buf` 0xC000 → 0xD400
  and `BAS_PROG_BASE` 0xD000 → 0xE400.
- **BSS / code overlap fix.**  `BAS_BSS_BASE` had been 0xB000 since
  v1.x — inside the code segment.  Moved to 0xC400 with
  `BAS_BSS_SIZE = 0x1000`; updated `bas_init` to zero a fixed 4 KB
  region.  NASM build-time assertion (`times (BAS_BSS_BASE - 0x8000)
  - ($-$$) db 0`) at the bottom of `basic.asm` fails the build with
  "TIMES value is negative" on overflow.
- **Stale-BSS-address fix.**  Five DEF FN frame `equ`s
  (`bas_userfn_depth`, `bas_userfn_saved_si/ex_top/pval/pname`) were
  still pointing at 0xBC60..0xBC68 after the BSS shift — inside the
  code segment.  Moved to 0xD060..0xD06A.  Backstopped by a new
  regression test that scans every `*.inc`/`*.asm` for any `equ` in
  the pre-fix BSS window (0xB000..0xC3FF).

### Test results

- **338 unit tests** pass.
- `TESTBAS.BAS` tests 1–8 all complete without OOM.
- `basic.mnx` 35/35 sectors.

## [0.9.18] - 2026-06-04

### Added
- **Full-coverage `mnoslib` helper library.**  Split the previous single-file
  `mnoslib.inc` into four category headers under an umbrella include:
  - `mnoslib_io.inc` — 11 wrappers for INT 0x80 console / keyboard syscalls
    (`mn_print_string`, `mn_print_char`, `mn_print_hex8/16`, `mn_print_dec16`,
    `mn_read_key`, `mn_peek_key`, `mn_wait_key`, `mn_clear_screen`,
    `mn_set_cursor`, `mn_get_cursor`).
  - `mnoslib_sys.inc` — 26 wrappers for INT 0x80 system, process, and debug
    syscalls (`mn_get_version`, `mn_get_bib`, BDA/equip/video/A20/E820/CPUID/
    EDD/IVT queries, `mn_reboot`, `mn_exit`, `mn_get_args`/`get_argc`/
    `get_argv`, `mn_exec`, `mn_spawn`, `mn_read_sector`, `mn_dbg_print/
    hex16/regs`).
  - `mnoslib_fs.inc` — 9 wrappers for INT 0x81 filesystem syscalls
    (`mn_list_files`, `mn_find_file`, `mn_find_base`, `mn_read_file`,
    `mn_get_fs_info`, `mn_write_file`, `mn_delete_file`, `mn_rename_file`,
    `mn_replace_file`).  The v0.9.17 names `mn_save_file` / `mn_load_file`
    remain available as `equ` aliases for compatibility.
  - `mnoslib_mm.inc` — 5 wrappers for INT 0x82 memory manager syscalls
    (`mn_alloc`, `mn_free`, `mn_avail`, `mn_mem_info`, `mn_mem_query`).
  - `mnoslib.inc` is now a thin umbrella that `%include`s all four; programs
    that want a smaller footprint can include only the categories they need.
- **`doc/MNOSLIB.md`** — full catalog and usage guide for the helper library,
  including the placement rule (always `%include` AFTER `entry:`) and the
  rationale for keeping wrappers pure 1:1 with no convenience helpers
  (relocation pipeline simplicity).

### Changed
- **EDIT, BASIC, SYSINFO, MNMON, and the core of SHELL migrated to mnoslib.**
  Refactored ~334 raw `int 0x80/0x81/0x82` call sites to use named
  `call mn_*` helpers: `src/programs/edit/` (all .inc files including the
  `%ifdef EDIT_DEBUG` debug-trace blocks in `edit_find.inc`, ~26 sites),
  `src/programs/basic/` (all .inc files including the `%ifdef BASIC_DEBUG`
  trace blocks in `basic_load.inc`, ~23 sites), `src/programs/sysinfo/`
  (~169 sites), `src/programs/mnmon.asm` (~97 sites), and
  `src/shell/{shell.asm, shell_readline.inc, shell_cmd_simple.inc}`
  (~19 sites).  **All shipped user-mode `.MNX` programs are now 100%
  mnoslib-clean** — zero raw `int 0x8N` sites and zero raw BIOS interrupts
  remain in `src/programs/`, both in release and debug-trace builds.  The
  remaining SHELL command files (`shell_cmd_sysinfo.inc`,
  `shell_cmd_mem.inc`, `shell_cmd_dir.inc`, `shell_cmd_fs.inc`,
  `shell_cmd_run.inc`) still use raw syscalls; migration is mechanical and
  can happen incrementally.  Wrappers are purely additive — raw `int 0x80`
  continues to work unchanged.
- **BASIC's last two raw BIOS calls eliminated.**  `bas_stmt_cls` and
  `bas_stmt_locate` in `src/programs/basic/basic_stmt.inc` were the only
  sites in the entire apps + shell tree still calling `INT 0x10 AH=02h`
  (BIOS Set Cursor) directly, bypassing the kernel.  They now route through
  `mn_clear_screen` and `mn_set_cursor` respectively, so every user-mode
  hardware interaction in MNOS16 now goes through the kernel's syscall
  layer.  A grep for `int 0x1[0-9a-fA-F]` across `src/programs/` and
  `src/shell/` now returns zero matches.
- BASIC.MNX briefly grew to 22 sectors when the mnoslib umbrella was first
  included, then returned to 21 sectors once the `%ifdef BASIC_DEBUG` trace
  blocks in `basic_load.inc` were collapsed into `call mn_dbg_*` (each site
  shrinks by ~2 bytes vs. the inline `mov ah, SYS_DBG_* / int 0x80`).  Final
  MNX sizes vs. v0.9.17: EDIT.MNX 15 sectors (unchanged), BASIC.MNX 21
  sectors (unchanged), SYSINFO.MNX 7 sectors (unchanged), MNMON.MNX 6
  sectors (unchanged), SHELL.SYS unchanged.

### Added (tests)
- **Six new structural / regression tests under `tests/`** that guard the
  v0.9.18 mnoslib invariants against silent drift, raising the suite from
  254 to 275 passing tests:
  - `test_no_raw_bios_in_userland.py` — no `int 0x1[0-9a-fA-F]` anywhere
    in `src/programs/` or `src/shell/`.
  - `test_migrated_programs_use_wrappers.py` — EDIT / BASIC / SYSINFO /
    MNMON contain zero raw `int 0x8[012]` sites.
  - `test_mnoslib_wrapper_shape.py` — every `mn_*:` body is exactly
    `mov ah, CONST / int 0xN / ret` and the constant prefix matches the
    interrupt vector (`SYS_→0x80`, `FS_→0x81`, `MEM_→0x82`).
  - `test_mnoslib_syscall_coverage.py` — bijection between syscall
    constants in the headers and `mn_*` wrapper labels (no missing
    wrappers, no dangling wrappers, all `equ` aliases resolve).
  - `test_mnoslib_include_order.py` — `%include "mnoslib.inc"` always
    appears AFTER the program's first label.
  - `test_mnx_size_budgets.py` — every shipped `.MNX` stays within its
    per-binary sector budget and the global 60-sector TPA ceiling.

### Documentation
- New `doc/MNOSLIB.md` (described above), now also describing the six
  regression tests above in §6.
- README.md: bumped version banner to v0.9.18 and added MNOSLIB.md to the
  doc table.

---

## [0.9.17] - 2026-06-04

### Added
- **BASIC interpreter (`BASIC.MNX`)** — interactive line-numbered BASIC à la
  GW-BASIC, runnable from the shell with `basic` (REPL) or `basic FOO.BAS`
  (load + REPL).  Supports `PRINT`, `INPUT`, `LET`, `IF…THEN`, `FOR…NEXT`,
  `GOTO`, `GOSUB…RETURN`, `END`, `STOP`, `REM`, `CLS`, `RUN`, `LIST`, `NEW`,
  `LOAD`, `SAVE`, `FILES`, `HELP`, `SYSTEM`.  16-bit integer variables A-Z,
  string variables `A$-Z$`, arrays, FOR/NEXT loop nesting up to 8 deep,
  GOSUB stack 16 deep, central error trampoline with `ERR/ERL`.  Modules
  in `src/programs/basic/basic_*.inc`.  Pre-seeded `HELLO.BAS` and
  `GUESS.BAS` on the disk image.
- **`FS_REPLACE_FILE` syscall (INT 0x81 AH=0x09)** — atomic create-or-replace.
  Writes data to freshly-allocated sectors first, then updates the directory
  entry in a single flush.  If the data write fails, the existing file is
  untouched.  Refuses to replace files with `ATTR_SYSTEM`.  Old extent leaks
  on replace (acceptable in MNFS's append-only model).  Eliminates the
  delete-then-write footgun that caused the v0.9.16 BASIC SAVE corruption.
- **`mnoslib.inc` — user-mode helper library.**  Header-only library
  (`%include "mnoslib.inc"` in user programs) wrapping common FS syscalls
  with stable, documented names: `mn_save_file` (wraps `FS_REPLACE_FILE`)
  and `mn_load_file` (wraps `FS_READ_FILE`).  Include guard prevents double
  inclusion.  Must be included AFTER the program's `entry:` label (it emits
  real code that must not be reachable by fall-through from the loader's
  jump target).
- **FS ABI Contract v1** documented at the top of `src/fs/fs.asm` and in
  `doc/FILESYSTEM.md` §8.1.  Codifies that all INT 0x81 handlers preserve
  full 32-bit register width (push/pop `EAX/EBX/ECX/EDX`, not their 16-bit
  halves) except for documented outputs and `AL` on `CF=1`; that only `CF`
  is defined in FLAGS on return; and that each handler's memory side
  effects are listed in its docstring.
- **Per-handler clobber docstrings** rewritten for every FS handler
  (`FS_LIST_FILES` through `FS_REPLACE_FILE`) plus internal helpers
  (`fs_flush_dir`, `fs_recalc_total`).
- **AL printout in `fs_iret_cf_set`** debug path — `[FS] -> ERR AL=NN`
  makes it possible to distinguish DIR_FULL from DISK_FULL from IO from
  PROTECTED at a glance in serial logs.

### Changed
- **EDIT migrated to `mn_save_file`** — `ed_save_file` in
  `src/programs/edit/edit_file.inc` no longer does delete-then-write; it
  calls the atomic helper.  Sets `ES = DS` defensively before the call.
- **BASIC's `bas_save_file` migrated to `mn_save_file`** — same atomic save.
- **`FS_FIND_FILE` now adds `push si`/`pop si`** (was previously letting the
  caller's SI be clobbered along the not-found path).
- **`FS_FIND_BASE` preserves SI** in both match and not-found paths; its
  documented memory side effect (writes 3 extension bytes to the caller's
  buffer) is now spelled out.
- **`FS_READ_FILE`** saves caller SI to a local memory slot and restores it
  on all three exit paths; wraps `INT 0x13` with `push ds; push cs; pop ds;
  …; pop ds` to be DS-safe (was previously assuming caller's DS=0).
- **`FS_WRITE_FILE`, `FS_DELETE_FILE`, `FS_RENAME_FILE`** push/pop changed
  from 16-bit (`bx`, `cx`, `dx`, `di`) to 32-bit (`ebx`, `ecx`, `edx`,
  `edi`) — preserves the upper 16 bits of caller registers.
- **`fs_flush_dir`** internal helper preserves `EAX/EDX` (was only `AX/DX`).
- **`fs_recalc_total`** preserves `EAX/EBX/ECX/EDX/DI` (was only `DI/CX`).
- **Dispatcher** uses `cmp bx, FS_SYSCALL_MAX` instead of a hardcoded `8`
  for the debug trace range check.  `FS_SYSCALL_MAX` bumped to `0x09`.
- **BASIC `FILES` command** filters to `.BAS` files only (per request from
  the BASIC-interpreter user feedback).
- **BASIC startup banner** trimmed — removed the "(C) 2026 ..." line; the
  shorter `MNOS16 BASIC 1.0\n(C) 2025 BASIC for MNOS16 ...` was overkill.
- **BASIC `LOAD`/`SAVE`** accept quoted filenames (`load "hello.bas"`).

### Fixed
- **BASIC SAVE corruption** (v0.9.16-era footgun): `bas_save_file` kept
  the byte count in DX across `FS_DELETE_FILE`, which silently clobbered
  DX inside `fs_recalc_total` (via `movzx edx, word [es:di + MNFS_ENT_SECTORS]`).
  Result: the subsequent `FS_WRITE_FILE` wrote only 1 byte and SAVE on an
  existing file truncated it to a single byte.  Fix is twofold:
  (a) `fs_recalc_total` and `FS_DELETE_FILE` now preserve full 32-bit width
  of every non-output register; (b) the entire delete-then-write pattern
  was replaced with `FS_REPLACE_FILE` / `mn_save_file`, eliminating the
  hazard structurally.
- **BASIC `LOAD` triple-fault** on missing file — `bas_load_file` now
  uses the central `bas_error` trampoline so a missing file goes through
  the REPL recovery path rather than `iret`-ing from a stack mismatch.
- **BASIC `FOR` TYPE error** with simple integer counters — `for_pi_*`
  state slots were reading uninitialised memory on first iteration.
- **mnoslib placement** — initial integration placed
  `%include "mnoslib.inc"` BEFORE the program's `entry:` label.  The
  resulting binary started with `mov ah, FS_REPLACE_FILE; int 0x81` and
  the loader jumped straight into REPLACE_FILE with garbage arguments.
  Both `basic.asm` and `edit.asm` now include `mnoslib.inc` alongside
  the other code-bearing `.inc` modules at the bottom of the file.
- **`FS_READ_FILE` DAP DS** — INT 0x13's DAP pointer was previously
  read with the caller's DS, which only worked because the OS happens
  to run with `DS=0` most of the time.  Now wrapped with
  `push ds; push cs; pop ds; …; pop ds` for safety.

### Documentation
- `doc/FILESYSTEM.md` — updated function table for AH=0x05 (`FS_FIND_BASE`)
  and AH=0x09 (`FS_REPLACE_FILE`); added §8.1 FS ABI Contract subsection;
  rewrote §8.6 `FS_WRITE_FILE` ("creates new, rejects duplicates"); added
  §8.9 `FS_REPLACE_FILE` section; renumbered §8.10 Error Codes and §8.11
  Tombstone Semantics; fixed all cross-references.
- `doc/SYSTEM-CALLS.md` §7.x — FS write-syscall table extended with
  `FS_REPLACE_FILE`; added pointer to `mnoslib.inc` helpers.
- New: `doc/BASIC.md` — full reference for the BASIC interpreter
  (language, syntax, commands, examples, internals).
- `doc/EDITOR.md` — Save section updated to reference atomic save via
  `mn_save_file`.

---

## [0.9.16] - 2026-06-02

### Added
- Nested `SYS_SPAWN` support up to 4 levels deep via
  `spawn_parent_stack` (depth-indexed); MNMON can now spawn itself.

### Fixed
- Nested `SYS_SPAWN` crash (`#UD Invalid Opcode`) — each nested spawn
  overwrote `spawn_saved_ret` with the trampoline address instead of
  preserving the original shell return.  The outermost spawn now installs
  the trampoline exactly once; nested spawns skip trampoline setup.
- `SYS_SPAWN` failure left spawn depth and trampoline committed even when
  the child failed to load; added `spawn_rollback_if_pending`.
- Trampoline not re-installed after nested unwind back to a still-spawned
  parent.

---

## [0.9.15] - 2026-05-28

### Added
- **SYS_EXEC syscall (AH=0x27)** — allows a running program to replace itself
  with another .MNX program (overlay exec). New program inherits shell return
  frame and arguments. Caller is destroyed on success; returns CF+error code
  on failure (file not found, not executable, too large).
- **SYS_SPAWN syscall (AH=0x28)** — spawns a child program and reloads the
  caller from disk when the child exits. Supports nesting up to 4 levels deep
  via depth-indexed `spawn_parent_stack`. Enables debugger/monitor patterns
  (MNMON `x` command).
- **MNMON `x` command uses SYS_SPAWN** — launches a program, returns to MNMON
  when it finishes (MNMON survives across child execution).
- **Kernel `exec_parse_args`** — kernel-local argument tokenizer for SYS_EXEC,
  identical semantics to shell_parse_args but operates on kernel scratch space.
- **Kernel exec scratch data** — `exec_fname_buf` (11 bytes), `exec_args_buf`
  (128 bytes), `exec_entry_addr`, `spawn_parent_stack` (44 bytes).
- **17 new unit tests** for exec_parse_args and the SYS_EXEC binary contract
  (total: 234 tests).

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
- `syscalls.inc` — added SYS_EXEC (0x27), SYS_SPAWN (0x28), bumped SYSCALL_MAX to 0x28.
- `kernel_syscall.inc` — added .fn_exec, .fn_spawn handlers; modified .fn_exit
  to use `spawn_depth` as index into parent stack; distinguishes outermost
  (restore shell ret) vs. still-nested (re-install trampoline) cases.
- `kernel_data.inc` — added exec scratch space, `spawn_parent_stack`
  (44 bytes), `spawn_depth`, and `spawn_pending` flag.
- `programs/mnmon.asm` — `x` command now uses SYS_SPAWN (was SYS_EXEC);
  added `mnmon_fname` data for parent reload.
- `doc/SYSTEM-CALLS.md` — documented SYS_EXEC and SYS_SPAWN interfaces
  including nesting semantics and rollback.
- `doc/ABI.md` — bumped to v2.0: fixed stale syscall numbers (SYS_EXIT=0x23,
  SYS_GET_ARGC=0x25, SYS_GET_ARGV=0x26, SYS_GET_VERSION=0x05), added
  SYS_SPAWN section, documented trampoline/ret behavior.
- `doc/PROGRAM-LOADER.md` — added §6.3 SYS_EXEC, §6.4 rewritten for
  stack-based spawn model.

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
