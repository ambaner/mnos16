; =============================================================================
; mnoslib.inc — User-mode helper library for MNOS16 programs (umbrella header)
;
; This file is a thin umbrella that `%include`s the four category-specific
; mnoslib headers.  User programs that want everything do:
;
;       %include "mnoslib.inc"      ; pulls in io/sys/fs/mm
;
; Programs that want to keep their binary as small as possible should include
; only the categories they actually use:
;
;       %include "mnoslib_io.inc"   ; INT 0x80 console / keyboard helpers
;       %include "mnoslib_sys.inc"  ; INT 0x80 system / process / debug
;       %include "mnoslib_fs.inc"   ; INT 0x81 filesystem
;       %include "mnoslib_mm.inc"   ; INT 0x82 memory manager
;
; All wrappers are 1:1 with their underlying syscall.  Each wrapper is the
; assembly equivalent of `mov ah, NN / int N / ret` and inherits the
; preservation contract of the syscall it wraps.  No internal data labels,
; no convenience helpers — those would introduce cross-module relocations.
;
; PLACEMENT RULE: include AFTER the program's `entry:` label, alongside
; other code-bearing `.inc` modules.  The wrappers emit real code and the
; MNEX loader will execute the very first byte of the program — if mnoslib
; comes before `entry:`, the loader jumps straight into `mov ah, FS_*`
; instead of your program's startup code.
;
; Dependencies: caller must `%include` the relevant constant headers first:
;   syscalls.inc  — for SYS_* constants (used by io/sys wrappers)
;   mnfs.inc      — for FS_* constants (used by fs wrappers)
;   memory.inc    — for MEM_* constants (used by mm wrappers)
;
; See doc/MNOSLIB.md for the full catalog with preservation contracts.
; =============================================================================

%ifndef MNOSLIB_INC_INCLUDED
%define MNOSLIB_INC_INCLUDED

%include "mnoslib_io.inc"
%include "mnoslib_sys.inc"
%include "mnoslib_fs.inc"
%include "mnoslib_mm.inc"

%endif ; MNOSLIB_INC_INCLUDED
