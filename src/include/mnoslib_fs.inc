; =============================================================================
; mnoslib_fs.inc — User-mode wrappers for INT 0x81 filesystem syscalls.
;
; All wrappers obey the FS ABI Contract v1 (see top of src/fs/fs.asm):
; full 32-bit register preservation except documented outputs and AL on CF=1.
;
; Dependencies: %include "mnfs.inc" before this file.
; =============================================================================

%ifndef MNOSLIB_FS_INC_INCLUDED
%define MNOSLIB_FS_INC_INCLUDED

; -----------------------------------------------------------------------------
; mn_list_files — Copy the MNFS directory into a caller buffer.
; Input:    ES:BX = buffer (at least MNFS_MAX_ENTRIES * MNFS_ENTRY_SIZE bytes)
; Output:   CL = entry count copied (tombstones skipped)
; -----------------------------------------------------------------------------
mn_list_files:
    mov ah, FS_LIST_FILES
    int 0x81
    ret

; -----------------------------------------------------------------------------
; mn_find_file — Locate a file by 11-byte name.
; Input:    DS:SI = 11-byte name (8.3, space-padded, uppercase)
; Output:   EAX = start LBA, CX = sector count, BL = attribute
;           CF=1 + AL=FS_ERR_NOT_FOUND on miss
; -----------------------------------------------------------------------------
mn_find_file:
    mov ah, FS_FIND_FILE
    int 0x81
    ret

; -----------------------------------------------------------------------------
; mn_find_base — Locate a file by 8-byte base name (any extension).
; Input:    DS:SI = 8-byte base name (space-padded)
; Output:   EAX = start LBA, CX = sector count, BL = attribute
;           CF=1 on miss
; -----------------------------------------------------------------------------
mn_find_base:
    mov ah, FS_FIND_BASE
    int 0x81
    ret

; -----------------------------------------------------------------------------
; mn_read_file — Read a file into a caller buffer in one call.
; Input:    DS:SI = 11-byte filename
;           ES:BX = destination buffer
;           CX    = max sectors caller's buffer can hold
; Output on CF=0: AX = file size in bytes (low 16),
;                 CX = sectors actually read.
; Output on CF=1: AL = FS_ERR_*.
; -----------------------------------------------------------------------------
mn_read_file:
    mov ah, FS_READ_FILE
    int 0x81
    ret

; Back-compat alias used by EDIT/BASIC before mnoslib was split.
mn_load_file equ mn_read_file

; -----------------------------------------------------------------------------
; mn_get_fs_info — Query MNFS volume info.
; Output:   AL = MNFS version, CL = file count, CH = max entries,
;           DX = total sectors used
; -----------------------------------------------------------------------------
mn_get_fs_info:
    mov ah, FS_GET_INFO
    int 0x81
    ret

; -----------------------------------------------------------------------------
; mn_write_file — Create a new file.  Rejects duplicates (FS_ERR_EXISTS).
; Input:    DS:SI = 11-byte filename
;           ES:BX = data buffer
;           ECX   = file size in bytes
;           DL    = attribute byte
; Output:   CF=1 + AL = FS_ERR_* on failure.
; -----------------------------------------------------------------------------
mn_write_file:
    mov ah, FS_WRITE_FILE
    int 0x81
    ret

; -----------------------------------------------------------------------------
; mn_delete_file — Tombstone-mark a file (name[0] = 0xE5).
; Input:    DS:SI = 11-byte filename
; Output:   CF=1 + AL = FS_ERR_* on failure.  Refuses ATTR_SYSTEM.
; -----------------------------------------------------------------------------
mn_delete_file:
    mov ah, FS_DELETE_FILE
    int 0x81
    ret

; -----------------------------------------------------------------------------
; mn_rename_file — Rename a file in place.
; Input:    DS:SI = old 11-byte name, ES:DI = new 11-byte name
; Output:   CF=1 + AL = FS_ERR_* on failure.
; -----------------------------------------------------------------------------
mn_rename_file:
    mov ah, FS_RENAME_FILE
    int 0x81
    ret

; -----------------------------------------------------------------------------
; mn_replace_file — Atomic create-or-replace.  Writes payload to fresh
; sectors first, then flips the directory entry.  Refuses ATTR_SYSTEM.
; Input:    DS:SI = 11-byte filename
;           ES:BX = data buffer
;           ECX   = file size in bytes (0 = empty entry only)
;           DL    = attribute byte
; Output:   CF=1 + AL = FS_ERR_* on failure.
; -----------------------------------------------------------------------------
mn_replace_file:
    mov ah, FS_REPLACE_FILE
    int 0x81
    ret

; Original public name from the v0.9.17 release; kept as an alias.
mn_save_file equ mn_replace_file

%endif ; MNOSLIB_FS_INC_INCLUDED
