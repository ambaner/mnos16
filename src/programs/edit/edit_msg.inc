; =============================================================================
; MESSAGES (temporary display on status bar)
; =============================================================================

ed_msg_saved:
    mov si, str_msg_saved
    jmp ed_show_msg

ed_msg_save_error:
    mov si, str_msg_save_err
    jmp ed_show_msg

ed_msg_load_error:
    mov si, str_msg_load_err
    jmp ed_show_msg

ed_msg_not_found:
    mov si, str_msg_not_found
    jmp ed_show_msg

ed_show_msg:
    ; Show message in a centered dialog box, wait for key, then redraw
    call ed_msgbox
    call ed_draw_screen
    ret

; -----------------------------------------------------------------------------
; ed_msgbox — Draw a centered message box with border, wait for key
; Input: SI = NUL-terminated message string
; Destroys: AX, BX, CX, DX, DI
; -----------------------------------------------------------------------------
%define MB_BOX_W 34
%define MB_BOX_LEFT ((SCREEN_COLS - MB_BOX_W) / 2)
%define MB_ROW 10

ed_msgbox:
    push es
    push si

    ; Hide cursor (move off-screen)
    mov dh, 25
    mov dl, 0
    mov ah, SYS_SET_CURSOR
    int 0x80

    mov ax, VGA_SEG
    mov es, ax

    ; Top border ┌──────┐
    mov di, MB_ROW * SCREEN_COLS * 2 + MB_BOX_LEFT * 2
    mov ah, VGA_ATTR_INV
    mov al, 0xDA
    stosw
    mov al, 0xC4
    mov cx, MB_BOX_W - 2
    rep stosw
    mov al, 0xBF
    stosw

    ; Empty row
    mov di, (MB_ROW + 1) * SCREEN_COLS * 2 + MB_BOX_LEFT * 2
    mov al, 0xB3
    stosw
    mov al, ' '
    mov cx, MB_BOX_W - 2
    rep stosw
    mov al, 0xB3
    stosw

    ; Message row: │ <space-padded message> │
    mov di, (MB_ROW + 2) * SCREEN_COLS * 2 + MB_BOX_LEFT * 2
    mov al, 0xB3
    stosw
    mov al, ' '
    stosw                           ; Left padding space
    ; Print message chars (up to MB_BOX_W-4)
    mov cx, MB_BOX_W - 4
.mb_print:
    lodsb
    or al, al
    jz .mb_pad
    mov ah, VGA_ATTR_INV
    stosw
    dec cx
    jnz .mb_print
.mb_pad:
    ; Pad remaining with spaces
    mov al, ' '
    mov ah, VGA_ATTR_INV
    rep stosw
    mov al, ' '
    stosw                           ; Right padding space
    mov al, 0xB3
    stosw

    ; Empty row
    mov di, (MB_ROW + 3) * SCREEN_COLS * 2 + MB_BOX_LEFT * 2
    mov ah, VGA_ATTR_INV
    mov al, 0xB3
    stosw
    mov al, ' '
    mov cx, MB_BOX_W - 2
    rep stosw
    mov al, 0xB3
    stosw

    ; Bottom border └──────┘
    mov di, (MB_ROW + 4) * SCREEN_COLS * 2 + MB_BOX_LEFT * 2
    mov ah, VGA_ATTR_INV
    mov al, 0xC0
    stosw
    mov al, 0xC4
    mov cx, MB_BOX_W - 2
    rep stosw
    mov al, 0xD9
    stosw

    ; Wait for key
    mov ah, SYS_READ_KEY
    int 0x80

    pop si
    pop es
    ret

; -----------------------------------------------------------------------------
; ed_confirm_yn — Show message box dialog, wait for Y or N key
; Input: SI = NUL-terminated message string
; Output: AL = 'Y' or 'N'
; -----------------------------------------------------------------------------
ed_confirm_yn:
    call ed_msgbox                  ; Draw box and wait for key (key in AL)
    ; Normalize to uppercase
    cmp al, 'y'
    je .cy_yes
    cmp al, 'Y'
    je .cy_yes
    ; Anything else = No
    mov al, 'N'
    jmp .cy_done
.cy_yes:
    mov al, 'Y'
.cy_done:
    ; Redraw screen to clear the dialog
    call ed_draw_screen
    ret

