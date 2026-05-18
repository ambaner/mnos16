
; =============================================================================
; COMMAND: mem
; Display detailed memory information using kernel syscalls.
; =============================================================================
cmd_mem:
    mov si, msg_mem_hdr
    mov ah, SYS_PRINT_STRING
    int 0x80

    ; --- Conventional memory (SYS_GET_CONV_MEM) ------------------------------
    mov si, msg_conv_mem
    mov ah, SYS_PRINT_STRING
    int 0x80

    mov ah, SYS_GET_CONV_MEM        ; Returns AX = conventional KB
    int 0x80
    mov dx, ax
    mov ah, SYS_PRINT_DEC16         ; Print DX as decimal
    int 0x80

    mov si, msg_kb
    mov ah, SYS_PRINT_STRING
    int 0x80

    ; --- Extended memory (SYS_GET_EXT_MEM) -----------------------------------
    mov si, msg_ext_mem
    mov ah, SYS_PRINT_STRING
    int 0x80

    mov ah, SYS_GET_EXT_MEM         ; Returns AX = extended KB, CF=err
    int 0x80
    jc .mem_no_ext

    mov dx, ax
    mov ah, SYS_PRINT_DEC16         ; Print DX as decimal
    int 0x80
    mov si, msg_kb
    mov ah, SYS_PRINT_STRING
    int 0x80
    jmp .mem_a20

.mem_no_ext:
    mov si, msg_na
    mov ah, SYS_PRINT_STRING
    int 0x80

.mem_a20:
    ; --- A20 gate status (from BIB via syscall) ------------------------------
    mov si, msg_a20
    mov ah, SYS_PRINT_STRING
    int 0x80

    ; Get BIB pointer via syscall (not hard-coded address)
    mov ah, SYS_GET_BIB             ; Returns ES:BX = BIB base
    int 0x80
    ; BIB+1 = a20_status byte
    cmp byte [es:bx+1], 1
    je .a20_show_on

    mov si, msg_a20_off
    mov ah, SYS_PRINT_STRING
    int 0x80
    jmp .a20_verify

.a20_show_on:
    mov si, msg_a20_on
    mov ah, SYS_PRINT_STRING
    int 0x80

.a20_verify:
    ; Live A20 verification via kernel syscall
    mov si, msg_a20_live
    mov ah, SYS_PRINT_STRING
    int 0x80

    mov ah, SYS_CHECK_A20           ; Returns AL=1 if enabled, 0 if not
    int 0x80
    test al, al
    jnz .a20_live_on

    mov si, msg_a20_off_short
    mov ah, SYS_PRINT_STRING
    int 0x80
    jmp .a20_section_done

.a20_live_on:
    mov si, msg_a20_on_short
    mov ah, SYS_PRINT_STRING
    int 0x80

.a20_section_done:

    ; --- Real-mode memory layout ---------------------------------------------
    mov si, msg_layout_hdr
    mov ah, SYS_PRINT_STRING
    int 0x80
    ; Print "    0x00800-0x027FF" — but now it's FS.SYS at runtime
    mov si, msg_layout
    mov ah, SYS_PRINT_STRING
    int 0x80

    ; --- E820 Memory Map via SYS_GET_E820 ------------------------------------
    mov si, msg_e820_hdr
    mov ah, SYS_PRINT_STRING
    int 0x80

    xor ebx, ebx                    ; EBX = 0 (start of enumeration)
    mov di, e820_buf                ; ES:DI = buffer for one E820 entry

