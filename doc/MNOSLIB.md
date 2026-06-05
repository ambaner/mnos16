# MNOSLIB — User-Mode Helper Library

**File:** `src/include/mnoslib.inc` (umbrella) + four split headers
**Status:** Effective from MNOS16 v0.9.18
**Audience:** Authors of user programs (`.MNX`) and the SHELL.SYS module

---

## 1. What it is

`mnoslib` is a header-only library of thin assembly-language wrappers around
the kernel's three syscall interrupts:

| Interrupt | Subsystem            | Wrappers |
|-----------|----------------------|----------|
| `INT 0x80` | Kernel (console, system, process, debug) | `mnoslib_io.inc`, `mnoslib_sys.inc` |
| `INT 0x81` | Filesystem (MNFS via FS.SYS)             | `mnoslib_fs.inc` |
| `INT 0x82` | Memory manager (MM.SYS)                  | `mnoslib_mm.inc` |

Every wrapper is the assembly equivalent of

```nasm
    mov ah, SYS_FOO        ; or FS_FOO / MEM_FOO
    int 0x80               ; or 0x81 / 0x82
    ret
```

so the *only* job of a wrapper is to give the call site a named, self-documenting
function name instead of two raw lines of magic numbers.

There are **no convenience helpers** (no `mn_print_crlf`, no `mn_strlen`, no
error normalization).  That keeps the wrappers data-free, which means they
emit no cross-module relocations — important because SHELL.SYS is a
relocatable module and we don't want mnoslib to grow the relocation table.

---

## 2. How to use it

In the user program (`.MNX`) or in SHELL.SYS:

```nasm
    ; Constant headers — must come first.
    %include "syscalls.inc"     ; for SYS_* (needed by io/sys wrappers)
    %include "mnfs.inc"         ; for FS_*  (needed by fs wrappers)
    %include "memory.inc"       ; for MEM_* (needed by mm wrappers)

    ; ... entry: and program code ...

    ; mnoslib goes at the BOTTOM of the file, alongside other code-bearing
    ; .inc modules.  See "Placement rule" below.
    %include "mnoslib.inc"
```

Or include only the categories you actually need (saves a few hundred bytes):

```nasm
    %include "mnoslib_fs.inc"   ; just FS wrappers
    %include "mnoslib_io.inc"   ; just console/keyboard wrappers
```

Call sites are then one-liners:

```nasm
    mov si, my_message
    call mn_print_string

    call mn_read_key            ; AH=scancode, AL=ASCII

    mov dl, MCB_OWNER_SHELL
    mov cx, 256
    call mn_alloc               ; AX=seg, BX=off
```

### Placement rule (important)

mnoslib headers emit *real assembly code*.  If you `%include` them before
your program's `entry:` label, the MNEX loader will jump straight into the
first instruction of `mn_print_string` instead of your startup code.

Always include mnoslib AFTER `entry:`, alongside other code modules:

```nasm
[ORG 0x8000]
entry:
    ; ... your startup code ...
    call shell

%include "my_code.inc"
%include "my_data.inc"
%include "mnoslib.inc"          ; ← bottom of the file
```

---

## 3. Calling convention

Every wrapper inherits the preservation contract of its underlying syscall.
That means:

- Registers used to **return** values are clobbered (e.g. `AX` from
  `mn_read_key`, `BX:CX` from `mn_get_argv`, etc.).
- All other registers are preserved by the kernel's syscall epilogue.
- For FS wrappers specifically, the FS ABI Contract v1 applies: every
  register is preserved at full 32-bit width except documented outputs and
  `AL` on `CF=1`.  See `doc/FILESYSTEM.md` §8.1.
- `CF` is the only defined FLAGS bit on return.  Other flags are undefined.
- The wrapper itself adds no overhead beyond a near `call`/`ret` (3 bytes
  per call site, 5 bytes per wrapper body — break-even at ~5 call sites).

There is **no normalized error reporting**.  Callers should test `CF` after
the call exactly the way they would test it after a raw `int 0xNN`.

---

## 4. Catalog

### 4.1 `mnoslib_io.inc` — INT 0x80 console / keyboard

