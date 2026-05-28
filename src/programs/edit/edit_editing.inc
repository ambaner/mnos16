; =============================================================================
; EDITING COMMANDS
; =============================================================================

ed_cmd_insert_char:
    ; AL = character to insert
    ; If selection is active, delete it first
    cmp byte [sel_active], 0
    je .ic_no_sel
    push ax
    call ed_delete_selection
    pop ax
.ic_no_sel:
    ; Check insert/overwrite mode
    cmp byte [insert_mode], 0
    jne .ic_insert
    ; Overwrite mode: delete forward char first (if not at end of line)
    push ax
    call ed_gap_delete_fwd
    pop ax
.ic_insert:
    call ed_gap_insert
    inc word [cursor_col]
    call ed_scroll_into_view
    call ed_draw_edit_area
    call ed_set_hw_cursor
    ret

ed_cmd_enter:
    ; Delete selection if active
    cmp byte [sel_active], 0
    je .enter_no_sel
    call ed_delete_selection
.enter_no_sel:
    mov al, 0x0A
    call ed_gap_insert
    inc word [cursor_row]
    mov word [cursor_col], 0
    call ed_scroll_into_view
    call ed_draw_edit_area
    call ed_set_hw_cursor
    ret

ed_cmd_tab:
    mov al, 0x09
    call ed_gap_insert
    ; Advance column to next tab stop
    mov ax, [cursor_col]
    add ax, TAB_WIDTH
    and ax, ~(TAB_WIDTH - 1)        ; Round down to tab stop... actually up
    mov [cursor_col], ax
    call ed_draw_edit_area
    call ed_set_hw_cursor
    ret

ed_cmd_backspace:
    cmp byte [sel_active], 0
    je .bs_no_sel
    call ed_delete_selection
    call ed_draw_edit_area
    call ed_set_hw_cursor
    ret
.bs_no_sel:
    call ed_gap_delete_back
    jc .bs_done                     ; At start, nothing to do
    ; If deleted char was newline, go to end of previous line
    cmp al, 0x0A
    je .bs_prev_line
    dec word [cursor_col]
    jmp .bs_redraw
.bs_prev_line:
    dec word [cursor_row]
    call ed_move_end_internal
.bs_redraw:
    call ed_scroll_into_view
    call ed_draw_edit_area
    call ed_set_hw_cursor
.bs_done:
    ret

ed_cmd_delete:
    cmp byte [sel_active], 0
    je .del_no_sel
    call ed_delete_selection
    call ed_draw_edit_area
    call ed_set_hw_cursor
    ret
.del_no_sel:
    call ed_gap_delete_fwd
    jc .del_done
    call ed_draw_edit_area
    call ed_set_hw_cursor
.del_done:
    ret

ed_cmd_toggle_insert:
    xor byte [insert_mode], 1
    ret