.mem_e820_loop:
    ; Call kernel E820 syscall: EBX=continuation, ES:DI=buffer
    mov ah, SYS_GET_E820
    int 0x80

    jc .mem_e820_done               ; CF set = end of map or error

    push ebx                        ; Save continuation value

    ; Print base address (4 bytes, big-endian display)
    mov si, msg_e820_base
    mov ah, SYS_PRINT_STRING
    int 0x80
    mov al, [e820_buf+3]
    mov ah, SYS_PRINT_HEX8
    int 0x80
    mov al, [e820_buf+2]
    mov ah, SYS_PRINT_HEX8
    int 0x80
    mov al, [e820_buf+1]
    mov ah, SYS_PRINT_HEX8
    int 0x80
    mov al, [e820_buf]
    mov ah, SYS_PRINT_HEX8
    int 0x80

    ; Print length (4 bytes at offset 8)
    mov si, msg_e820_len
    mov ah, SYS_PRINT_STRING
    int 0x80
    mov al, [e820_buf+11]
    mov ah, SYS_PRINT_HEX8
    int 0x80
    mov al, [e820_buf+10]
    mov ah, SYS_PRINT_HEX8
    int 0x80
    mov al, [e820_buf+9]
    mov ah, SYS_PRINT_HEX8
    int 0x80
    mov al, [e820_buf+8]
    mov ah, SYS_PRINT_HEX8
    int 0x80

    ; Print type number
    mov si, msg_e820_type
    mov ah, SYS_PRINT_STRING
    int 0x80
    mov al, [e820_buf+16]
    add al, '0'                     ; Convert type number to ASCII digit
    mov ah, SYS_PRINT_CHAR
    int 0x80

    ; Print type name string
    mov al, [e820_buf+16]
    mov si, msg_type_usable
    cmp al, 1
    je .mem_print_type
    mov si, msg_type_reserved
    cmp al, 2
    je .mem_print_type
    mov si, msg_type_acpi
    cmp al, 3
    je .mem_print_type
    mov si, msg_type_nvs
    cmp al, 4
    je .mem_print_type
    mov si, msg_type_bad
    cmp al, 5
    je .mem_print_type
    mov si, msg_type_unknown
.mem_print_type:
    mov ah, SYS_PRINT_STRING
    int 0x80

    pop ebx                         ; Restore continuation value
    test ebx, ebx                   ; EBX=0 means end of map
    jnz .mem_e820_loop

.mem_e820_done:
    ; --- Pause before heap section (output may exceed one screen) -------------
    mov si, msg_anykey
    mov ah, SYS_PRINT_STRING
    int 0x80
    mov ah, SYS_READ_KEY            ; Wait for any keypress
    int 0x80
    mov si, msg_crlf
    mov ah, SYS_PRINT_STRING
    int 0x80

    ; --- Heap Statistics (INT 0x82 MEM_INFO + MEM_AVAIL) ----------------------
    mov si, msg_heap_hdr
    mov ah, SYS_PRINT_STRING
    int 0x80

    ; MEM_QUERY: AX=segment, BX=start, CX=size — detect HMA vs conventional
    mov ah, MEM_QUERY
    int 0x82

    ; Show heap type based on segment
    push cx                         ; Save heap size for later
    push bx                         ; Save heap start for walk
    push ax                         ; Save segment for walk

    mov si, msg_heap_type
    mov ah, SYS_PRINT_STRING
    int 0x80

    ; Check if segment is HMA_SEG (0xFFFF)
    pop ax                          ; Peek segment (re-push below)
    push ax
    cmp ax, HMA_SEG
    jne .mem_heap_conv
    mov si, msg_heap_hma
    jmp .mem_heap_type_print
.mem_heap_conv:
    mov si, msg_heap_conventional
