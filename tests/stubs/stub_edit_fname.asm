; stub_edit_fname.asm — Test harness for ed_fp_parse_typed_name
;
; Converts user-typed filenames (e.g., "hello.txt") to 8.3 padded format.

[bits 16]
[org 0x1000]

MNFS_NAME_LEN   equ 11

; =============================================================================
; ENTRY POINTS
; =============================================================================

; Entry 0: ed_fp_parse_typed_name
entry_parse:
    call ed_fp_parse_typed_name
    hlt

; =============================================================================
; ROUTINE (inlined from edit_dialog.inc)
; =============================================================================

ed_fp_parse_typed_name:
    mov si, _fp_input_buf
    mov di, filename
    ; Fill with spaces first (8+3)
    push di
    mov al, ' '
    mov cx, 11
    rep stosb
    pop di

    ; Copy name part (up to 8 chars, stop at dot or end)
    mov cx, 8
.ptn_name:
    lodsb
    or al, al
    jz .ptn_done
    cmp al, '.'
    je .ptn_ext
    ; Convert to uppercase
    cmp al, 'a'
    jb .ptn_store
    cmp al, 'z'
    ja .ptn_store
    sub al, 0x20
.ptn_store:
    stosb
    dec cx
    jnz .ptn_name
    ; Skip any remaining name chars before dot
.ptn_skip:
    lodsb
    or al, al
    jz .ptn_done
    cmp al, '.'
    jne .ptn_skip

.ptn_ext:
    ; Position DI at extension (offset 8)
    mov di, filename + 8
    mov cx, 3
.ptn_ext_ch:
    lodsb
    or al, al
    jz .ptn_done
    cmp al, 'a'
    jb .ptn_ext_st
    cmp al, 'z'
    ja .ptn_ext_st
    sub al, 0x20
.ptn_ext_st:
    stosb
    dec cx
    jnz .ptn_ext_ch

.ptn_done:
    mov byte [filename + 11], 0     ; NUL terminate
    mov byte [filename_len], MNFS_NAME_LEN
    ret

; =============================================================================
; DATA
; =============================================================================
_fp_input_buf:  times 16 db 0       ; User input goes here
filename:       times 12 db 0       ; 11 chars + NUL
filename_len:   db 0
