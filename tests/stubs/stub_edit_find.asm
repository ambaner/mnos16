; stub_edit_find.asm — Test harness for ed_search_text, ed_get_char_at_offset, ed_atoi
;
; Uses a small gap buffer (256 bytes at 0x4000) just like stub_edit_gap.

[bits 16]
[org 0x1000]

; --- Constants ---
GAP_BUF_START   equ 0x4000
GAP_BUF_END     equ 0x40FF
SEARCH_BUF_ADDR equ 0x5000          ; Search string placed here by test

; =============================================================================
; ENTRY POINTS
; =============================================================================

; Entry 0: ed_search_text (AX = start offset)
entry_search_text:
    call ed_search_text
    hlt

; Entry 1: ed_get_char_at_offset (AX = offset)
entry_char_at_offset:
    call ed_get_char_at_offset
    hlt

; Entry 2: ed_atoi (SI = string)
entry_atoi:
    call ed_atoi
    hlt

; =============================================================================
; ROUTINES
; =============================================================================

; --- ed_get_text_length (needed by search_text) ---
ed_get_text_length:
    mov ax, [gap_start]
    sub ax, GAP_BUF_START
    push bx
    mov bx, GAP_BUF_END + 1
    sub bx, [gap_end]
    add ax, bx
    pop bx
    ret

; --- ed_get_char_at_offset ---
ed_get_char_at_offset:
    push bx
    mov bx, ax
    add bx, GAP_BUF_START
    cmp bx, [gap_start]
    jb .gcao_read
    push cx
    mov cx, [gap_end]
    sub cx, [gap_start]
    add bx, cx
    pop cx
.gcao_read:
    mov al, [bx]
    pop bx
    ret

; --- ed_search_text ---
ed_search_text:
    push bx
    push cx
    push dx
    push di

    mov dx, ax                      ; DX = current search position
    call ed_get_text_length
    mov bx, ax                      ; BX = text length

.st_outer:
    cmp dx, bx
    jge .st_not_found

    push dx
    mov cx, dx
    mov di, SEARCH_BUF_ADDR
    movzx ax, byte [search_len]

.st_compare:
    or al, al
    jz .st_found
    push ax
    mov ax, cx
    call ed_get_char_at_offset
    cmp al, [di]
    pop ax
    jne .st_mismatch
    inc cx
    inc di
    dec al
    jmp .st_compare

.st_mismatch:
    pop dx
    inc dx
    jmp .st_outer

.st_found:
    pop dx
    mov ax, dx
    clc
    pop di
    pop dx
    pop cx
    pop bx
    ret

.st_not_found:
    stc
    pop di
    pop dx
    pop cx
    pop bx
    ret

; --- ed_atoi ---
ed_atoi:
    xor ax, ax
    mov bx, 10
.atoi_loop:
    movzx cx, byte [si]
    or cl, cl
    jz .atoi_done
    sub cl, '0'
    cmp cl, 9
    ja .atoi_done
    mul bx
    add ax, cx
    inc si
    jmp .atoi_loop
.atoi_done:
    ret

; =============================================================================
; DATA
; =============================================================================
gap_start:   dw GAP_BUF_START
gap_end:     dw GAP_BUF_END + 1
search_len:  db 0