| Wrapper             | Wraps              | Inputs                              | Outputs                                        |
|---------------------|--------------------|-------------------------------------|------------------------------------------------|
| `mn_print_string`   | `SYS_PRINT_STRING` | `DS:SI` = NUL-terminated string     | none                                           |
| `mn_print_char`     | `SYS_PRINT_CHAR`   | `AL` = char                         | none                                           |
| `mn_print_hex8`     | `SYS_PRINT_HEX8`   | `AL` = byte                         | `AX` clobbered                                 |
| `mn_print_hex16`    | `SYS_PRINT_HEX16`  | `DX` = word                         | `AX` clobbered                                 |
| `mn_print_dec16`    | `SYS_PRINT_DEC16`  | `DX` = unsigned word                | `AX` clobbered                                 |
| `mn_read_key`       | `SYS_READ_KEY`     | —                                   | `AH` = scancode, `AL` = ASCII                  |
| `mn_peek_key`       | `SYS_PEEK_KEY`     | —                                   | `ZF=1` if no key, else `AH`/`AL` set            |
| `mn_wait_key`       | `SYS_WAIT_KEY`     | —                                   | none                                           |
| `mn_clear_screen`   | `SYS_CLEAR_SCREEN` | —                                   | none                                           |
| `mn_set_cursor`     | `SYS_SET_CURSOR`   | `DH` = row, `DL` = col              | none                                           |
| `mn_get_cursor`     | `SYS_GET_CURSOR`   | —                                   | `DH` = row, `DL` = col                         |

### 4.2 `mnoslib_sys.inc` — INT 0x80 system / process / debug

| Wrapper            | Wraps               | Inputs / Outputs                                                          |
|--------------------|---------------------|---------------------------------------------------------------------------|
| `mn_get_version`   | `SYS_GET_VERSION`   | → `AH`=major, `AL`=minor                                                  |
| `mn_get_bib`       | `SYS_GET_BIB`       | → `ES:BX` = BIB                                                           |
| `mn_get_equip`     | `SYS_GET_EQUIP`     | → `AX` = equipment word                                                   |
| `mn_get_video`     | `SYS_GET_VIDEO`     | → `AL`=mode, `AH`=cols, `BH`=page                                         |
| `mn_get_bda_byte`  | `SYS_GET_BDA_BYTE`  | `BX`=offset → `AL`=byte                                                   |
| `mn_get_bda_word`  | `SYS_GET_BDA_WORD`  | `BX`=offset → `AX`=word                                                   |
| `mn_check_a20`     | `SYS_CHECK_A20`     | → `AL`=1/0                                                                |
| `mn_get_conv_mem`  | `SYS_GET_CONV_MEM`  | → `AX`=KB                                                                 |
| `mn_get_ext_mem`   | `SYS_GET_EXT_MEM`   | → `AX`=KB, `CF`=1 on error                                                |
| `mn_get_e820`      | `SYS_GET_E820`      | `EBX`=continuation, `ES:DI`=buf → entry filled                            |
| `mn_get_drive_info`| `SYS_GET_DRIVE_INFO`| → drive geometry; `CF`=1 on error                                         |
| `mn_get_edd`       | `SYS_GET_EDD`       | `DL`=drive → EDD info; `CF`=1 on error                                    |
| `mn_get_ivt`       | `SYS_GET_IVT`       | `CL`=vector → `AX`=off, `DX`=seg                                          |
| `mn_check_cpuid`   | `SYS_CHECK_CPUID`   | → `AL`=1/0                                                                |
| `mn_cpuid`         | `SYS_CPUID`         | `EDI`=leaf → `EAX`/`EBX`/`ECX`/`EDX`                                      |
| `mn_read_sector`   | `SYS_READ_SECTOR`   | `EDI`=LBA, `ES:BX`=buf, `CL`=count → `CF`=1 on error                      |
| `mn_reboot`        | `SYS_REBOOT`        | does not return                                                           |
| `mn_exit`          | `SYS_EXIT`          | `AL`=code, does not return                                                |
| `mn_get_args`      | `SYS_GET_ARGS`      | → `SI`=ptr, `CX`=length                                                   |
| `mn_get_argc`      | `SYS_GET_ARGC`      | → `CL`=argc                                                               |
| `mn_get_argv`      | `SYS_GET_ARGV`      | `CL`=index → `SI`=ptr, `CX`=length                                        |
| `mn_exec`          | `SYS_EXEC`          | `DS:SI`=name, `DS:DI`=args → no return on success, `CF`=1 on fail         |
| `mn_spawn`         | `SYS_SPAWN`         | `DS:SI`=child, `DS:DI`=args, `DS:BX`=caller → `CF`=1 on fail              |
| `mn_dbg_print`     | `SYS_DBG_PRINT`     | `DS:SI`=msg, `DS:BX`=tag                                                  |
| `mn_dbg_hex16`     | `SYS_DBG_HEX16`     | `DX`=value, `DS:BX`=tag                                                   |
| `mn_dbg_regs`      | `SYS_DBG_REGS`      | `DS:BX`=tag                                                               |

