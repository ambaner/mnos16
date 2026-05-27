; =============================================================================
; MNOS16 Filesystem Module (FS.SYS) - MNFS Driver
;
; Relocatable system module — loaded by KERNEL.SYS at a dynamic address
; (currently 0x0800, replacing LOADER.SYS's slot).  Assembled with ORG 0
; and relocated at load time via MNEX v2 header.
;
; Provides filesystem services via INT 0x81 — fully decoupled from the
; kernel's INT 0x80 interface.
;
; FS.SYS uses the kernel's INT 0x80 SYS_READ_SECTOR for disk I/O, creating
; a clean layered architecture:
;   User mode (SHELL)  →  INT 0x81  →  FS.SYS  →  INT 0x80  →  KERNEL  →  BIOS
;
; Initialization:
;   The kernel calls our init entry point after loading and relocating.
;   Init installs INT 0x81 in the IVT and caches the MNFS directory.
;
; INT 0x81 functions (AH = function number):
;   0x01  FS_LIST_FILES  — Copy cached directory to caller's buffer
;   0x02  FS_FIND_FILE   — Search for file by 8.3 name
;   0x03  FS_READ_FILE   — Read file contents into buffer
;   0x04  FS_GET_INFO    — Return filesystem metadata
;
; See doc/FILESYSTEM.md for the complete specification.
;
; Build: assembled by gen_relocs.py + pack_module.py (see tools/build.ps1)
; =============================================================================

%include "bib.inc"
%include "mnfs.inc"
%include "syscalls.inc"
%include "debug.inc"

[BITS 16]

; Relocatable module — assembled at ORG 0, relocated at load time.
; The MNEX v2 header and relocation table are added by pack_module.py.
%ifndef RELOC_BASE
%define RELOC_BASE 0
%endif
[ORG RELOC_BASE]

; =============================================================================
; fs_init — Initialize the filesystem module
;
; Called by the kernel after loading FS.SYS into memory.
;   1. Installs INT 0x81 handler in the IVT
;   2. Reads the MNFS directory via INT 0x80 SYS_READ_SECTOR
;   3. Validates the directory magic
;   4. Caches the directory data internally
;
; Input:  none
; Output: CF clear = success, CF set = error
; Clobbers: AX, CX
; =============================================================================
fs_init:
    ; --- Install INT 0x81 handler in the IVT ---------------------------------
    cli                             ; Disable interrupts while modifying IVT
    push es

    xor ax, ax
    mov es, ax                      ; ES = 0x0000 (IVT segment)

    ; Vector 0x81 is at address 0x81 * 4 = 0x0204
    mov word [es:0x81*4],   fs_syscall_handler  ; Offset
    mov word [es:0x81*4+2], cs                   ; Segment

    pop es
    sti                             ; Re-enable interrupts

    ; --- Read MNFS directory sector via kernel's SYS_READ_SECTOR -------------
    ; The directory is at partition sector MNFS_DIR_SECTOR.  We need the
    ; absolute LBA = partition_lba + MNFS_DIR_SECTOR.
    mov edi, [BIB_PART_LBA]
    add edi, MNFS_DIR_SECTOR        ; EDI = absolute LBA of directory

    push ds
    pop es                          ; ES = DS (our segment)
    mov bx, dir_cache               ; ES:BX → our internal cache buffer
    mov cl, MNFS_DIR_SECTORS        ; Read 1 sector

    mov ah, SYS_READ_SECTOR         ; Kernel syscall for disk read
    int 0x80
    jc .init_fail                   ; Disk read error
    ASSERT_CF_CLEAR "FS directory sector read failed"

    ; --- Validate MNFS magic in the cached directory -------------------------
    cmp dword [dir_cache], MNFS_MAGIC
    jne .init_fail
    ASSERT_MAGIC dir_cache, MNFS_MAGIC, "MNFS directory magic mismatch"

    ; --- Cache the file count for quick access --------------------------------
    mov al, [dir_cache + MNFS_HDR_COUNT]
    mov [cached_count], al

    ; --- Success -------------------------------------------------------------
    clc
    ret

.init_fail:
    stc
    ret

; =============================================================================
; fs_syscall_handler — INT 0x81 dispatcher
;
; Dispatches filesystem syscalls based on AH function number.
; All functions operate on the cached directory data (no disk reads
; needed except FS_READ_FILE which reads file contents).
; =============================================================================
fs_syscall_handler:
%ifdef DEBUG
    inc byte [cs:BIB_INT_DEPTH]        ; Track total INT nesting (shared counter)
    push si
    push ax
    push bx

    mov si, .fs_trace_pfx           ; "[FS] "
    call serial_puts

    movzx bx, ah
    cmp bx, 8
    ja .fs_trace_noname
    shl bx, 1
    mov si, [cs:.fs_name_table + bx]
    test si, si
    jz .fs_trace_noname
    call serial_puts
    jmp .fs_trace_done

.fs_trace_noname:
    mov si, .fs_trace_ah            ; "AH="
    call serial_puts
    mov al, ah
    call serial_hex8

.fs_trace_done:
    call serial_crlf
    pop bx
    pop ax
    pop si
%endif

    cmp ah, FS_LIST_FILES
    je .fn_list_files
    cmp ah, FS_FIND_FILE
    je .fn_find_file
    cmp ah, FS_READ_FILE
    je .fn_read_file
    cmp ah, FS_GET_INFO
    je .fn_get_info
    cmp ah, FS_FIND_BASE
    je .fn_find_base
    cmp ah, FS_WRITE_FILE
    je .fn_write_file
    cmp ah, FS_DELETE_FILE
    je .fn_delete_file
    cmp ah, FS_RENAME_FILE
    je .fn_rename_file

    ; Unknown function
    jmp fs_iret_cf_set

%ifdef DEBUG
.fs_trace_pfx: db '[FS] ', 0
.fs_trace_ah:  db 'AH=', 0
.fsn_01: db 'LIST_FILES', 0
.fsn_02: db 'FIND_FILE', 0
.fsn_03: db 'READ_FILE', 0
.fsn_04: db 'GET_INFO', 0
.fsn_05: db 'FIND_BASE', 0
.fsn_06: db 'WRITE_FILE', 0
.fsn_07: db 'DELETE_FILE', 0
.fsn_08: db 'RENAME_FILE', 0
.fs_name_table:
    dw 0            ; 0x00 — unused
    dw .fsn_01      ; 0x01
    dw .fsn_02      ; 0x02
    dw .fsn_03      ; 0x03
    dw .fsn_04      ; 0x04
    dw .fsn_05      ; 0x05
    dw .fsn_06      ; 0x06
    dw .fsn_07      ; 0x07
    dw .fsn_08      ; 0x08
%endif

; ─── FS_LIST_FILES (AH=0x01) ─────────────────────────────────────────────────
; Copy the cached 512-byte directory sector to the caller's buffer.
; Input:  ES:BX = 512-byte destination buffer
; Output: CL = file count, CF clear
; ──────────────────────────────────────────────────────────────────────────────
.fn_list_files:
    push ax
    push si
    push di
    push cx
    push ds

    ; Set up source: DS:SI → our cached directory
    push cs
    pop ds                          ; DS = CS (FS.SYS's segment)
    mov si, dir_cache

    ; Set up destination: ES:BX → caller's buffer
    mov di, bx

    ; Copy 512 bytes (256 words)
    mov cx, 256
    rep movsw

    pop ds
    pop cx
    mov cl, [cs:cached_count]       ; CL = file count
    pop di
    pop si
    pop ax
    jmp fs_iret_cf_clear

; ─── FS_FIND_FILE (AH=0x02) ──────────────────────────────────────────────────
; Search cached directory for a file by 11-byte 8.3 name.
; Input:  DS:SI = pointer to 11-byte filename (8+3, space-padded, uppercase)
; Output: CF clear = found:
;           EAX = start sector (partition-relative)
;           CX  = size in sectors
;           EDX = size in bytes
;           DL (low byte of EDX) also available; DH clobbered
;           BL  = attribute byte
;         CF set = not found
; ──────────────────────────────────────────────────────────────────────────────
.fn_find_file:
    push di
    push bx

    ; Save caller's filename pointer
    mov [cs:.ff_caller_si], si

    ; Scan all 15 possible directory slots (not just cached_count)
    mov cx, MNFS_MAX_ENTRIES

    ; DI → first entry in cache (CS-relative)
    ; We need to compare DS:SI (caller's name) with CS:entry
    ; Use ES:DI for our cache since caller owns DS
    push es
    push cs
    pop es                          ; ES = CS (our cache segment)
    mov di, dir_cache + MNFS_HDR_SIZE

.ff_loop:
    push cx
    push di

    ; Skip deleted/empty entries
    cmp byte [es:di], MNFS_DELETED
    je .ff_skip
    cmp byte [es:di], 0x00
    je .ff_skip

    ; Compare 11 bytes: DS:SI (caller) vs ES:DI (our cache entry)
    mov si, [cs:.ff_caller_si]      ; Restore SI each iteration
    mov cx, MNFS_NAME_LEN
    repe cmpsb
    je .ff_match

.ff_skip:
    pop di
    pop cx
    add di, MNFS_ENTRY_SIZE
    dec cx
    jnz .ff_loop

    pop es
    jmp .ff_not_found

.ff_match:
    pop di                          ; DI → matching entry start
    pop cx                          ; Discard count

    ; Save attribute byte to temp (before we clobber registers)
    push ax
    mov al, [es:di + MNFS_ENT_ATTR]
    mov [cs:.ff_attr_tmp], al
    pop ax

    ; Extract fields from the matched entry (ES:DI relative)
    mov eax, [es:di + MNFS_ENT_START]
    mov cx, [es:di + MNFS_ENT_SECTORS]
    mov edx, [es:di + MNFS_ENT_BYTES]

    pop es                          ; Restore caller's ES
    pop bx
    pop di

    ; Return attribute in BL
    mov bl, [cs:.ff_attr_tmp]
    jmp fs_iret_cf_clear

.ff_not_found:
    pop bx
    pop di
    jmp fs_iret_cf_set

.ff_caller_si: dw 0                 ; Saved caller's filename pointer
.ff_attr_tmp:  db 0                 ; Temp storage for attribute byte

; ─── FS_FIND_BASE (AH=0x05) ─────────────────────────────────────────────────
; Find a file by its 8-byte base name only (ignores extension).
; Returns the first matching entry regardless of extension.
; Input:  DS:SI = 8-byte space-padded base name
; Output: CF clear = found
;           EAX = start sector (partition-relative)
;           CX  = file size in sectors
;           EDX = file size in bytes
;           BL  = attribute byte
;         CF set = not found
; ──────────────────────────────────────────────────────────────────────────────
.fn_find_base:
    push di

    ; Save caller's name pointer
    mov [cs:.fb_caller_si], si

    ; Scan all 15 possible directory slots
    mov cx, MNFS_MAX_ENTRIES

    push es
    push cs
    pop es                          ; ES = CS (our cache segment)
    mov di, dir_cache + MNFS_HDR_SIZE

.fb_loop:
    push cx
    push di

    ; Skip deleted/empty entries
    cmp byte [es:di], MNFS_DELETED
    je .fb_skip
    cmp byte [es:di], 0x00
    je .fb_skip

    ; Compare only first 8 bytes (name portion, not extension)
    mov si, [cs:.fb_caller_si]
    mov cx, 8
    repe cmpsb
    je .fb_match

.fb_skip:
    pop di
    pop cx
    add di, MNFS_ENTRY_SIZE
    dec cx
    jnz .fb_loop

    pop es
    jmp .fb_not_found

.fb_match:
    pop di                          ; DI → matching entry start
    pop cx                          ; Discard count

    ; Copy found extension (bytes 8-10) back into caller's name buffer
    ; so caller can use full 11-byte name for FS_READ_FILE
    push si
    mov si, [cs:.fb_caller_si]
    mov al, [es:di + 8]             ; Extension byte 0
    mov [ds:si + 8], al
    mov al, [es:di + 9]             ; Extension byte 1
    mov [ds:si + 9], al
    mov al, [es:di + 10]            ; Extension byte 2
    mov [ds:si + 10], al
    pop si

    ; Save attribute byte
    push ax
    mov al, [es:di + MNFS_ENT_ATTR]
    mov [cs:.fb_attr_tmp], al
    pop ax

    ; Extract fields from matched entry
    mov cx, [es:di + MNFS_ENT_SECTORS]
    mov edx, [es:di + MNFS_ENT_BYTES]
    mov eax, [es:di + MNFS_ENT_START]

    pop es                          ; Restore caller's ES
    pop di

    ; Return attribute in BL
    mov bl, [cs:.fb_attr_tmp]
    jmp fs_iret_cf_clear

.fb_not_found:
    pop di
    jmp fs_iret_cf_set

.fb_caller_si: dw 0
.fb_attr_tmp:  db 0

; ─── FS_READ_FILE (AH=0x03) ──────────────────────────────────────────────────
; Read a file's contents from disk into the caller's buffer.
; Internally finds the file, then uses INT 0x80 SYS_READ_SECTOR.
; Input:  DS:SI = 11-byte filename
;         ES:BX = buffer to read into
;         CX    = maximum sectors to read
; Output: CF clear = success, CX = sectors actually read
;         CF set   = error (file not found or disk I/O error)
; ──────────────────────────────────────────────────────────────────────────────
.fn_read_file:
    push dx
    push ax

    ; Save caller's buffer and max sectors
    mov [cs:.rf_buf_off], bx
    mov [cs:.rf_buf_seg], es
    mov [cs:.rf_max], cx

    ; Find the file first (reuse our own find logic)
    ; DS:SI already points to filename
    push di
    push bx
    mov cx, MNFS_MAX_ENTRIES

    push es
    push cs
    pop es
    mov di, dir_cache + MNFS_HDR_SIZE

.rf_search:
    push cx
    push di
    push si                         ; Save SI for each iteration

    ; Skip deleted/empty entries
    cmp byte [es:di], MNFS_DELETED
    je .rf_skip_entry
    cmp byte [es:di], 0x00
    je .rf_skip_entry

    mov cx, MNFS_NAME_LEN
    repe cmpsb
    je .rf_found

.rf_skip_entry:
    pop si
    pop di
    pop cx
    add di, MNFS_ENTRY_SIZE
    dec cx
    jnz .rf_search

    pop es
    jmp .rf_not_found

.rf_found:
    pop si                          ; Discard saved SI
    pop di                          ; DI → matched entry
    pop cx                          ; Discard count

    ; Read file metadata BEFORE loading EDI (which clobbers DI)
    mov cx, [es:di + MNFS_ENT_SECTORS]
    mov eax, [es:di + MNFS_ENT_BYTES]  ; EAX = file size in bytes
    mov [cs:.rf_bytes], eax
    mov edi, [es:di + MNFS_ENT_START]
    pop es                          ; Restore caller's ES

    ; Clamp to caller's max sectors
    jbe .rf_size_ok
    mov cx, [cs:.rf_max]
.rf_size_ok:
    mov [cs:.rf_actual], cx

    ; Calculate absolute LBA = partition_lba + start_sector
    add edi, [BIB_PART_LBA]

    ; Read via direct INT 0x13 (avoids nested INT 0x80 which causes DMA errors
    ; in Hyper-V due to triple-nested interrupt context)
    xor ch, ch
    mov cl, [cs:.rf_actual]         ; CX = sectors to read
    mov [cs:.rf_dap_lba], edi
    mov [cs:.rf_dap_sectors], cx
    mov ax, [cs:.rf_buf_off]
    mov [cs:.rf_dap_buf], ax
    mov ax, [cs:.rf_buf_seg]
    mov [cs:.rf_dap_buf+2], ax

%ifdef DEBUG
    ; --- DAP dump before INT 0x13 ---
    push cx
    push si
    mov si, fs_dbg_dap_pfx          ; "[FS] DAP: "
    call serial_puts
    mov si, .rf_dap
    mov cx, 16
.rf_dap_dump:
    lodsb
    call serial_hex8
    mov al, ' '
    call serial_putc
    loop .rf_dap_dump
    call serial_crlf
    pop si
    pop cx
%endif

    mov si, .rf_dap                 ; DS:SI → our DAP (DS=0, flat model)
    mov dl, [BIB_DRIVE]
    mov ah, 0x42
    sti                             ; BIOS needs interrupts for DMA
    int 0x13
    jc .rf_disk_err

%ifdef DEBUG
    push si
    mov si, fs_dbg_read_ok
    call serial_puts
    call serial_crlf
    pop si
%endif

    ; Success — return AX=bytes, CX=sectors
    mov cx, [cs:.rf_actual]
    mov ax, [cs:.rf_bytes]          ; AX = file size in bytes (low 16 bits)
    pop bx
    pop di
    add sp, 2                       ; Discard saved AX (replaced with byte count)
    pop dx
    jmp fs_iret_cf_clear

.rf_not_found:
%ifdef DEBUG
    push si
    mov si, fs_dbg_rf_nf            ; "[FS] RF: not_found"
    call serial_puts
    call serial_crlf
    pop si
%endif
    pop bx
    pop di
    pop ax
    pop dx
    jmp fs_iret_cf_set

.rf_disk_err:
%ifdef DEBUG
    push si
    push ax
    mov si, fs_dbg_disk_err         ; "[FS] INT13 ERR AH="
    call serial_puts
    mov al, ah
    call serial_hex8
    call serial_crlf
    pop ax
    pop si
%endif
    pop bx
    pop di
    pop ax
    pop dx
    jmp fs_iret_cf_set

.rf_buf_off:  dw 0
.rf_buf_seg:  dw 0
.rf_max:      dw 0
.rf_actual:   dw 0
.rf_bytes:    dd 0

; Local DAP for direct INT 0x13 (avoids nested INT 0x80)
.rf_dap:
    db 0x10, 0                      ; Size=16, reserved=0
.rf_dap_sectors:
    dw 0                            ; Sector count
.rf_dap_buf:
    dw 0, 0                         ; Buffer offset, segment
.rf_dap_lba:
    dd 0, 0                         ; 64-bit LBA

; ─── FS_GET_INFO (AH=0x04) ───────────────────────────────────────────────────
; Return filesystem metadata.
; Input:  none
; Output: AL = MNFS version, CL = file count, CH = max entries (15)
;         DX = total sectors used, BX = total data area capacity (sectors)
; ──────────────────────────────────────────────────────────────────────────────
.fn_get_info:
    mov al, [cs:dir_cache + MNFS_HDR_VERSION]
    mov cl, [cs:cached_count]
    mov ch, MNFS_MAX_ENTRIES
    mov dx, [cs:dir_cache + MNFS_HDR_TOTAL]
    mov bx, [cs:dir_cache + MNFS_HDR_CAPACITY]
    jmp fs_iret_cf_clear

; ─── FS_WRITE_FILE (AH=0x06) ─────────────────────────────────────────────────
; Create a new file on disk (append-only allocation).
; Input:  DS:SI = 11-byte filename (8.3, space-padded, uppercase)
;         ES:BX = data buffer to write
;         ECX   = file size in bytes (0 = empty directory entry only)
;         DL    = attribute byte
; Output: CF clear = success
;         CF set   = error, AL = error code (FS_ERR_*)
; ──────────────────────────────────────────────────────────────────────────────
.fn_write_file:
    push es
    push di
    push si
    push bx
    push dx

    ; --- Save caller parameters -----------------------------------------------
    mov [cs:.wf_caller_si], si
    mov [cs:.wf_buf_off], bx
    mov [cs:.wf_buf_seg], es
    mov [cs:.wf_size_bytes], ecx
    mov [cs:.wf_attr], dl

    ; --- Validate filename (first byte must not be 0x00, 0xE5) ----------------
    cmp byte [ds:si], 0x00
    je .wf_err_invalid
    cmp byte [ds:si], MNFS_DELETED
    je .wf_err_invalid

    ; --- Check if file already exists (scan all 15 entries) -------------------
    push cs
    pop es                          ; ES = CS for cache access
    mov di, dir_cache + MNFS_HDR_SIZE
    mov cx, MNFS_MAX_ENTRIES

.wf_dup_check:
    cmp byte [es:di], MNFS_DELETED
    je .wf_dup_skip
    cmp byte [es:di], 0x00
    je .wf_dup_skip

    ; Compare 11 bytes
    push cx
    push di
    mov si, [cs:.wf_caller_si]
    mov cx, MNFS_NAME_LEN
    repe cmpsb
    pop di
    pop cx
    je .wf_err_exists               ; Found duplicate name

.wf_dup_skip:
    add di, MNFS_ENTRY_SIZE
    dec cx
    jnz .wf_dup_check

    ; --- Find a free directory slot (0x00 or 0xE5) ----------------------------
    mov di, dir_cache + MNFS_HDR_SIZE
    mov cx, MNFS_MAX_ENTRIES
    xor bx, bx                     ; BX = 0 means "no free slot found"

.wf_slot_scan:
    cmp byte [es:di], 0x00
    je .wf_slot_found
    cmp byte [es:di], MNFS_DELETED
    je .wf_slot_found
    add di, MNFS_ENTRY_SIZE
    dec cx
    jnz .wf_slot_scan

    ; No free slot
    jmp .wf_err_dir_full

.wf_slot_found:
    mov [cs:.wf_slot_di], di        ; Save slot offset

    ; --- Calculate sectors needed = (ECX + 511) / 512 -------------------------
    mov eax, [cs:.wf_size_bytes]
    add eax, 511
    shr eax, 9                      ; EAX = sectors needed
    mov [cs:.wf_sectors], ax

    ; --- Calculate append sector (high-water mark from total_sectors header) ---
    ; Append after all existing data: start = MNFS_DIR_SECTOR + total_sectors
    movzx eax, word [cs:dir_cache + MNFS_HDR_TOTAL]
    add eax, MNFS_DIR_SECTOR        ; EAX = next free partition-relative sector
    mov [cs:.wf_start], eax

    ; --- Check disk capacity --------------------------------------------------
    movzx ebx, word [cs:dir_cache + MNFS_HDR_CAPACITY]
    ; Used sectors = total_sectors + MNFS_DIR_SECTORS (directory itself)
    movzx ecx, word [cs:dir_cache + MNFS_HDR_TOTAL]
    add ecx, MNFS_DIR_SECTORS
    movzx edx, word [cs:.wf_sectors]
    add ecx, edx                    ; ECX = total after write
    cmp ecx, ebx
    ja .wf_err_disk_full

    ; --- Write data sectors to disk (if any) ----------------------------------
    cmp word [cs:.wf_sectors], 0
    je .wf_update_dir               ; Zero-length file, skip disk write

    ; Calculate absolute LBA = partition_lba + start sector
    mov edi, [cs:.wf_start]
    add edi, [BIB_PART_LBA]

    ; Write in chunks of MNFS_WRITE_CHUNK sectors
    mov cx, [cs:.wf_sectors]
    mov ax, [cs:.wf_buf_seg]
    mov [cs:.wf_dap_buf+2], ax
    mov ax, [cs:.wf_buf_off]
    mov [cs:.wf_dap_buf], ax

.wf_write_loop:
    ; Determine chunk size (min of remaining, MNFS_WRITE_CHUNK)
    mov ax, cx
    cmp ax, MNFS_WRITE_CHUNK
    jbe .wf_chunk_ok
    mov ax, MNFS_WRITE_CHUNK
.wf_chunk_ok:
    mov [cs:.wf_dap_sectors], ax
    mov [cs:.wf_dap_lba], edi
    mov dword [cs:.wf_dap_lba+4], 0

    ; Issue INT 0x13 AH=0x43 (extended write)
    push cx
    push si
    push ds
    push cs
    pop ds                          ; DS = CS for DAP access
    mov si, .wf_dap
    mov dl, [BIB_DRIVE]
    mov ah, 0x43
    xor al, al                      ; AL=0: no verify
    sti
    int 0x13
    pop ds
    pop si
    pop cx
    jc .wf_err_io

    ; Advance buffer pointer and LBA
    movzx eax, word [cs:.wf_dap_sectors]
    sub cx, ax                      ; Remaining sectors
    jz .wf_update_dir               ; Done writing

    ; Advance LBA
    add edi, eax

    ; Advance buffer (sectors * 512 bytes)
    shl ax, 9                       ; AX = bytes written this chunk
    add [cs:.wf_dap_buf], ax        ; Advance buffer offset
    ; Handle segment wrap (if buffer offset > 0xFFFF)
    jnc .wf_write_loop
    ; Wrapped — adjust segment
    mov ax, [cs:.wf_dap_buf+2]
    add ax, 0x1000                  ; Advance segment by 64K
    mov [cs:.wf_dap_buf+2], ax
    jmp .wf_write_loop

.wf_update_dir:
    ; --- Update directory entry in cache --------------------------------------
    push cs
    pop es
    mov di, [cs:.wf_slot_di]

    ; Write filename (11 bytes)
    push ds
    pop es                          ; Temporarily ES = DS for movsb... no wait
    ; Actually we need to copy from DS:caller_si to CS:di
    ; Let's do it manually
    push cs
    pop es                          ; ES = CS (our cache)
    mov si, [cs:.wf_caller_si]
    mov cx, MNFS_NAME_LEN
.wf_copy_name:
    mov al, [ds:si]
    mov [es:di], al
    inc si
    inc di
    dec cx
    jnz .wf_copy_name

    ; DI now at offset +11 (attributes)
    mov al, [cs:.wf_attr]
    mov [es:di], al                 ; Attribute
    inc di

    ; Start sector (4 bytes) at offset +12
    mov eax, [cs:.wf_start]
    mov [es:di], eax
    add di, 4

    ; Size in sectors (2 bytes) at offset +16
    mov ax, [cs:.wf_sectors]
    mov [es:di], ax
    add di, 2

    ; Size in bytes (4 bytes) at offset +18
    mov eax, [cs:.wf_size_bytes]
    mov [es:di], eax
    add di, 4

    ; Reserved (10 bytes) at offset +22 — zero fill
    xor al, al
    mov cx, 10
.wf_zero_reserved:
    mov [es:di], al
    inc di
    dec cx
    jnz .wf_zero_reserved

    ; --- Update header: file_count++, total_sectors += new sectors -------------
    inc byte [cs:dir_cache + MNFS_HDR_COUNT]
    inc byte [cs:cached_count]
    mov ax, [cs:dir_cache + MNFS_HDR_TOTAL]
    add ax, [cs:.wf_sectors]
    mov [cs:dir_cache + MNFS_HDR_TOTAL], ax

    ; --- Flush directory to disk ----------------------------------------------
    call fs_flush_dir
    jc .wf_err_io_post              ; Flush failed (dir mutated but disk out of sync)

    ; --- Success --------------------------------------------------------------
    pop dx
    pop bx
    pop si
    pop di
    pop es
    jmp fs_iret_cf_clear

.wf_err_invalid:
    mov al, FS_ERR_EXISTS           ; Invalid name (reuse code 2 for simplicity)
    jmp .wf_fail
.wf_err_exists:
    mov al, FS_ERR_EXISTS
    jmp .wf_fail
.wf_err_dir_full:
    mov al, FS_ERR_DIR_FULL
    jmp .wf_fail
.wf_err_disk_full:
    mov al, FS_ERR_DISK_FULL
    jmp .wf_fail
.wf_err_io:
.wf_err_io_post:
    mov al, FS_ERR_IO
.wf_fail:
    pop dx
    pop bx
    pop si
    pop di
    pop es
    jmp fs_iret_cf_set

; FS_WRITE_FILE local data
.wf_caller_si:   dw 0
.wf_buf_off:     dw 0
.wf_buf_seg:     dw 0
.wf_size_bytes:  dd 0
.wf_attr:        db 0
.wf_sectors:     dw 0
.wf_start:       dd 0
.wf_slot_di:     dw 0

; Write DAP (16 bytes)
.wf_dap:
    db 0x10, 0                      ; Size=16, reserved=0
.wf_dap_sectors: dw 0
.wf_dap_buf:     dw 0, 0           ; Buffer offset, segment
.wf_dap_lba:     dd 0, 0           ; 64-bit LBA

; ─── FS_DELETE_FILE (AH=0x07) ─────────────────────────────────────────────────
; Delete a file by marking its directory entry as a tombstone.
; System files (ATTR_SYSTEM) cannot be deleted.
; If the file is the physically last one, space is reclaimed.
; Input:  DS:SI = 11-byte filename
; Output: CF clear = success
;         CF set   = error, AL = error code
; ──────────────────────────────────────────────────────────────────────────────
.fn_delete_file:
    push es
    push di
    push bx
    push cx

    ; Save caller's filename
    mov [cs:.df_caller_si], si

    ; --- Find the file --------------------------------------------------------
    push cs
    pop es
    mov di, dir_cache + MNFS_HDR_SIZE
    mov cx, MNFS_MAX_ENTRIES

.df_search:
    cmp byte [es:di], MNFS_DELETED
    je .df_skip
    cmp byte [es:di], 0x00
    je .df_skip

    ; Compare 11 bytes
    push cx
    push di
    mov si, [cs:.df_caller_si]
    mov cx, MNFS_NAME_LEN
    repe cmpsb
    pop di
    pop cx
    je .df_found

.df_skip:
    add di, MNFS_ENTRY_SIZE
    dec cx
    jnz .df_search

    ; Not found
    mov al, FS_ERR_NOT_FOUND
    jmp .df_fail

.df_found:
    ; --- Check if system file (cannot delete) ---------------------------------
    test byte [es:di + MNFS_ENT_ATTR], MNFS_ATTR_SYSTEM
    jnz .df_err_protected

    ; --- Mark as deleted (tombstone) ------------------------------------------
    mov byte [es:di], MNFS_DELETED

    ; --- Decrement active file count ------------------------------------------
    dec byte [cs:dir_cache + MNFS_HDR_COUNT]
    dec byte [cs:cached_count]

    ; --- Recalculate high-water mark (total_sectors) --------------------------
    ; Scan all entries, find max(start + sectors) as the new high-water
    call fs_recalc_total

    ; --- Flush directory to disk ----------------------------------------------
    call fs_flush_dir
    jc .df_err_io

    ; --- Success --------------------------------------------------------------
    pop cx
    pop bx
    pop di
    pop es
    jmp fs_iret_cf_clear

.df_err_protected:
    mov al, FS_ERR_PROTECTED
    jmp .df_fail
.df_err_io:
    mov al, FS_ERR_IO
.df_fail:
    pop cx
    pop bx
    pop di
    pop es
    jmp fs_iret_cf_set

.df_caller_si: dw 0

; ─── FS_RENAME_FILE (AH=0x08) ────────────────────────────────────────────────
; Rename a file (directory entry update only, no disk data changes).
; Input:  DS:SI = 11-byte old filename
;         ES:DI = 11-byte new filename
; Output: CF clear = success
;         CF set   = error, AL = error code
; ──────────────────────────────────────────────────────────────────────────────
.fn_rename_file:
    push es
    push di
    push bx
    push cx

    ; Save caller parameters
    mov [cs:.rn_old_si], si
    mov [cs:.rn_new_off], di
    mov [cs:.rn_new_seg], es

    ; --- Validate new name (first byte not 0x00, not 0xE5) --------------------
    cmp byte [es:di], 0x00
    je .rn_err_invalid
    cmp byte [es:di], MNFS_DELETED
    je .rn_err_invalid

    ; --- Check new name doesn't already exist ---------------------------------
    push cs
    pop es
    mov di, dir_cache + MNFS_HDR_SIZE
    mov cx, MNFS_MAX_ENTRIES

.rn_dup_check:
    cmp byte [es:di], MNFS_DELETED
    je .rn_dup_skip
    cmp byte [es:di], 0x00
    je .rn_dup_skip

    ; Compare 11 bytes against new name
    push cx
    push di
    push ds
    mov ax, [cs:.rn_new_seg]
    mov ds, ax
    mov si, [cs:.rn_new_off]
    mov cx, MNFS_NAME_LEN
    repe cmpsb
    pop ds
    pop di
    pop cx
    je .rn_err_exists

.rn_dup_skip:
    add di, MNFS_ENTRY_SIZE
    dec cx
    jnz .rn_dup_check

    ; --- Find old name --------------------------------------------------------
    mov di, dir_cache + MNFS_HDR_SIZE
    mov cx, MNFS_MAX_ENTRIES

.rn_find_old:
    cmp byte [es:di], MNFS_DELETED
    je .rn_old_skip
    cmp byte [es:di], 0x00
    je .rn_old_skip

    push cx
    push di
    mov si, [cs:.rn_old_si]
    mov cx, MNFS_NAME_LEN
    repe cmpsb
    pop di
    pop cx
    je .rn_old_found

.rn_old_skip:
    add di, MNFS_ENTRY_SIZE
    dec cx
    jnz .rn_find_old

    ; Old file not found
    mov al, FS_ERR_NOT_FOUND
    jmp .rn_fail

.rn_old_found:
    ; DI → matched entry in cache (ES=CS)
    ; Copy new name (11 bytes) from caller's buffer into cache entry
    push ds
    mov ax, [cs:.rn_new_seg]
    mov ds, ax
    mov si, [cs:.rn_new_off]
    mov cx, MNFS_NAME_LEN
.rn_copy_name:
    mov al, [ds:si]
    mov [es:di], al
    inc si
    inc di
    dec cx
    jnz .rn_copy_name
    pop ds

    ; --- Flush directory to disk ----------------------------------------------
    call fs_flush_dir
    jc .rn_err_io

    ; --- Success --------------------------------------------------------------
    pop cx
    pop bx
    pop di
    pop es
    jmp fs_iret_cf_clear

.rn_err_invalid:
    mov al, FS_ERR_EXISTS
    jmp .rn_fail
.rn_err_exists:
    mov al, FS_ERR_EXISTS
    jmp .rn_fail
.rn_err_io:
    mov al, FS_ERR_IO
.rn_fail:
    pop cx
    pop bx
    pop di
    pop es
    jmp fs_iret_cf_set

.rn_old_si:  dw 0
.rn_new_off: dw 0
.rn_new_seg: dw 0

; =============================================================================
; fs_flush_dir — Write the cached directory sector back to disk
;
; Uses INT 0x13 AH=0x43 (extended write) to write the 512-byte dir_cache
; back to the MNFS directory sector on disk.
;
; Input:  none (uses dir_cache in CS)
; Output: CF clear = success, CF set = disk error
; Clobbers: AX (preserved otherwise)
; =============================================================================
fs_flush_dir:
    push ax
    push si
    push dx
    push ds

    ; Set up DAP for directory write
    mov dword [cs:.fd_dap_lba+4], 0
    mov word [cs:.fd_dap_sectors], 1
    ; Buffer = CS:dir_cache
    mov ax, cs
    mov [cs:.fd_dap_buf+2], ax
    mov word [cs:.fd_dap_buf], dir_cache

    ; LBA = partition_lba + MNFS_DIR_SECTOR
    mov eax, [BIB_PART_LBA]
    add eax, MNFS_DIR_SECTOR
    mov [cs:.fd_dap_lba], eax

    ; Issue INT 0x13 AH=0x43
    push cs
    pop ds
    mov si, .fd_dap
    mov dl, [BIB_DRIVE]
    mov ah, 0x43
    xor al, al
    sti
    int 0x13

    pop ds
    pop dx
    pop si
    pop ax
    ret                             ; CF from INT 0x13 propagates

.fd_dap:
    db 0x10, 0                      ; Size=16, reserved
.fd_dap_sectors: dw 0
.fd_dap_buf:     dw 0, 0
.fd_dap_lba:     dd 0, 0

; =============================================================================
; fs_recalc_total — Recalculate total_sectors (high-water mark)
;
; Scans all directory entries and finds max(start_sector + size_sectors)
; relative to first data sector. Updates dir_cache header.
;
; Input:  ES = CS (cache segment)
; Output: [dir_cache + MNFS_HDR_TOTAL] updated
; Clobbers: AX, BX, CX, DI
; =============================================================================
fs_recalc_total:
    push di
    push cx

    mov di, dir_cache + MNFS_HDR_SIZE
    mov cx, MNFS_MAX_ENTRIES
    xor bx, bx                     ; BX = high-water mark (in sectors from dir)

.rt_loop:
    ; Skip free/deleted entries
    cmp byte [es:di], 0x00
    je .rt_next
    cmp byte [es:di], MNFS_DELETED
    je .rt_next

    ; Compute end = start_sector + size_sectors - MNFS_DIR_SECTOR
    ; total_sectors = end_sector - MNFS_DIR_SECTOR (relative to dir start)
    mov eax, [es:di + MNFS_ENT_START]     ; start (partition-relative)
    movzx edx, word [es:di + MNFS_ENT_SECTORS]
    add eax, edx                          ; EAX = end sector (partition-relative)
    sub eax, MNFS_DIR_SECTOR              ; EAX = relative to directory
    ; Compare with current high-water
    cmp ax, bx
    jbe .rt_next
    mov bx, ax                            ; New high-water

.rt_next:
    add di, MNFS_ENTRY_SIZE
    dec cx
    jnz .rt_loop

    ; BX = new total_sectors (could be 0 if all files deleted)
    ; But we must include the directory sector itself in total
    ; Actually total_sectors in header = sectors used by files + dir sector
    ; No — looking at create-disk.ps1: total_sectors = MNFS_DIR_SECTORS + totalDataSectors
    ; So the high-water is just end - (MNFS_DIR_SECTOR + MNFS_DIR_SECTORS) + MNFS_DIR_SECTORS
    ; Simpler: total_sectors = (max end sector) - (MNFS_DIR_SECTOR + MNFS_DIR_SECTORS) + MNFS_DIR_SECTORS
    ;        = (max end sector) - MNFS_DIR_SECTOR
    ; Wait no. Let me look at the header definition:
    ; MNFS_HDR_TOTAL = total sectors used (directory + all files)
    ; create-disk.ps1: $totalSectors = $MNFS_DIR_SECTORS + $totalDataSectors
    ; So total = 1 (dir) + sum of all file sectors
    ; For high-water: total = (max_end - first_data_sector) + MNFS_DIR_SECTORS
    ;   first_data_sector = MNFS_DIR_SECTOR + MNFS_DIR_SECTORS = 3
    ;   total = (max_end - 3) + 1 = max_end - 2 = max_end - MNFS_DIR_SECTOR

    ; BX already = max_end - MNFS_DIR_SECTOR (from above subtraction)
    ; But we need total_sectors = MNFS_DIR_SECTORS + data_sectors_high_water
    ; data_sectors_high_water = max_end - (MNFS_DIR_SECTOR + MNFS_DIR_SECTORS)
    ; total_sectors = MNFS_DIR_SECTORS + max_end - MNFS_DIR_SECTOR - MNFS_DIR_SECTORS
    ;              = max_end - MNFS_DIR_SECTOR
    ; Which is what BX already holds! Good.

    mov [cs:dir_cache + MNFS_HDR_TOTAL], bx

    pop cx
    pop di
    ret

; =============================================================================
; DATA
; =============================================================================
cached_count:  db 0                  ; Cached file count (from directory header)

; --- Directory cache (512 bytes — holds the full MNFS directory sector) -------
; This is read once during init and used for all subsequent lookups.
dir_cache:
    times 512 db 0

; =============================================================================
; IRET Helpers — properly propagate CF via the interrupt stack frame
;
; Problem: `clc; iret` does NOT work because `iret` pops FLAGS from the stack
; (the caller's saved FLAGS), ignoring the current FLAGS register.
; Solution: Use `retf 2` which pops IP and CS but DISCARDS the saved FLAGS
; (adds 2 to SP), preserving the handler's current FLAGS (including CF).
; `sti` is needed because `int` clears IF on entry.
;
; This matches the kernel's syscall_ret_cf pattern (see kernel.asm §comment).
; =============================================================================

; Clear CF in handler FLAGS and return from interrupt
fs_iret_cf_clear:
%ifdef DEBUG
    push si
    mov si, fs_dbg_ret_ok
    call serial_puts
    call serial_crlf
    pop si
    dec byte [cs:BIB_INT_DEPTH]        ; Track total INT nesting
%endif
    clc
    sti
    retf 2

; Set CF in handler FLAGS and return from interrupt
fs_iret_cf_set:
%ifdef DEBUG
    push si
    mov si, fs_dbg_ret_err
    call serial_puts
    call serial_crlf
    pop si
    dec byte [cs:BIB_INT_DEPTH]        ; Track total INT nesting
%endif
    stc
    sti
    retf 2

%ifdef DEBUG
fs_dbg_ret_ok:   db '[FS] -> OK', 0
fs_dbg_ret_err:  db '[FS] -> ERR (CF=1)', 0
fs_dbg_read_ok:  db '[FS] READ_SECTOR OK', 0
fs_dbg_dap_pfx:  db '[FS] DAP: ', 0
fs_dbg_disk_err: db '[FS] INT13 ERR AH=', 0
fs_dbg_rf_nf:    db '[FS] RF: not_found', 0
%endif

; =============================================================================
; Serial I/O functions (debug build only — placed after FS code to avoid
; polluting the header at offset 0)
; =============================================================================
%include "serial.inc"

; =============================================================================
; END OF MODULE — no padding; pack_module.py handles sector alignment
; =============================================================================
