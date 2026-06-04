; =============================================================================
; mnoslib.inc — User-mode helper library for MNOS16 programs
;
; This is a header-only library: it is `%include`d into each user program
; and assembled into the program's binary.  Future versions may move to a
; separate loadable module if size becomes a concern.
;
; All helpers obey the same calling convention as the underlying syscalls
; they wrap: AL is clobbered on CF=1 (error code); all other registers are
; preserved.  Helpers themselves push/pop everything they touch internally
; so callers can rely on the documented inputs/outputs only.
;
; Currently provided (FS only — INT 0x80 wrappers are out of scope here):
;   mn_save_file     — Atomic create-or-replace using FS_REPLACE_FILE
;   mn_load_file     — Read a file in one call (FIND + READ)
;
; Dependencies: caller must `%include "mnfs.inc"` first for the FS_* constants.
; =============================================================================

%ifndef MNOSLIB_INC_INCLUDED
%define MNOSLIB_INC_INCLUDED

; =============================================================================
; mn_save_file — Atomic save (create-or-replace) wrapper around FS_REPLACE_FILE.
;
; Input:    DS:SI = 11-byte filename (8.3, space-padded, uppercase)
;           ES:BX = data buffer
;           ECX   = file size in bytes (0 = empty entry only)
;           DL    = attribute byte
; Output on CF=0: success.
; Output on CF=1: AL = FS_ERR_* code.
; Preserves: all registers (full 32-bit width) EXCEPT AL on error.
; =============================================================================
mn_save_file:
    mov ah, FS_REPLACE_FILE
    int 0x81
    ret

; =============================================================================
; mn_load_file — Convenience wrapper for FS_READ_FILE.
; (FS_READ_FILE already does the directory lookup internally; this wrapper
; exists for symmetry with mn_save_file and to give a stable user-mode name.)
;
; Input:    DS:SI = 11-byte filename
;           ES:BX = destination buffer (large enough for the file)
;           CX    = max sectors caller's buffer can hold
; Output on CF=0: AX = file size in bytes (low 16),
;                 CX = sectors actually read.
; Output on CF=1: file not found or disk I/O error.
; Preserves: SI, BX, DX, DI, ES, DS (only AX/CX returned).
; =============================================================================
mn_load_file:
    mov ah, FS_READ_FILE
    int 0x81
    ret

%endif ; MNOSLIB_INC_INCLUDED