### 4.3 `mnoslib_fs.inc` — INT 0x81 filesystem

| Wrapper             | Wraps              | Notes                                                              |
|---------------------|--------------------|--------------------------------------------------------------------|
| `mn_list_files`     | `FS_LIST_FILES`    | `ES:BX`=buf → `CL`=count (tombstones skipped)                       |
| `mn_find_file`      | `FS_FIND_FILE`     | `DS:SI`=11-byte name → `EAX`=LBA, `CX`=sectors, `BL`=attr           |
| `mn_find_base`      | `FS_FIND_BASE`     | `DS:SI`=8-byte basename → same returns                              |
| `mn_read_file`      | `FS_READ_FILE`     | `DS:SI`=name, `ES:BX`=buf, `CX`=maxsec → `AX`=bytes, `CX`=sec read |
| `mn_load_file`      | (alias of `mn_read_file`) | kept for v0.9.17 compatibility                              |
| `mn_get_fs_info`    | `FS_GET_INFO`      | → `AL`=ver, `CL`=count, `CH`=max, `DX`=total sectors used           |
| `mn_write_file`     | `FS_WRITE_FILE`    | creates new file; rejects duplicates                                |
| `mn_delete_file`    | `FS_DELETE_FILE`   | tombstones a slot; refuses `ATTR_SYSTEM`                            |
| `mn_rename_file`    | `FS_RENAME_FILE`   | `DS:SI`=old, `ES:DI`=new                                            |
| `mn_replace_file`   | `FS_REPLACE_FILE`  | atomic create-or-replace                                            |
| `mn_save_file`      | (alias of `mn_replace_file`) | kept for v0.9.17 compatibility                          |

All FS wrappers obey the **FS ABI Contract v1** — see `doc/FILESYSTEM.md` §8.1.

### 4.4 `mnoslib_mm.inc` — INT 0x82 memory manager

| Wrapper        | Wraps        | Inputs / Outputs                                                  |
|----------------|--------------|-------------------------------------------------------------------|
| `mn_alloc`     | `MEM_ALLOC`  | `CX`=size, `DL`=owner → `AX`=`HMA_SEG`, `BX`=offset; `CF`=1 on fail |
| `mn_free`      | `MEM_FREE`   | `BX`=pointer → `CF`=1 on fail                                       |
| `mn_avail`     | `MEM_AVAIL`  | → `AX`=largest, `DX`=total free                                     |
| `mn_mem_info`  | `MEM_INFO`   | → `AX`=total, `BX`=used, `CX`=free, `DX`=block count                |
| `mn_mem_query` | `MEM_QUERY`  | → `AX`=seg, `BX`=start offset, `CX`=heap size                       |

---

## 5. Adopted by

As of v0.9.18:

- `src/programs/edit/` — fully migrated (~13 call sites)
- `src/programs/basic/` — fully migrated (~16 call sites + 2 remaining raw
  `INT 0x10` BIOS sites eliminated in `basic_stmt.inc`).  BASIC now makes
  **zero** direct BIOS or VGA-buffer accesses; every hardware-touching
  operation routes through the kernel's syscall layer.
- `src/programs/sysinfo/` — fully migrated (~169 call sites).  The hardware
  diagnostic program now uses `mn_check_cpuid`, `mn_cpuid`, `mn_get_bib`,
  `mn_get_bda_byte/word`, `mn_check_a20`, `mn_get_conv_mem`,
  `mn_get_ext_mem`, `mn_get_e820`, `mn_get_drive_info`, `mn_get_edd`,
  `mn_get_ivt`, `mn_get_video`, `mn_get_cursor`, etc. throughout.
- `src/programs/mnmon.asm` — fully migrated (~97 call sites).  The
  interactive machine monitor now goes through `mn_print_string`,
  `mn_print_char`, `mn_print_hex8/16`, `mn_read_key`, `mn_get_args`,
  `mn_exec`, `mn_spawn`, and the FS wrappers for its `dir`/`x` commands.
- `src/shell/shell.asm`, `shell_readline.inc`, `shell_cmd_simple.inc` —
  migrated (~19 call sites) as the proof-of-concept inside a relocatable
  module.

