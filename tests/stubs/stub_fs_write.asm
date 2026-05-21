; =============================================================================
; stub_fs_write.asm — Test harness for FS write/delete/rename routines
;
; Provides entry points that exercise the directory manipulation logic
; without needing real INT 0x13 disk I/O.  Instead of INT 0x13, we mock
; the disk operations by copying data to/from a "virtual disk" buffer in
; memory at VDISK_BASE.
;
; The directory cache (dir_cache) is initialized by the test harness before
; calling entry points.  Disk writes are captured in the virtual disk area.
;
; Entry points (at known offsets from ORG 0x1000):
;   0x1000 = fs_write_entry  — DS:SI=name, ES:BX=buf, ECX=bytes, DL=attr
;   0x1020 = fs_delete_entry — DS:SI=name
;   0x1040 = fs_rename_entry — DS:SI=old, ES:DI=new
;   0x1060 = fs_find_entry   — DS:SI=name → EAX,CX,EDX,BL
;   0x1080 = fs_init_dir     — Initialize dir_cache from VDISK directory
;   0x10A0 = fs_get_dir_cache — Returns BX=dir_cache address
;
; Virtual disk layout (at VDISK_BASE = 0x4000):
;   The harness pre-populates this with a valid MNFS directory at offset 0
;   followed by file data.  Writes go here too.
;
; Memory layout:
;   0x1000–0x2FFF  Code + data (this stub)
;   0x4000–0x7FFF  Virtual disk (16 KB)
;   0x0600         BIB (partition_lba = 0, drive = 0x80)
; =============================================================================

[BITS 16]
[ORG 0x1000]

%include "mnfs.inc"
%include "memory.inc"

; --- Constants for the test stub ---
VDISK_BASE      equ 0x4000           ; Virtual disk starts here
VDISK_DIR_OFF   equ (MNFS_DIR_SECTOR * 512)  ; Directory at sector 2 offset
VDISK_DATA_OFF  equ ((MNFS_DIR_SECTOR + MNFS_DIR_SECTORS) * 512)  ; Data at sector 3

; =============================================================================
; Entry: fs_write (offset 0x00)
; Input: DS:SI=name, ES:BX=buf, ECX=bytes, DL=attr
; Output: CF=result, AL=error code
; =============================================================================
fs_write_entry:
    call fs_write_impl
    hlt

    times 0x20 - ($ - fs_write_entry) db 0x90

; =============================================================================
; Entry: fs_delete (offset 0x20)
; Input: DS:SI=name
; Output: CF=result, AL=error code
; =============================================================================
fs_delete_entry:
    call fs_delete_impl
    hlt

    times 0x20 - ($ - fs_delete_entry) db 0x90

; =============================================================================
; Entry: fs_rename (offset 0x40)
; Input: DS:SI=old name, ES:DI=new name
; Output: CF=result, AL=error code
; =============================================================================
fs_rename_entry:
    call fs_rename_impl
    hlt

    times 0x20 - ($ - fs_rename_entry) db 0x90

; =============================================================================
; Entry: fs_find (offset 0x60)
; Input: DS:SI=11-byte name
; Output: CF clear=found (EAX=start, CX=sectors, EDX=bytes, BL=attr)
;         CF set=not found
; =============================================================================
fs_find_entry:
    call fs_find_impl
    hlt

    times 0x20 - ($ - fs_find_entry) db 0x90

; =============================================================================
; Entry: fs_init_dir (offset 0x80)
; Load dir_cache from VDISK directory sector
; =============================================================================
fs_init_dir_entry:
    call fs_init_dir_impl
    hlt

    times 0x20 - ($ - fs_init_dir_entry) db 0x90

; =============================================================================
; Entry: fs_get_dir_cache (offset 0xA0)
; Returns BX = address of dir_cache
; =============================================================================
fs_get_dir_cache_entry:
    mov bx, dir_cache
    hlt

    times 0x20 - ($ - fs_get_dir_cache_entry) db 0x90

; =============================================================================
; IMPLEMENTATION — fs_init_dir_impl
; Copies 512 bytes from VDISK_BASE + VDISK_DIR_OFF into dir_cache
; =============================================================================
fs_init_dir_impl:
    push si
    push di
    push cx
    push es

    push ds
    pop es                          ; ES = DS = 0

    mov si, VDISK_BASE + VDISK_DIR_OFF
    mov di, dir_cache
    mov cx, 256
    rep movsw

    ; Cache file count
    mov al, [dir_cache + MNFS_HDR_COUNT]
    mov [cached_count], al

    pop es
    pop cx
    pop di
    pop si
    ret

