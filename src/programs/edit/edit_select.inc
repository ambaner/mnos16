; =============================================================================
; SELECTION OPERATIONS
; =============================================================================

ed_clear_selection:
    mov byte [sel_active], 0
    ret

ed_start_or_extend_sel:
    cmp byte [sel_active], 0
    jne .sos_already
    ; Start new selection at current cursor offset
    mov byte [sel_active], 1
    call ed_get_cursor_offset
    mov [sel_start], ax
    mov [sel_end], ax
.sos_already:
    ret

ed_update_sel_end:
    call ed_get_cursor_offset
    mov [sel_end], ax
    ret

ed_cmd_select_all:
    mov byte [sel_active], 1
    mov word [sel_start], 0
    call ed_get_text_length
    mov [sel_end], ax
    ; Move cursor to end
    mov ax, [total_lines]
    dec ax
    mov [cursor_row], ax
    call ed_move_end_internal
    call ed_sync_gap_to_cursor
    call ed_draw_edit_area
    call ed_set_hw_cursor
    ret

; -----------------------------------------------------------------------------
; ed_delete_selection — Delete selected text
; -----------------------------------------------------------------------------
ed_delete_selection:
    cmp byte [sel_active], 0
    je .ds_done

    ; Ensure sel_start <= sel_end
    mov ax, [sel_start]
    mov bx, [sel_end]
    cmp ax, bx
    jle .ds_ordered
    xchg ax, bx
.ds_ordered:
    ; Move gap to sel_start
    push bx
    call ed_gap_move_to             ; AX = target offset
    pop bx

    ; Delete (bx - ax) chars forward
    sub bx, ax
    mov cx, bx                      ; CX = chars to delete
.ds_del_loop:
    or cx, cx
    jz .ds_del_done
    call ed_gap_delete_fwd
    jc .ds_del_done
    dec cx
    jmp .ds_del_loop
.ds_del_done:
    mov byte [sel_active], 0
    ; Recalculate cursor position from gap offset
    call ed_recalc_cursor_from_gap
.ds_done:
    ret

; -----------------------------------------------------------------------------
; ed_recalc_cursor_from_gap — Set cursor_row/col from gap_start position
; -----------------------------------------------------------------------------
ed_recalc_cursor_from_gap:
    push bx
    push cx
    ; Count newlines from buffer start to gap_start
    mov si, GAP_BUF_START
    xor cx, cx                      ; Line count
    xor bx, bx                      ; Column count

.rcg_loop:
    cmp si, [gap_start]
    jae .rcg_done
    mov al, [si]
    inc si
    cmp al, 0x0A
    jne .rcg_not_nl
    inc cx
    xor bx, bx
    jmp .rcg_loop
.rcg_not_nl:
    inc bx
    jmp .rcg_loop

.rcg_done:
    mov [cursor_row], cx
    mov [cursor_col], bx
    ; Recount total lines
    call ed_count_lines
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; ed_count_lines — Recount total_lines from buffer content
; -----------------------------------------------------------------------------
ed_count_lines:
    push si
    push cx
    mov si, GAP_BUF_START
    mov cx, 1                       ; At least 1 line

.cl_loop:
    cmp si, GAP_BUF_END + 1
    jae .cl_done
    cmp si, [gap_start]
    jb .cl_read
    cmp si, [gap_end]
    jae .cl_read                    ; Already past gap, read normally
    ; SI is inside the gap — skip it
    mov si, [gap_end]
    cmp si, GAP_BUF_END + 1
    jae .cl_done
.cl_read:
    mov al, [si]
    inc si
    cmp al, 0x0A
    jne .cl_loop
    inc cx
    jmp .cl_loop

.cl_done:
    mov [total_lines], cx
    pop cx
    pop si
    ret
