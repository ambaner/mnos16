; =============================================================================
; ED_HANDLE_KEY — Dispatch a keystroke
;
; Input:  AH = scancode, AL = ASCII char
; =============================================================================
ed_handle_key:
    ; Check for ASCII characters first
    or al, al
    jz .extended_key

    ; --- ASCII key dispatch ---

    ; Ctrl key combos
    cmp al, CTRL_S
    je ed_cmd_save
    cmp al, CTRL_O
    je ed_cmd_open
    cmp al, CTRL_N
    je ed_cmd_new
    cmp al, CTRL_X
    je ed_cmd_cut
    cmp al, CTRL_C
    je ed_cmd_copy
    cmp al, CTRL_V
    je ed_cmd_paste
    cmp al, CTRL_F
    je ed_cmd_find
    cmp al, CTRL_G
    je ed_cmd_goto_line
    cmp al, CTRL_A
    je ed_cmd_select_all

    ; Escape — close menu or cancel selection
    cmp al, 0x1B
    je ed_cmd_escape

    ; Backspace (ASCII 0x08 but scancode 0x0E — distinguish from Ctrl+H)
    cmp al, 0x08
    jne .not_bs
    cmp ah, 0x0E                    ; Backspace scancode
    je ed_cmd_backspace
    jmp ed_cmd_replace              ; Otherwise it's Ctrl+H (scancode 0x23)
.not_bs:

    ; Enter
    cmp al, 0x0D
    je ed_cmd_enter

    ; Tab
    cmp al, 0x09
    je ed_cmd_tab

    ; Regular printable character (>= 0x20)
    cmp al, 0x20
    jb .handle_done
    call ed_cmd_insert_char
    jmp .handle_done

.extended_key:
    ; --- Extended key dispatch (AL=0, scancode in AH) ---

    ; Check for shift state (selection)
    push ax
    mov bx, BDA_SHIFT_FLAGS
    mov ah, SYS_GET_BDA_BYTE
    int 0x80                        ; AL = shift flags
    mov dl, al                      ; DL = shift flags
    pop ax

    ; Arrow keys
    cmp ah, KEY_UP
    je .key_up
    cmp ah, KEY_DOWN
    je .key_down
    cmp ah, KEY_LEFT
    je .key_left
    cmp ah, KEY_RIGHT
    je .key_right
    cmp ah, KEY_HOME
    je .key_home
    cmp ah, KEY_END
    je .key_end
    cmp ah, KEY_PGUP
    je .key_pgup
    cmp ah, KEY_PGDN
    je .key_pgdn
    cmp ah, KEY_INSERT
    je ed_cmd_toggle_insert
    cmp ah, KEY_DELETE
    je ed_cmd_delete

    ; Ctrl+Home / Ctrl+End
    cmp ah, CTRL_HOME
    je ed_cmd_goto_top
    cmp ah, CTRL_END
    je ed_cmd_goto_bottom

    ; Alt keys (menu access)
    cmp ah, ALT_F
    je ed_cmd_menu_file
    cmp ah, ALT_E
    je ed_cmd_menu_edit
    cmp ah, ALT_S
    je ed_cmd_menu_search
    cmp ah, ALT_X
    je ed_cmd_exit

    ; F1 = Help
    cmp ah, KEY_F1
    je ed_cmd_help

    ; F3 = Find next
    cmp ah, KEY_F3
    je ed_cmd_find_next

    ; F4 = Replace all
    cmp ah, KEY_F4
    je ed_cmd_replace_all

    jmp .handle_done

.key_up:
    test dl, 0x03                   ; Shift held?
    jnz .sel_up
    call ed_clear_selection
    call ed_move_up
    jmp .after_move
.key_down:
    test dl, 0x03
    jnz .sel_down
    call ed_clear_selection
    call ed_move_down
    jmp .after_move
.key_left:
    test dl, 0x03
    jnz .sel_left
    call ed_clear_selection
    call ed_move_left
    jmp .after_move
.key_right:
    test dl, 0x03
    jnz .sel_right
    call ed_clear_selection
    call ed_move_right
    jmp .after_move
.key_home:
    test dl, 0x03
    jnz .sel_home
    call ed_clear_selection
    call ed_move_home
    jmp .after_move
.key_end:
    test dl, 0x03
    jnz .sel_end
    call ed_clear_selection
    call ed_move_end
    jmp .after_move
.key_pgup:
    call ed_clear_selection
    call ed_move_pgup
    jmp .after_move
.key_pgdn:
    call ed_clear_selection
    call ed_move_pgdn
    jmp .after_move

.after_move:
    call ed_scroll_into_view
    call ed_draw_edit_area
    call ed_set_hw_cursor
.handle_done:
    ret

; Selection versions of movement
.sel_up:
    call ed_start_or_extend_sel
    call ed_move_up
    call ed_update_sel_end
    jmp .after_move
.sel_down:
    call ed_start_or_extend_sel
    call ed_move_down
    call ed_update_sel_end
    jmp .after_move
.sel_left:
    call ed_start_or_extend_sel
    call ed_move_left
    call ed_update_sel_end
    jmp .after_move
.sel_right:
    call ed_start_or_extend_sel
    call ed_move_right
    call ed_update_sel_end
    jmp .after_move
.sel_home:
    call ed_start_or_extend_sel
    call ed_move_home
    call ed_update_sel_end
    jmp .after_move
.sel_end:
    call ed_start_or_extend_sel
    call ed_move_end
    call ed_update_sel_end
    jmp .after_move