; =============================================================================
; IMPLEMENTATION — fs_find_impl
; Search cached directory for a file by 11-byte name, skipping tombstones.
; =============================================================================
fs_find_impl:
    push di

    mov [.fi_caller_si], si

    mov cx, MNFS_MAX_ENTRIES
    mov di, dir_cache + MNFS_HDR_SIZE

.fi_loop:
    ; Skip deleted/empty
    cmp byte [di], MNFS_DELETED
    je .fi_skip
    cmp byte [di], 0x00
    je .fi_skip

    ; Compare 11 bytes
    push cx
    push di
    mov si, [.fi_caller_si]
    mov cx, MNFS_NAME_LEN
    repe cmpsb
    pop di
    pop cx
    je .fi_found

.fi_skip:
    add di, MNFS_ENTRY_SIZE
    dec cx
    jnz .fi_loop

    ; Not found
    pop di
    stc
    ret

.fi_found:
    ; Extract fields
    mov eax, [di + MNFS_ENT_START]
    mov cx, [di + MNFS_ENT_SECTORS]
    mov edx, [di + MNFS_ENT_BYTES]
    mov bl, [di + MNFS_ENT_ATTR]
    pop di
    clc
    ret

.fi_caller_si: dw 0

; =============================================================================
; IMPLEMENTATION — fs_write_impl
; Create a new file. Simplified version that writes to VDISK instead of INT 0x13.
; =============================================================================
fs_write_impl:
    push es
    push di
    push si
    push bx
    push dx

    ; Save parameters
    mov [.wi_caller_si], si
    mov [.wi_buf_off], bx
    mov [.wi_buf_seg], es
    mov [.wi_size_bytes], ecx
    mov [.wi_attr], dl

    ; Validate name
    cmp byte [ds:si], 0x00
    je .wi_err_invalid
    cmp byte [ds:si], MNFS_DELETED
    je .wi_err_invalid

    ; Check duplicate (scan all 15 entries)
    mov di, dir_cache + MNFS_HDR_SIZE
    mov cx, MNFS_MAX_ENTRIES

.wi_dup_check:
    cmp byte [di], MNFS_DELETED
    je .wi_dup_skip
    cmp byte [di], 0x00
    je .wi_dup_skip

    push cx
    push di
    mov si, [.wi_caller_si]
    mov cx, MNFS_NAME_LEN
    repe cmpsb
    pop di
    pop cx
    je .wi_err_exists

.wi_dup_skip:
    add di, MNFS_ENTRY_SIZE
    dec cx
    jnz .wi_dup_check

    ; Find free slot
    mov di, dir_cache + MNFS_HDR_SIZE
    mov cx, MNFS_MAX_ENTRIES

.wi_slot_scan:
    cmp byte [di], 0x00
    je .wi_slot_found
    cmp byte [di], MNFS_DELETED
    je .wi_slot_found
    add di, MNFS_ENTRY_SIZE
    dec cx
    jnz .wi_slot_scan

    jmp .wi_err_dir_full

.wi_slot_found:
    mov [.wi_slot_di], di

    ; Calculate sectors = (bytes + 511) / 512
    mov eax, [.wi_size_bytes]
    add eax, 511
    shr eax, 9
    mov [.wi_sectors], ax

    ; Calculate append sector = MNFS_DIR_SECTOR + total_sectors
    movzx eax, word [dir_cache + MNFS_HDR_TOTAL]
    add eax, MNFS_DIR_SECTOR
    mov [.wi_start], eax

    ; Check capacity
    movzx ebx, word [dir_cache + MNFS_HDR_CAPACITY]
    movzx ecx, word [dir_cache + MNFS_HDR_TOTAL]
    add ecx, MNFS_DIR_SECTORS
    movzx edx, word [.wi_sectors]
    add ecx, edx
    cmp ecx, ebx
    ja .wi_err_disk_full

    ; Write data to virtual disk (direct memcpy instead of INT 0x13)
    cmp word [.wi_sectors], 0
    je .wi_update_dir

    ; Dest = VDISK_BASE + start_sector * 512
    mov eax, [.wi_start]
    shl eax, 9                      ; EAX = byte offset on "disk"
    mov di, VDISK_BASE
    add di, ax                      ; DI = dest in virtual disk

    ; Source = caller's buffer (ES:BX saved earlier, but since DS=ES=0 in test)
    mov si, [.wi_buf_off]

    ; Copy size_bytes
    mov ecx, [.wi_size_bytes]
