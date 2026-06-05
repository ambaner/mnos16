; =============================================================================
; EXIT / HELP / ESCAPE
; =============================================================================

ed_cmd_exit:
    ; Check if modified
    cmp byte [modified], 0
    je .exit_now

    ; Ask to save (show message, Y/N)
    call ed_prompt_save_before_exit
    cmp al, 'Y'
    jne .exit_check_no
    call ed_cmd_save
    jmp .exit_now
.exit_check_no:
    cmp al, 'N'
    jne .exit_cancelled
.exit_now:
    ; Clear screen and exit to shell
    call mn_clear_screen
    call mn_exit                    ; Does not return
.exit_cancelled:
    ret

ed_cmd_escape:
    cmp byte [menu_open], 0
    jne .esc_close_menu
    ; Clear selection
    call ed_clear_selection
    call ed_draw_edit_area
    ret
.esc_close_menu:
    mov byte [menu_open], 0
    call ed_draw_edit_area
    ret

ed_cmd_help:
    ; Display help screen (simple overlay)
    call ed_draw_help
    ; Wait for any key
    call mn_read_key
    ; Redraw
    call ed_draw_screen
    ret

; -----------------------------------------------------------------------------
; ed_draw_help — Draw help overlay
; -----------------------------------------------------------------------------
ed_draw_help:
    push es
    push di

    mov ax, VGA_SEG
    mov es, ax

    ; Draw centered help box (rows 3-20, cols 10-69)
    %define HELP_TOP 3
    %define HELP_LEFT 10
    %define HELP_WIDTH 50
    %define HELP_LINES 16

    mov dh, HELP_TOP
    mov cx, HELP_LINES + 2          ; Including top/bottom border

.help_row:
    movzx di, dh
    imul di, SCREEN_COLS * 2
    add di, HELP_LEFT * 2

    mov ah, VGA_ATTR_INV
    push cx

    cmp dh, HELP_TOP
    je .help_top_border
    mov al, dh
    sub al, HELP_TOP
    cmp al, HELP_LINES + 1
    je .help_bot_border

    ; Content row — write from help text
    mov al, 0xB3                    ; │
    stosw
    ; Find the right help line
    push si
    mov al, dh
    sub al, HELP_TOP + 1            ; 0-based help line
    movzx bx, al
    shl bx, 1                       ; Word index
    mov si, [help_lines + bx]       ; SI = pointer to help line string
    mov cx, HELP_WIDTH - 2
.help_char:
    lodsb
    or al, al
    jz .help_pad_rest
    mov ah, VGA_ATTR_INV
    stosw
    dec cx
    jmp .help_char
.help_pad_rest:
    mov ah, VGA_ATTR_INV
    mov al, ' '
    rep stosw
    pop si
    mov ah, VGA_ATTR_INV
    mov al, 0xB3
    stosw
    jmp .help_next

.help_top_border:
    mov al, 0xDA
    stosw
    mov al, 0xC4
    mov cx, HELP_WIDTH - 2
    rep stosw
    mov al, 0xBF
    stosw
    jmp .help_next

.help_bot_border:
    mov al, 0xC0
    stosw
    mov al, 0xC4
    mov cx, HELP_WIDTH - 2
    rep stosw
    mov al, 0xD9
    stosw

.help_next:
    pop cx
    inc dh
    loop .help_row

    pop di
    pop es
    ret

; -----------------------------------------------------------------------------
; ed_prompt_save_before_exit — Ask "Save changes? (Y/N/Esc)"
; Returns: AL = 'Y', 'N', or 0x1B
; -----------------------------------------------------------------------------
ed_prompt_save_before_exit:
    push es
    push di

    ; Draw on status bar
    mov ax, VGA_SEG
    mov es, ax
    mov di, STATUS_ROW * SCREEN_COLS * 2
    mov ah, VGA_ATTR_INV
    mov al, ' '
    mov cx, SCREEN_COLS
    rep stosw

    mov di, STATUS_ROW * SCREEN_COLS * 2 + 2
    mov si, str_save_prompt
    mov ah, VGA_ATTR_INV
.spe_write:
    lodsb
    or al, al
    jz .spe_read
    stosw
    jmp .spe_write

.spe_read:
    pop di
    pop es

    ; Read response
    call mn_read_key
    ; Uppercase
    cmp al, 'y'
    jne .spe_check_n
    mov al, 'Y'
    ret
.spe_check_n:
    cmp al, 'n'
    jne .spe_ret
    mov al, 'N'
.spe_ret:
    ret

