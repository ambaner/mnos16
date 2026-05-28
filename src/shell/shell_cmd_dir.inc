; =============================================================================
; COMMAND: dir
; List files in the MNFS filesystem via INT 0x81 (FS.SYS).
;
; Calls FS_LIST_FILES to get the cached directory, then parses and displays
; each entry in a formatted table.
; =============================================================================
cmd_dir:
    ; Print header
    mov si, msg_dir_hdr
    mov ah, SYS_PRINT_STRING
    int 0x80

    ; Call FS_LIST_FILES — copies directory into our buffer
    mov ah, FS_LIST_FILES
    push ds
    pop es                          ; ES = DS (our segment)
    mov bx, dir_buffer              ; ES:BX → 512-byte buffer
    int 0x81                        ; CL = file count (may include deleted)

    ; Print column headers
    mov si, msg_dir_cols
    mov ah, SYS_PRINT_STRING
    int 0x80

    ; SI → first directory entry (offset 32 in buffer = past header)
    mov si, dir_buffer + MNFS_HDR_SIZE
    mov word [dir_file_count], 0    ; Will count non-deleted entries
    mov cx, MNFS_MAX_ENTRIES        ; Scan all 15 slots
    xor dx, dx                      ; DX = total bytes accumulator (low word)

.dir_loop:
    ; Skip empty/deleted entries (tombstones)
    cmp byte [si], 0x00
    je .dir_skip_entry
    cmp byte [si], MNFS_DELETED
    je .dir_skip_entry

    inc word [dir_file_count]
    push cx
    push dx

    ; Print "  " indent
    push si
    mov si, msg_dir_indent
    mov ah, SYS_PRINT_STRING
    int 0x80
    pop si

    ; Print filename (8 bytes) — SI → entry name field
    push si
    mov cx, 8
.dir_print_name:
    lodsb
    mov ah, SYS_PRINT_CHAR
    int 0x80
    dec cx
    jnz .dir_print_name

    ; Print dot between name and extension
    mov al, '.'
    mov ah, SYS_PRINT_CHAR
    int 0x80

    ; Print extension (3 bytes)
    mov cx, 3
.dir_print_ext:
    lodsb
    mov ah, SYS_PRINT_CHAR
    int 0x80
    dec cx
    jnz .dir_print_ext
    pop si                          ; Restore SI to entry start

    ; Print spaces + attribute type
    push si
    mov si, msg_dir_space
    mov ah, SYS_PRINT_STRING
    int 0x80
    pop si

    ; Check attributes at entry + 11
    mov al, [si + MNFS_ENT_ATTR]
    ; Check combined SYSTEM+EXEC first (0x03)
    mov ah, al
    and ah, MNFS_ATTR_SYSTEM | MNFS_ATTR_EXEC
    cmp ah, MNFS_ATTR_SYSTEM | MNFS_ATTR_EXEC
    je .dir_sysx
    test al, MNFS_ATTR_SYSTEM
    jnz .dir_sys
    test al, MNFS_ATTR_EXEC
    jnz .dir_exe
    ; Data file
    push si
    mov si, msg_dir_dat
    mov ah, SYS_PRINT_STRING
    int 0x80
    pop si
    jmp .dir_size

.dir_sysx:
    push si
    mov si, msg_dir_sysx
    mov ah, SYS_PRINT_STRING
    int 0x80
    pop si
    jmp .dir_size

.dir_sys:
    push si
    mov si, msg_dir_sys
    mov ah, SYS_PRINT_STRING
    int 0x80
    pop si
    jmp .dir_size

.dir_exe:
    push si
    mov si, msg_dir_exe
    mov ah, SYS_PRINT_STRING
    int 0x80
    pop si

.dir_size:
    ; Print size in sectors, right-justified in 3-char field
    mov dx, [si + MNFS_ENT_SECTORS]
    mov cl, 3
    call rjust_dec16

    push si
    mov si, msg_dir_sec_suffix
    mov ah, SYS_PRINT_STRING
    int 0x80
    pop si

    ; Print size in bytes, right-justified in 6-char field
    mov dx, [si + MNFS_ENT_BYTES]
    push dx                         ; Save for total accumulation
    mov cl, 6
    call rjust_dec16

    push si
    mov si, msg_dir_bytes_suffix
    mov ah, SYS_PRINT_STRING
    int 0x80
    pop si

    ; Accumulate total bytes
    pop ax                          ; AX = this file's size (low word)
    pop dx                          ; DX = running total
    add dx, ax
    push dx

    ; Advance to next entry
    add si, MNFS_ENTRY_SIZE
    pop dx
    pop cx
    dec cx
    jnz .dir_loop
    jmp .dir_summary

.dir_skip_entry:
    add si, MNFS_ENTRY_SIZE
    dec cx
    jnz .dir_loop

    ; Check if any files were found
.dir_summary:
    cmp word [dir_file_count], 0
    je .dir_empty

    ; Print summary line
    push dx                         ; Save total bytes
    mov si, msg_dir_sep
    mov ah, SYS_PRINT_STRING
    int 0x80

    mov dx, [dir_file_count]
    mov ah, SYS_PRINT_DEC16
    int 0x80

    mov si, msg_dir_summary
    mov ah, SYS_PRINT_STRING
    int 0x80

    pop dx                          ; DX = total bytes
    mov ah, SYS_PRINT_DEC16
    int 0x80

    mov si, msg_dir_total_bytes
    mov ah, SYS_PRINT_STRING
    int 0x80

    ; --- Disk space statistics via FS_GET_INFO --------------------------------
    mov ah, FS_GET_INFO             ; Returns: DX=used sectors, BX=capacity
    int 0x81

    ; Save returned values
    mov [dir_used_sec], dx
    mov [dir_cap_sec], bx

    ; "  Used:  X KB / Y KB"
    mov si, msg_dir_used
    mov ah, SYS_PRINT_STRING
    int 0x80

    mov dx, [dir_used_sec]
    shr dx, 1                       ; Sectors → KB (÷2)
    mov cl, 6
    call rjust_dec16

    mov si, msg_dir_of
    mov ah, SYS_PRINT_STRING
    int 0x80

    mov dx, [dir_cap_sec]
    shr dx, 1                       ; Sectors → KB
    mov ah, SYS_PRINT_DEC16
    int 0x80

    mov si, msg_dir_kb
    mov ah, SYS_PRINT_STRING
    int 0x80

    ; "  Free:  X KB"
    mov si, msg_dir_free
    mov ah, SYS_PRINT_STRING
    int 0x80

    mov dx, [dir_cap_sec]
    sub dx, [dir_used_sec]
    shr dx, 1                       ; Sectors → KB
    mov cl, 6
    call rjust_dec16

    mov si, msg_dir_kb
    mov ah, SYS_PRINT_STRING
    int 0x80

    jmp shell_prompt

.dir_empty:
    mov si, msg_dir_empty
    mov ah, SYS_PRINT_STRING
    int 0x80
    jmp shell_prompt
