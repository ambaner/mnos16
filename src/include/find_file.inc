; =============================================================================
; find_file.inc — Bootstrap MNFS directory lookup subroutine
;
; This file is %included by VBR, LOADER, and KERNEL to provide boot-time
; filesystem access.  Each including file gets its own copy in the assembled
; output (same pattern as load_binary.inc).
;
; After boot completes, user-mode programs use FS.SYS's INT 0x81 interface
; instead.  This include is the "bootstrap filesystem reader" — just enough
; to find a file by name in the MNFS directory table.
;
; find_file — Look up a file in the MNFS directory table
;
; Reads the MNFS directory sector from disk (partition offset MNFS_DIR_SECTOR),
; verifies the 'MNFS' magic, and searches for a file matching the given
; 11-byte 8.3 filename.
;
; Input:
;   BX  = scratch buffer offset (512 bytes, segment 0x0000) for directory read
;   DS:SI = pointer to 11-byte filename to find (8 name + 3 ext, space-padded)
;
; Output:
;   CF clear = found:
;     EAX = partition-relative start sector (from directory entry)
;     CX  = file size in sectors (from directory entry)
;   CF set   = not found or error (disk read failed, bad magic, file missing)
;
; Clobbers: AX, CX, DI
; Preserves: BX, DX, SI, BP, ES, DS
;
; Requirements:
;   - mnfs.inc must be included (provides MNFS_DIR_SECTOR, MNFS_MAGIC, etc.)
;   - bib.inc must be included (provides BIB_PART_LBA, BIB_DRIVE)
;   - The including file must define: dap, dap_sectors, dap_buffer, dap_lba
; =============================================================================

find_file:
    push di
    push dx
    push si
    push bx

    ; --- Save the filename pointer for later search --------------------------
    mov [.ff_name_ptr], si

    ; --- Read directory sector from disk -------------------------------------
    ; Calculate absolute LBA = partition_lba + MNFS_DIR_SECTOR
    mov eax, MNFS_DIR_SECTOR
    add eax, [BIB_PART_LBA]
    mov [dap_lba], eax
    mov dword [dap_lba+4], 0        ; High 32 bits = 0

    mov word [dap_sectors], MNFS_DIR_SECTORS
    mov [dap_buffer], bx
    mov word [dap_buffer+2], 0x0000  ; Segment = 0

    mov dl, [BIB_DRIVE]
    mov si, dap
    mov ah, 0x42
    int 0x13
    jc .ff_fail                     ; Disk read error

    ; --- Verify MNFS magic at start of directory -----------------------------
    mov di, bx                      ; DI → directory buffer
    cmp dword [di], MNFS_MAGIC      ; Check for 'MNFS'
    jne .ff_fail                    ; Bad magic

    ; --- Get file count from header ------------------------------------------
    movzx cx, byte [di + MNFS_HDR_COUNT]
    test cx, cx
    jz .ff_fail                     ; No files in directory

    ; --- Search entries (starting at header + 32 bytes) ----------------------
    add di, MNFS_HDR_SIZE           ; DI → first directory entry

.ff_search:
    push cx                         ; Save remaining count
    push di                         ; Save entry pointer

    ; Compare 11 bytes of filename (entry vs. search key)
    mov si, [.ff_name_ptr]          ; SI → filename to find
    mov cx, MNFS_NAME_LEN           ; 11 bytes to compare
    repe cmpsb
    je .ff_found                    ; Match!

    ; No match — advance to next entry
    pop di
    pop cx
    add di, MNFS_ENTRY_SIZE         ; DI → next entry
    dec cx
    jnz .ff_search

    ; --- Not found -----------------------------------------------------------
    jmp .ff_fail

.ff_found:
    pop di                          ; DI → matching entry start
    pop cx                          ; Discard saved count

    ; --- Extract start sector and size from entry ----------------------------
    mov eax, [di + MNFS_ENT_START]  ; EAX = partition-relative start sector
    mov cx, [di + MNFS_ENT_SECTORS]        ; CX = size in sectors

    ; --- Success —  restore and return with CF clear -------------------------
    pop bx
    pop si
    pop dx
    pop di
    clc
    ret

.ff_fail:
    pop bx
    pop si
    pop dx
    pop di
    stc
    ret

; --- Local storage (placed after ret, never executed as code) ----------------
.ff_name_ptr:  dw 0                 ; Saved pointer to search filename
