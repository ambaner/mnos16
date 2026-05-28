; =============================================================================
; CLIPBOARD (CUT / COPY / PASTE)
; =============================================================================

ed_cmd_cut:
    cmp byte [sel_active], 0
    je .cut_done
    call ed_copy_selection_to_clipboard
    call ed_delete_selection
    call ed_draw_edit_area
    call ed_set_hw_cursor
.cut_done:
    ret

ed_cmd_copy:
    cmp byte [sel_active], 0
    je .copy_done
    call ed_copy_selection_to_clipboard
.copy_done:
    ret

ed_cmd_paste:
    cmp word [clipboard_len], 0
    je .paste_done
    ; Delete selection if active
    cmp byte [sel_active], 0
    je .paste_no_sel
    call ed_delete_selection
.paste_no_sel:
    ; Insert clipboard contents
    mov si, CLIPBOARD_ADDR
    mov cx, [clipboard_len]
.paste_loop:
    lodsb
    call ed_gap_insert
    loop .paste_loop
    ; Update cursor position
    call ed_recalc_cursor_from_gap
    call ed_scroll_into_view
    call ed_draw_edit_area
    call ed_set_hw_cursor
.paste_done:
    ret

; -----------------------------------------------------------------------------
; ed_copy_selection_to_clipboard — Copy selected text to clipboard buffer
; -----------------------------------------------------------------------------
ed_copy_selection_to_clipboard:
    push di
    push cx

    ; Get ordered selection bounds
    mov ax, [sel_start]
    mov bx, [sel_end]
    cmp ax, bx
    jle .cstc_ordered
    xchg ax, bx
.cstc_ordered:
    ; Calculate length (capped at CLIPBOARD_SIZE)
    sub bx, ax                      ; BX = length
    cmp bx, CLIPBOARD_SIZE
    jle .cstc_len_ok
    mov bx, CLIPBOARD_SIZE
.cstc_len_ok:
    mov [clipboard_len], bx

    ; Read from gap buffer at logical offset AX, length BX
    ; First, save gap position and navigate
    push word [gap_start]
    push word [gap_end]
    call ed_gap_move_to             ; Move gap to sel_start

    ; Now text after gap is the selection — copy from gap_end
    mov si, [gap_end]
    mov di, CLIPBOARD_ADDR
    mov cx, bx
    rep movsb

    ; Restore gap position
    pop word [gap_end]
    pop word [gap_start]

    pop cx
    pop di
    ret