.wi_copy_loop:
    test ecx, ecx
    jz .wi_update_dir
    mov al, [si]
    mov [di], al
    inc si
    inc di
    dec ecx
    jmp .wi_copy_loop

.wi_update_dir:
    ; Update directory entry
    mov di, [.wi_slot_di]

    ; Copy name (11 bytes)
    mov si, [.wi_caller_si]
    mov cx, MNFS_NAME_LEN
.wi_copy_name:
    mov al, [si]
    mov [di], al
    inc si
    inc di
    dec cx
    jnz .wi_copy_name

    ; Attribute
    mov al, [.wi_attr]
    mov [di], al
    inc di

    ; Start sector (4 bytes)
    mov eax, [.wi_start]
    mov [di], eax
    add di, 4

    ; Sectors (2 bytes)
    mov ax, [.wi_sectors]
    mov [di], ax
    add di, 2

    ; Size bytes (4 bytes)
    mov eax, [.wi_size_bytes]
    mov [di], eax
    add di, 4

    ; Reserved (10 bytes zero)
    xor al, al
    mov cx, 10
.wi_zero:
    mov [di], al
    inc di
    dec cx
    jnz .wi_zero

    ; Update header
    inc byte [dir_cache + MNFS_HDR_COUNT]
    inc byte [cached_count]
    mov ax, [dir_cache + MNFS_HDR_TOTAL]
    add ax, [.wi_sectors]
    mov [dir_cache + MNFS_HDR_TOTAL], ax

    ; Flush dir to vdisk
    call flush_dir_to_vdisk

    ; Success
    pop dx
    pop bx
    pop si
    pop di
    pop es
    clc
    ret

.wi_err_invalid:
    mov al, FS_ERR_EXISTS
    jmp .wi_fail
.wi_err_exists:
    mov al, FS_ERR_EXISTS
    jmp .wi_fail
.wi_err_dir_full:
    mov al, FS_ERR_DIR_FULL
    jmp .wi_fail
.wi_err_disk_full:
    mov al, FS_ERR_DISK_FULL
    jmp .wi_fail
.wi_fail:
    pop dx
    pop bx
    pop si
    pop di
    pop es
    stc
    ret

.wi_caller_si:   dw 0
.wi_buf_off:     dw 0
.wi_buf_seg:     dw 0
.wi_size_bytes:  dd 0
.wi_attr:        db 0
.wi_sectors:     dw 0
.wi_start:       dd 0
.wi_slot_di:     dw 0

; =============================================================================
; IMPLEMENTATION — fs_delete_impl
; =============================================================================
fs_delete_impl:
    push di
    push cx

    mov [.di_caller_si], si

    ; Find file
    mov di, dir_cache + MNFS_HDR_SIZE
    mov cx, MNFS_MAX_ENTRIES

.di_search:
    cmp byte [di], MNFS_DELETED
    je .di_skip
    cmp byte [di], 0x00
    je .di_skip

    push cx
    push di
    mov si, [.di_caller_si]
    mov cx, MNFS_NAME_LEN
    repe cmpsb
    pop di
    pop cx
    je .di_found

.di_skip:
    add di, MNFS_ENTRY_SIZE
    dec cx
    jnz .di_search

    ; Not found
    mov al, FS_ERR_NOT_FOUND
    pop cx
    pop di
    stc
    ret

.di_found:
    ; Check if system file
    test byte [di + MNFS_ENT_ATTR], MNFS_ATTR_SYSTEM
    jnz .di_protected

    ; Mark as deleted
    mov byte [di], MNFS_DELETED

    ; Decrement count
    dec byte [dir_cache + MNFS_HDR_COUNT]
    dec byte [cached_count]

    ; Recalculate total_sectors (high-water)
    call recalc_total_impl

    ; Flush
    call flush_dir_to_vdisk

    pop cx
    pop di
    clc
    ret