.mem_heap_type_print:
    mov ah, SYS_PRINT_STRING
    int 0x80

    ; MEM_INFO: AX=total, BX=used, CX=free, DX=block count
    mov ah, MEM_INFO
    int 0x82

    ; Save results
    push dx                         ; block count
    push cx                         ; free bytes
    push bx                         ; used bytes
    push ax                         ; total bytes

    ; Print total heap size
    mov si, msg_heap_total
    mov ah, SYS_PRINT_STRING
    int 0x80
    pop dx                          ; AX was total → now in DX for print
    mov ah, SYS_PRINT_DEC16
    int 0x80
    mov si, msg_bytes
    mov ah, SYS_PRINT_STRING
    int 0x80

    ; Print used bytes
    mov si, msg_heap_used
    mov ah, SYS_PRINT_STRING
    int 0x80
    pop dx                          ; BX was used → now in DX for print
    mov ah, SYS_PRINT_DEC16
    int 0x80
    mov si, msg_bytes
    mov ah, SYS_PRINT_STRING
    int 0x80

    ; Print free bytes
    mov si, msg_heap_free
    mov ah, SYS_PRINT_STRING
    int 0x80
    pop dx                          ; CX was free → now in DX for print
    mov ah, SYS_PRINT_DEC16
    int 0x80
    mov si, msg_bytes
    mov ah, SYS_PRINT_STRING
    int 0x80

    ; Print block count
    mov si, msg_heap_blocks
    mov ah, SYS_PRINT_STRING
    int 0x80
    pop dx                          ; DX was block count
    mov ah, SYS_PRINT_DEC16
    int 0x80
    mov si, msg_crlf
    mov ah, SYS_PRINT_STRING
    int 0x80

    ; MEM_AVAIL: AX=largest free, DX=total free (usable bytes)
    mov ah, MEM_AVAIL
    int 0x82

    mov si, msg_heap_largest
    mov ah, SYS_PRINT_STRING
    int 0x80
    ; AX has largest free — move to DX for printing
    mov dx, ax
    mov ah, SYS_PRINT_DEC16
    int 0x80
    mov si, msg_bytes
    mov ah, SYS_PRINT_STRING
    int 0x80

    ; --- Heap Block Detail (direct walk of MCB chain via ES:) -----------------
    mov si, msg_heap_detail_hdr
    mov ah, SYS_PRINT_STRING
    int 0x80

    ; Restore heap segment and start from MEM_QUERY results
    pop ax                          ; segment
    pop di                          ; heap start offset
    pop cx                          ; heap size (used as end = start + size)
    add cx, di                      ; CX = heap end offset
    mov [.mem_heap_end], cx         ; Store end for comparison
    mov es, ax                      ; ES = heap segment

.mem_heap_walk:
    cmp di, [.mem_heap_end]
    jae .mem_heap_done

    ; Validate magic (via ES:)
    cmp byte [es:di + MCB_MAGIC_OFF], MCB_MAGIC
    jne .mem_heap_done               ; Corrupted — stop

    ; Print block address
    mov si, msg_indent
    mov ah, SYS_PRINT_STRING
    int 0x80
    mov dx, di
    mov ah, SYS_PRINT_HEX16
    int 0x80

    ; Print size
    mov si, msg_heap_sz_eq
    mov ah, SYS_PRINT_STRING
    int 0x80
    mov dx, [es:di + MCB_SIZE_OFF]
    mov ah, SYS_PRINT_DEC16
    int 0x80

    ; Print status (free/used)
    test byte [es:di + MCB_FLAGS_OFF], MCB_FLAG_USED
    jz .mem_block_free

    ; Allocated — print owner name
    mov si, msg_heap_alloc
    mov ah, SYS_PRINT_STRING
    int 0x80

    ; Extract owner ID: (flags >> 1) & 0x07
    mov al, [es:di + MCB_FLAGS_OFF]
    shr al, MCB_OWNER_SHIFT
    and al, 0x07

    ; Look up owner name from table
    movzx bx, al
    shl bx, 1                       ; Word table index
    mov si, [mem_owner_table + bx]
    mov ah, SYS_PRINT_STRING
    int 0x80
    jmp .mem_block_next

.mem_block_free:
    mov si, msg_heap_free_tag
    mov ah, SYS_PRINT_STRING
    int 0x80

.mem_block_next:
    ; Advance to next block (size from ES:)
    add di, [es:di + MCB_SIZE_OFF]
    jmp .mem_heap_walk

.mem_heap_done:
    ; Restore ES to 0 (normal DS-relative state)
    xor ax, ax
    mov es, ax

    mov si, msg_crlf
    mov ah, SYS_PRINT_STRING
    int 0x80
    jmp shell_prompt

; Local variable for heap end offset
.mem_heap_end: dw 0

