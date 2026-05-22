; =============================================================================
; ED_DRAW_SCREEN — Draw entire screen (menu + edit area + status)
; =============================================================================
ed_draw_screen:
    call ed_draw_menu
    call ed_draw_edit_area
    call ed_draw_status
    call ed_set_hw_cursor
    ret

; =============================================================================
; ED_DRAW_MENU — Draw menu bar (row 0)
; =============================================================================
ed_draw_menu:
    push es
    push di

    mov ax, VGA_SEG
    mov es, ax

    ; Row 0, offset 0
    xor di, di

    ; Fill entire row with inverse spaces
    mov ah, VGA_ATTR_INV
    mov al, ' '
    mov cx, SCREEN_COLS
    rep stosw

    ; Write menu items at specific positions
    ; " File  Edit  Search "
    xor di, di                      ; Back to start of row 0
    add di, 2                       ; Column 1 (word offset = col*2)

    mov si, str_menu_file
    call .write_menu_item
    add di, 4                       ; Gap
    mov si, str_menu_edit
    call .write_menu_item
    add di, 4                       ; Gap
    mov si, str_menu_search
    call .write_menu_item

    pop di
    pop es
    ret

.write_menu_item:
    ; Write first char with hotkey attribute, rest with inverse
    lodsb
    or al, al
    jz .wmi_done
    mov ah, VGA_ATTR_HOTKEY
    stosw
    mov ah, VGA_ATTR_INV
.wmi_loop:
    lodsb
    or al, al
    jz .wmi_done
    stosw
    jmp .wmi_loop
.wmi_done:
    ret

; =============================================================================
; ED_DRAW_STATUS — Draw status bar (row 24)
; =============================================================================
ed_draw_status:
    push es
    push di
    push bx

    mov ax, VGA_SEG
    mov es, ax

    ; Row 24 offset = 24 * 80 * 2 = 3840
    mov di, STATUS_ROW * SCREEN_COLS * 2

    ; Fill with inverse spaces
    mov ah, VGA_ATTR_INV
    mov al, ' '
    mov cx, SCREEN_COLS
    rep stosw

    ; Reset to start of status row
    mov di, STATUS_ROW * SCREEN_COLS * 2
    add di, 2                       ; Column 1

    ; Write filename (or "Untitled")
    mov ah, VGA_ATTR_INV
    cmp byte [filename_len], 0
    je .status_untitled

    mov si, filename
    call .write_status_str
    jmp .status_after_name

.status_untitled:
    mov si, str_untitled
    call .write_status_str

.status_after_name:
    ; Add spacing
    add di, 4

    ; Write "Ln:" + line number
    mov si, str_ln
    call .write_status_str
    mov ax, [cursor_row]
    inc ax                          ; 1-based display
    call .write_status_dec

    ; Write " Col:" + column
    add di, 2
    mov si, str_col
    call .write_status_str
    mov ax, [cursor_col]
    inc ax                          ; 1-based display
    call .write_status_dec

    ; Modified indicator
    cmp byte [modified], 0
    je .status_no_mod
    add di, 4
    mov si, str_modified
    call .write_status_str
.status_no_mod:

    ; Insert/Overwrite indicator on the right side
    mov di, STATUS_ROW * SCREEN_COLS * 2
    add di, (SCREEN_COLS - 12) * 2  ; Near right edge
    cmp byte [insert_mode], 0
    jne .status_ins
    mov si, str_ovr
    jmp .status_mode
.status_ins:
    mov si, str_ins
.status_mode:
    call .write_status_str

    pop bx
    pop di
    pop es
    ret

.write_status_str:
    ; Write NUL-terminated string at ES:DI, inverse attr
    mov ah, VGA_ATTR_INV
.wss_loop:
    lodsb
    or al, al
    jz .wss_done
    stosw
    jmp .wss_loop
.wss_done:
    ret

.write_status_dec:
    ; Write AX as decimal at ES:DI (inverse attr), max 5 digits
    push bx
    push cx
    push dx
    mov cx, 0                       ; Digit count
    mov bx, 10
.wsd_div:
    xor dx, dx
    div bx
    push dx
    inc cx
    or ax, ax
    jnz .wsd_div
    ; Write digits
    mov ah, VGA_ATTR_INV
.wsd_write:
    pop dx
    add dl, '0'
    mov al, dl
    stosw
    loop .wsd_write
    pop dx
    pop cx
    pop bx
    ret

; =============================================================================
; ED_DRAW_EDIT_AREA — Render visible lines from gap buffer
; =============================================================================
ed_draw_edit_area:
    push es
    push di
    push bx

    mov ax, VGA_SEG
    mov es, ax

    ; Start at row 1 (first edit row)
    mov di, EDIT_TOP_ROW * SCREEN_COLS * 2

    ; We need to render EDIT_LINES lines starting from view_top
    mov cx, EDIT_LINES
    mov bx, [view_top]              ; Current line index to render

.draw_line_loop:
    push cx
    push bx

    ; Get pointer to start of line 'bx' in the gap buffer
    ; and render one screen line
    call ed_render_line             ; BX = line index, ES:DI = VGA position
                                    ; advances DI by SCREEN_COLS*2

    pop bx
    pop cx
    inc bx
    loop .draw_line_loop

    pop bx
    pop di
    pop es
    ret

; =============================================================================
; ED_RENDER_LINE — Render one line to VGA
;
; Input:  BX = logical line number
;         ES:DI = VGA position (start of row)
; Output: DI advanced to next row
; Trashes: AX, SI, DX
; =============================================================================
ed_render_line:
    push cx
    push di                         ; Save row start for clearing

    ; Check if line exists
    cmp bx, [total_lines]
    jae .render_empty

    ; Get buffer position of line start
    push di
    mov ax, bx
    call ed_get_line_offset         ; Returns SI = buffer pointer to line start
    pop di

    ; Render characters until newline or end of text or 80 cols
    mov cx, SCREEN_COLS
    mov ah, VGA_ATTR_NORM

.render_char:
    ; Get next character from gap buffer
    call ed_gap_char_at_si          ; AL = char (or 0 if past end), advances SI
    or al, al
    jz .render_pad                  ; Past end of text
    cmp al, 0x0A                    ; Newline?
    je .render_pad
    cmp al, 0x0D                    ; CR?
    je .render_skip_cr
    cmp al, 0x09                    ; Tab?
    je .render_tab

    ; Regular character — write to VGA
    stosw
    dec cx
    jz .render_done
    jmp .render_char

.render_skip_cr:
    jmp .render_char                ; Skip CR, continue

.render_tab:
    ; Expand tab to spaces
    mov al, ' '
    push dx
    ; Calculate current column: SCREEN_COLS - cx
    mov dx, SCREEN_COLS
    sub dx, cx
    ; Spaces to next tab stop
.tab_loop:
    cmp cx, 0
    je .tab_done
    stosw
    dec cx
    inc dx
    test dx, (TAB_WIDTH - 1)        ; At tab stop? (dx % 8 == 0)
    jnz .tab_loop
.tab_done:
    pop dx
    jmp .render_char

.render_pad:
    ; Fill rest of line with spaces
    mov al, ' '
    rep stosw
    jmp .render_done

.render_empty:
    ; Empty line (past end of file) — fill with spaces
    mov al, ' '
    mov ah, VGA_ATTR_NORM
    mov cx, SCREEN_COLS
    rep stosw

.render_done:
    add sp, 2                       ; Discard saved DI (DI already at next row)
    pop cx                          ; Restore caller's CX (was push cx at start)
    ret

