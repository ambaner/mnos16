; stub_edit_gap.asm — Test harness for editor gap buffer operations
;
; Provides multiple entry points for testing individual gap buffer routines.
; Each entry point calls one routine then halts.
;
; Memory layout for testing:
;   0x1000 = code (this stub)
;   0x4000 = gap buffer (small test buffer instead of full 0xAC00-0xF7FF)
;   0x5000 = data area (gap_start, gap_end, modified, total_lines)

[bits 16]
[org 0x1000]

; --- Constants matching edit.asm ---
GAP_BUF_START   equ 0x4000
GAP_BUF_END     equ 0x40FF          ; 256-byte test buffer (more manageable)
GAP_BUF_CAPA    equ GAP_BUF_END - GAP_BUF_START + 1

; --- Data area at fixed addresses for easy test inspection ---
; Placed at 0x5000 so tests can read/write these directly
section .text

; =============================================================================
; ENTRY POINTS — the test picks which one to call by setting IP
; =============================================================================

; Entry 0: ed_gap_insert (offset 0x0000 from org)
entry_insert:
    call ed_gap_insert
    hlt

; Entry 1: ed_gap_delete_back (offset = entry_delete_back - $$)
entry_delete_back:
    call ed_gap_delete_back
    hlt

; Entry 2: ed_gap_delete_fwd
entry_delete_fwd:
    call ed_gap_delete_fwd
    hlt

; Entry 3: ed_gap_move_to
entry_move_to:
    call ed_gap_move_to
    hlt

; Entry 4: ed_get_text_length
entry_text_length:
    call ed_get_text_length
    hlt

; Entry 5: ed_get_cursor_offset
entry_cursor_offset:
    call ed_get_cursor_offset
    hlt

; Entry 6: ed_gap_char_at_si
entry_char_at_si:
    call ed_gap_char_at_si
    hlt

; Entry 7: ed_get_line_offset
entry_line_offset:
    call ed_get_line_offset
    hlt

; =============================================================================
; GAP BUFFER ROUTINES (inlined from edit_gap.inc with adjusted constants)
; =============================================================================

ed_gap_insert:
    push bx
    mov bx, [gap_start]
    cmp bx, [gap_end]
    je .gap_full
    mov [bx], al
    inc word [gap_start]
    mov byte [modified], 1
    cmp al, 0x0A
    jne .gi_done
    inc word [total_lines]
.gi_done:
    pop bx
    ret
.gap_full:
    pop bx
    ret

ed_gap_delete_back:
    push bx
    mov bx, [gap_start]
    cmp bx, GAP_BUF_START
    je .gdb_fail
    dec bx
    mov al, [bx]
    mov [gap_start], bx
    mov byte [modified], 1
    cmp al, 0x0A
    jne .gdb_ok
    dec word [total_lines]
.gdb_ok:
    clc
    pop bx
    ret
.gdb_fail:
    stc
    pop bx
    ret

ed_gap_delete_fwd:
    push bx
    mov bx, [gap_end]
    cmp bx, GAP_BUF_END + 1
    je .gdf_fail
    mov al, [bx]
    inc bx
    mov [gap_end], bx
    mov byte [modified], 1
    cmp al, 0x0A
    jne .gdf_ok
    dec word [total_lines]
.gdf_ok:
    clc
    pop bx
    ret
.gdf_fail:
    stc
    pop bx
    ret

ed_gap_move_to:
    push es
    push ax

    mov bx, [gap_start]
    sub bx, GAP_BUF_START
    cmp ax, bx
    je .gmt_done
    ja .gmt_move_right

    ; Move left
    sub bx, ax
    mov cx, bx
    mov si, [gap_start]
    dec si
    mov di, [gap_end]
    dec di
    std
.gmt_left_loop:
    lodsb
    mov [di], al
    dec di
    loop .gmt_left_loop
    cld
    sub word [gap_start], bx
    sub word [gap_end], bx
    jmp .gmt_done

.gmt_move_right:
    sub ax, bx
    mov cx, ax
    push cx                         ; Save count (loop clobbers AL)
    mov si, [gap_end]
    mov di, [gap_start]
.gmt_right_loop:
    mov al, [si]
    mov [di], al
    inc si
    inc di
    loop .gmt_right_loop
    pop ax                          ; Restore count
    add word [gap_start], ax
    add word [gap_end], ax

.gmt_done:
    pop ax
    pop es
    ret

ed_get_text_length:
    mov ax, [gap_start]
    sub ax, GAP_BUF_START
    push bx
    mov bx, GAP_BUF_END + 1
    sub bx, [gap_end]
    add ax, bx
    pop bx
    ret

ed_get_cursor_offset:
    mov ax, [gap_start]
    sub ax, GAP_BUF_START
    ret

ed_gap_char_at_si:
    cmp si, GAP_BUF_END + 1
    jae .gcas_end
    cmp si, [gap_start]
    jb .gcas_read
    cmp si, [gap_end]
    jae .gcas_read
    mov si, [gap_end]
    cmp si, GAP_BUF_END + 1
    jae .gcas_end
.gcas_read:
    mov al, [si]
    inc si
    cmp si, [gap_start]
    jne .gcas_ret
    cmp si, [gap_end]
    jae .gcas_ret
    mov si, [gap_end]
.gcas_ret:
    ret
.gcas_end:
    xor al, al
    ret

ed_get_line_offset:
    push bx
    mov si, GAP_BUF_START
    mov cx, ax
    or cx, cx
    jz .glo_done
.glo_scan:
    call ed_gap_char_at_si
    or al, al
    jz .glo_done
    cmp al, 0x0A
    jne .glo_scan
    loop .glo_scan
.glo_done:
    pop bx
    ret

; =============================================================================
; DATA — at known locations for test access
; =============================================================================
gap_start:   dw GAP_BUF_START
gap_end:     dw GAP_BUF_END + 1
modified:    db 0
total_lines: dw 1