**BIOS-interrupt usage in apps + shell:** a survey of
`int 0x1[0-9a-fA-F]` across `src/programs/` and `src/shell/` returns
**zero** matches as of v0.9.18.  All disk, video, keyboard, RTC, and
equipment access from user-mode code now goes through `INT 0x80/0x81/0x82`
(typically via `mn_*` wrappers).  Raw BIOS interrupts remain available
inside the kernel modules (`KERNEL.SYS`, `FS.SYS`, `MM.SYS`) where they
implement the syscall abstractions themselves.

**Intentionally not migrated** (raw `int 0xNN` still works — wrappers are
purely additive):

- `src/shell/shell_cmd_sysinfo.inc` (168 sites)
- `src/shell/shell_cmd_mem.inc` (68 sites)
- `src/shell/shell_cmd_dir.inc` (27 sites)
- `src/shell/shell_cmd_fs.inc` (24 sites)
- `src/shell/shell_cmd_run.inc` (21 sites)

All of these are SHELL.SYS command files; migrating them is a mechanical
follow-up task with no design risk; do it when convenient (e.g., next time
the file is touched for an unrelated bug).  All user-mode `.MNX` programs
shipped in the v0.9.18 build — including the `%ifdef EDIT_DEBUG` and
`%ifdef BASIC_DEBUG` blocks in `src/programs/edit/edit_find.inc` and
`src/programs/basic/basic_load.inc` — are 100% mnoslib-clean.

## 6. Regression tests

Six static / structural tests in `tests/` guard the mnoslib invariants
against silent drift:

| Test file                                 | Asserts                                                                 |
| ----------------------------------------- | ----------------------------------------------------------------------- |
| `test_no_raw_bios_in_userland.py`         | No `int 0x1[0-9a-fA-F]` anywhere in `src/programs/` or `src/shell/`.    |
| `test_migrated_programs_use_wrappers.py`  | EDIT / BASIC / SYSINFO / MNMON contain zero raw `int 0x8[012]` sites.   |
| `test_mnoslib_wrapper_shape.py`           | Every `mn_*:` body is exactly `mov ah, CONST / int 0xN / ret`, and the constant prefix matches the interrupt vector (`SYS_→0x80`, `FS_→0x81`, `MEM_→0x82`). |
| `test_mnoslib_syscall_coverage.py`        | Bijection between syscall constants in the `.inc` headers and `mn_*` wrapper labels (no missing wrappers, no dangling wrappers, all `equ` aliases resolve). |
| `test_mnoslib_include_order.py`           | In every program that pulls in `mnoslib.inc`, the `%include` appears AFTER the first label (so the MNEX loader's jump never lands inside wrapper code). |
| `test_mnx_size_budgets.py`                | Every shipped `.MNX` stays within its per-binary sector budget and the global `USER_PROG_MAX_SEC = 60` TPA ceiling.  Bumping a budget requires editing the `BUDGETS` dict, which forces explicit code-review acknowledgement. |

All six pass on every CI run; together they make the v0.9.18 migration
regression-proof.

---

## 6. Why no convenience helpers?

Earlier drafts of mnoslib considered:

- `mn_print_crlf` — print 13, 10
- `mn_strlen` — count chars until NUL
- `mn_print_dec_dword` — print a 32-bit decimal

All three were rejected for v1 because:

1. **They require internal data labels** (e.g., a `db 13, 10, 0` for CRLF,
   string offsets for length tables).  Internal data references inside a
   relocatable module like SHELL.SYS produce relocation table entries.  The
   `gen_relocs.py` pipeline would have to track them, and the per-call
   benefit is tiny.
2. **They are not syscall wrappers.**  Pure assembly helpers belong in
   per-program `.inc` files or a separate utility module, not in mnoslib.
   Conflating the two makes the contract harder to reason about.
3. **YAGNI.**  Adding helpers later is trivial and additive.  Adding them
   now and never using them is wasted code.

If a real need emerges (e.g., five programs all reinvent the same helper),
move it into a new `mnoslib_util.inc` with a clearly documented contract.

---

## 7. References

- `doc/SYSTEM-CALLS.md` — full INT 0x80 / 0x81 / 0x82 syscall reference.
- `doc/FILESYSTEM.md` — MNFS layout and FS ABI Contract v1.
- `doc/MEMORY-MANAGER.md` — HMA heap and MCB layout.
- `doc/EDITOR.md`, `doc/BASIC.md` — example consumers of `mn_save_file` /
  `mn_load_file` / `mn_read_key`.