.di_protected:
    mov al, FS_ERR_PROTECTED
    pop cx
    pop di
    stc
    ret

.di_caller_si: dw 0

; =============================================================================
; IMPLEMENTATION — fs_rename_impl
; Input: DS:SI=old name, ES:DI=new name
; =============================================================================
fs_rename_impl:
    push bx
    push cx
    push di

    mov [.ri_old_si], si
    mov [.ri_new_di], di

    ; Validate new name
    cmp byte [di], 0x00
    je .ri_err_invalid
    cmp byte [di], MNFS_DELETED
    je .ri_err_invalid

    ; Check new name doesn't exist
    push di
    mov di, dir_cache + MNFS_HDR_SIZE
    mov cx, MNFS_MAX_ENTRIES

.ri_dup_check:
    cmp byte [di], MNFS_DELETED
    je .ri_dup_skip
    cmp byte [di], 0x00
    je .ri_dup_skip

    push cx
    push di
    mov si, [.ri_new_di]
    mov cx, MNFS_NAME_LEN
    repe cmpsb
    pop di
    pop cx
    je .ri_err_exists_pop

.ri_dup_skip:
    add di, MNFS_ENTRY_SIZE
    dec cx
    jnz .ri_dup_check
    pop di                          ; Restore caller's new name DI

    ; Find old name
    mov di, dir_cache + MNFS_HDR_SIZE
    mov cx, MNFS_MAX_ENTRIES

.ri_find_old:
    cmp byte [di], MNFS_DELETED
    je .ri_old_skip
    cmp byte [di], 0x00
    je .ri_old_skip

    push cx
    push di
    mov si, [.ri_old_si]
    mov cx, MNFS_NAME_LEN
    repe cmpsb
    pop di
    pop cx
    je .ri_old_found

.ri_old_skip:
    add di, MNFS_ENTRY_SIZE
    dec cx
    jnz .ri_find_old

    ; Old not found
    mov al, FS_ERR_NOT_FOUND
    pop di
    pop cx
    pop bx
    stc
    ret

.ri_old_found:
    ; Copy new name into entry at DI
    mov si, [.ri_new_di]
    mov cx, MNFS_NAME_LEN
.ri_copy:
    mov al, [si]
    mov [di], al
    inc si
    inc di
    dec cx
    jnz .ri_copy

    ; Flush
    call flush_dir_to_vdisk

    pop di
    pop cx
    pop bx
    clc
    ret

.ri_err_invalid:
    mov al, FS_ERR_EXISTS
    pop di
    pop cx
    pop bx
    stc
    ret

.ri_err_exists_pop:
    pop di                          ; Balance the push di from dup check start
    mov al, FS_ERR_EXISTS
    pop di
    pop cx
    pop bx
    stc
    ret

.ri_old_si: dw 0
.ri_new_di: dw 0

; =============================================================================
; flush_dir_to_vdisk — Copy dir_cache to VDISK at directory offset
; =============================================================================
flush_dir_to_vdisk:
    push si
    push di
    push cx

    mov si, dir_cache
    mov di, VDISK_BASE + VDISK_DIR_OFF
    mov cx, 256
    rep movsw

    pop cx
    pop di
    pop si
    ret

; =============================================================================
; recalc_total_impl — Recalculate total_sectors high-water mark
; =============================================================================
recalc_total_impl:
    push di
    push cx
    push eax
    push edx

    mov di, dir_cache + MNFS_HDR_SIZE
    mov cx, MNFS_MAX_ENTRIES
    xor bx, bx                     ; High-water

.rt_loop:
    cmp byte [di], 0x00
    je .rt_next
    cmp byte [di], MNFS_DELETED
    je .rt_next

    ; end = start + sectors
    mov eax, [di + MNFS_ENT_START]
    movzx edx, word [di + MNFS_ENT_SECTORS]
    add eax, edx
    sub eax, MNFS_DIR_SECTOR
    cmp ax, bx
    jbe .rt_next
    mov bx, ax

.rt_next:
    add di, MNFS_ENTRY_SIZE
    dec cx
    jnz .rt_loop

    mov [dir_cache + MNFS_HDR_TOTAL], bx

    pop edx
    pop eax
    pop cx
    pop di
    ret

; =============================================================================
; DATA
; =============================================================================
cached_count: db 0

dir_cache:
    times 512 db 0
