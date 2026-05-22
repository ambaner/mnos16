; =============================================================================
; CURSOR MOVEMENT
; =============================================================================

ed_move_up:
    cmp word [cursor_row], 0
    je .mu_done
    ; Move gap to start of current line, then one char back (to prev line)
    ; Then find the equivalent column on that line
    dec word [cursor_row]
    call ed_sync_gap_to_cursor
.mu_done:
    ret

ed_move_down:
    mov ax, [cursor_row]
    inc ax
    cmp ax, [total_lines]
    jge .md_done
    mov [cursor_row], ax
    call ed_sync_gap_to_cursor
.md_done:
    ret

ed_move_left:
    cmp word [cursor_col], 0
    je .ml_prev_line
    dec word [cursor_col]
    call ed_sync_gap_to_cursor
    ret
.ml_prev_line:
    cmp word [cursor_row], 0
    je .ml_done
    dec word [cursor_row]
    call ed_move_end_internal       ; Go to end of previous line
    call ed_sync_gap_to_cursor
.ml_done:
    ret

ed_move_right:
    ; Check if we can move right on current line
    call ed_get_line_length         ; AX = length of current line
    cmp [cursor_col], ax
    jge .mr_next_line
    inc word [cursor_col]
    call ed_sync_gap_to_cursor
    ret
.mr_next_line:
    mov ax, [cursor_row]
    inc ax
    cmp ax, [total_lines]
    jge .mr_done
    mov [cursor_row], ax
    mov word [cursor_col], 0
    call ed_sync_gap_to_cursor
.mr_done:
    ret

ed_move_home:
    mov word [cursor_col], 0
    call ed_sync_gap_to_cursor
    ret

ed_move_end:
    call ed_move_end_internal
    call ed_sync_gap_to_cursor
    ret

ed_move_end_internal:
    call ed_get_line_length
    mov [cursor_col], ax
    ret

ed_move_pgup:
    mov ax, [cursor_row]
    sub ax, EDIT_LINES
    jns .pgup_ok
    xor ax, ax
.pgup_ok:
    mov [cursor_row], ax
    call ed_sync_gap_to_cursor
    ret

ed_move_pgdn:
    mov ax, [cursor_row]
    add ax, EDIT_LINES
    mov bx, [total_lines]
    dec bx                          ; Max valid row
    cmp ax, bx
    jle .pgdn_ok
    mov ax, bx
.pgdn_ok:
    cmp ax, 0
    jge .pgdn_set
    xor ax, ax
.pgdn_set:
    mov [cursor_row], ax
    call ed_sync_gap_to_cursor
    ret

ed_cmd_goto_top:
    mov word [cursor_row], 0
    mov word [cursor_col], 0
    call ed_sync_gap_to_cursor
    call ed_scroll_into_view
    call ed_draw_edit_area
    call ed_set_hw_cursor
    ret

ed_cmd_goto_bottom:
    mov ax, [total_lines]
    dec ax
    mov [cursor_row], ax
    call ed_move_end_internal
    call ed_sync_gap_to_cursor
    call ed_scroll_into_view
    call ed_draw_edit_area
    call ed_set_hw_cursor
    ret

; -----------------------------------------------------------------------------
; ed_sync_gap_to_cursor — Move gap buffer to match cursor_row:cursor_col
; Trashes: AX, BX, CX, SI, DI
; -----------------------------------------------------------------------------
ed_sync_gap_to_cursor:
    ; Calculate the logical offset for cursor_row:cursor_col
    mov ax, [cursor_row]
    call ed_get_line_offset         ; SI = start of that line in buffer

    ; Now count cursor_col characters forward (or stop at newline/end)
    mov cx, [cursor_col]
    or cx, cx
    jz .sgc_have_offset

.sgc_col_loop:
    call ed_gap_char_at_si
    or al, al
    jz .sgc_have_offset             ; End of text
    cmp al, 0x0A
    je .sgc_have_offset             ; End of line
    dec cx
    jnz .sgc_col_loop

.sgc_have_offset:
    ; SI now points to where the gap should be
    ; Calculate logical offset: distance from GAP_BUF_START, minus gap if we passed it
    ; Simplification: calculate offset as if no gap
    mov ax, si
    cmp ax, [gap_end]
    jb .sgc_no_adjust
    ; We're past the gap, subtract gap size
    push bx
    mov bx, [gap_end]
    sub bx, [gap_start]
    sub ax, bx
    pop bx
.sgc_no_adjust:
    sub ax, GAP_BUF_START
    call ed_gap_move_to
    ret

; -----------------------------------------------------------------------------
; ed_get_line_length — Get length of current cursor line
; Returns: AX = character count (excluding newline)
; Trashes: SI
; -----------------------------------------------------------------------------
ed_get_line_length:
    push bx
    push cx
    mov ax, [cursor_row]
    call ed_get_line_offset         ; SI = start of line
    xor cx, cx                      ; Count
.gll_loop:
    call ed_gap_char_at_si
    or al, al
    jz .gll_done
    cmp al, 0x0A
    je .gll_done
    inc cx
    jmp .gll_loop
.gll_done:
    mov ax, cx
    pop cx
    pop bx
    ret

; =============================================================================
; SCROLLING
; =============================================================================

ed_scroll_into_view:
    ; Ensure cursor_row is visible in the viewport
    mov ax, [cursor_row]
    cmp ax, [view_top]
    jge .siv_check_bottom
    ; Cursor is above viewport — scroll up
    mov [view_top], ax
    ret
.siv_check_bottom:
    mov bx, [view_top]
    add bx, EDIT_LINES
    dec bx                          ; Last visible row
    cmp ax, bx
    jle .siv_done
    ; Cursor is below viewport — scroll down
    sub ax, EDIT_LINES
    inc ax
    mov [view_top], ax
.siv_done:
    ret

; =============================================================================
; HARDWARE CURSOR POSITIONING
; =============================================================================
ed_set_hw_cursor:
    ; Position hardware cursor at cursor_row - view_top + EDIT_TOP_ROW, cursor_col
    mov dh, byte [cursor_row]
    sub dh, byte [view_top]
    add dh, EDIT_TOP_ROW            ; Screen row
    mov dl, byte [cursor_col]       ; Screen column
    mov ah, SYS_SET_CURSOR
    int 0x80
    ret

