; =============================================================================
; MENU SYSTEM
; =============================================================================

ed_cmd_menu_file:
    mov byte [menu_open], 1
    call ed_draw_menu_file
    call ed_menu_loop
    ret

ed_cmd_menu_edit:
    mov byte [menu_open], 2
    call ed_draw_menu_edit
    call ed_menu_loop
    ret

ed_cmd_menu_search:
    mov byte [menu_open], 3
    call ed_draw_menu_search
    call ed_menu_loop
    ret

; -----------------------------------------------------------------------------
; ed_menu_loop — Handle menu navigation until selection or Esc
; -----------------------------------------------------------------------------
ed_menu_loop:
    mov byte [menu_sel], 0          ; First item selected

    ; Hide cursor while menu is active
    mov dh, 25
    mov dl, 0
    mov ah, SYS_SET_CURSOR
    int 0x80

.ml_loop:
    call ed_draw_current_menu       ; Highlight current selection

    ; Read key
    mov ah, SYS_READ_KEY
    int 0x80

    ; Check for Esc
    cmp al, 0x1B
    je .ml_close

    ; Check for Enter
    cmp al, 0x0D
    je .ml_activate

    ; Check extended keys
    or al, al
    jnz .ml_loop

    ; Arrow keys
    cmp ah, KEY_UP
    je .ml_up
    cmp ah, KEY_DOWN
    je .ml_down
    cmp ah, KEY_LEFT
    je .ml_prev_menu
    cmp ah, KEY_RIGHT
    je .ml_next_menu

    jmp .ml_loop

.ml_up:
    cmp byte [menu_sel], 0
    je .ml_loop
    dec byte [menu_sel]
    jmp .ml_loop

.ml_down:
    inc byte [menu_sel]
    ; Check max items for current menu
    call ed_get_menu_max
    cmp [menu_sel], al
    jb .ml_loop
    dec byte [menu_sel]
    jmp .ml_loop

.ml_prev_menu:
    cmp byte [menu_open], 1
    je .ml_loop
    dec byte [menu_open]
    mov byte [menu_sel], 0
    call ed_draw_edit_area          ; Clear old dropdown
    call ed_draw_menu
    call ed_draw_current_menu_box
    jmp .ml_loop

.ml_next_menu:
    cmp byte [menu_open], 3
    je .ml_loop
    inc byte [menu_open]
    mov byte [menu_sel], 0
    call ed_draw_edit_area          ; Clear old dropdown
    call ed_draw_menu
    call ed_draw_current_menu_box
    jmp .ml_loop

.ml_close:
    mov byte [menu_open], 0
    call ed_draw_edit_area          ; Restore edit area under menu
    ret

.ml_activate:
    ; Dispatch based on menu_open and menu_sel
    mov al, [menu_open]
    mov bl, [menu_sel]
    mov byte [menu_open], 0
    push ax                         ; Save menu number (AL)
    push bx                         ; Save selection (BL)
    call ed_draw_edit_area          ; Restore screen (clobbers AX)
    pop bx
    pop ax

    cmp al, 1
    je .ml_file_dispatch
    cmp al, 2
    je .ml_edit_dispatch
    cmp al, 3
    je .ml_search_dispatch
    ret

.ml_file_dispatch:
    cmp bl, 0
    je ed_cmd_new
    cmp bl, 1
    je ed_cmd_open
    cmp bl, 2
    je ed_cmd_save
    cmp bl, 3
    je ed_cmd_save_as
    cmp bl, 4
    je ed_cmd_exit
    ret

.ml_edit_dispatch:
    cmp bl, 0
    je ed_cmd_cut
    cmp bl, 1
    je ed_cmd_copy
    cmp bl, 2
    je ed_cmd_paste
    cmp bl, 3
    je ed_cmd_select_all
    ret

.ml_search_dispatch:
    cmp bl, 0
    je ed_cmd_find
    cmp bl, 1
    je ed_cmd_find_next
    cmp bl, 2
    je ed_cmd_replace
    cmp bl, 3
    je ed_cmd_replace_all
    cmp bl, 4
    je ed_cmd_goto_line
    ret

; -----------------------------------------------------------------------------
; ed_get_menu_max — Get max items for current menu
; Returns: AL = number of items
; -----------------------------------------------------------------------------
ed_get_menu_max:
    cmp byte [menu_open], 1
    je .gmm_file
    cmp byte [menu_open], 2
    je .gmm_edit
    mov al, 5                       ; Search: 5 items
    ret
.gmm_file:
    mov al, 5                       ; File: 5 items
    ret
.gmm_edit:
    mov al, 4                       ; Edit: 4 items
    ret

; -----------------------------------------------------------------------------
; ed_draw_current_menu_box — Draw the box for the currently open menu
; -----------------------------------------------------------------------------
ed_draw_current_menu_box:
    cmp byte [menu_open], 1
    je ed_draw_menu_file
    cmp byte [menu_open], 2
    je ed_draw_menu_edit
    jmp ed_draw_menu_search

; ed_draw_current_menu — Same as above but with highlight
ed_draw_current_menu:
    jmp ed_draw_current_menu_box    ; Highlight is handled inside

; -----------------------------------------------------------------------------
; Menu drawing — File menu
; -----------------------------------------------------------------------------
ed_draw_menu_file:
    push es
    push di

    mov ax, VGA_SEG
    mov es, ax

    ; File menu at column 1, rows 1-5
    ; Each row: draw box border + menu item
    mov bx, 1                       ; Start column
    mov cx, 5                       ; Item count
    mov si, menu_file_items
    mov dh, 1                       ; Start row
    call ed_draw_menu_box

    pop di
    pop es
    ret

; -----------------------------------------------------------------------------
; Menu drawing — Edit menu
; -----------------------------------------------------------------------------
ed_draw_menu_edit:
    push es
    push di

    mov ax, VGA_SEG
    mov es, ax

    mov bx, 7                       ; Start column (after "File  ")
    mov cx, 4                       ; Item count
    mov si, menu_edit_items
    mov dh, 1
    call ed_draw_menu_box

    pop di
    pop es
    ret

; -----------------------------------------------------------------------------
; Menu drawing — Search menu
; -----------------------------------------------------------------------------
ed_draw_menu_search:
    push es
    push di

    mov ax, VGA_SEG
    mov es, ax

    mov bx, 13                      ; Start column
    mov cx, 4                       ; Item count
    mov si, menu_search_items
    mov dh, 1
    call ed_draw_menu_box

    pop di
    pop es
    ret

; -----------------------------------------------------------------------------
; ed_draw_menu_box — Draw a dropdown menu box
; Input: BX = left column, CX = item count, SI = item strings ptr
;        DH = top row, ES = VGA segment
; Uses: menu_sel for highlight
; -----------------------------------------------------------------------------
ed_draw_menu_box:
    push cx
    push bx

    ; Menu width = 20 chars total (18 content + 2 border chars)
    %define MENU_WIDTH 20

    ; Draw top border
    movzx di, dh
    imul di, SCREEN_COLS * 2
    add di, bx
    add di, bx                      ; DI = VGA offset for (dh, bx)

    ; Top border line
    mov ah, VGA_ATTR_INV
    mov al, 0xDA                    ; ┌
    stosw
    mov al, 0xC4                    ; ─
    push cx
    mov cx, MENU_WIDTH - 2
    rep stosw
    pop cx
    mov al, 0xBF                    ; ┐
    stosw

    ; Draw each menu item row
    xor dl, dl                      ; Current item index
.dmb_row:
    inc dh                          ; Next row
    push cx

    ; Calculate VGA offset for this row
    movzx di, dh
    imul di, SCREEN_COLS * 2
    push bx
    shl bx, 1
    add di, bx
    pop bx

    ; Left border
    mov ah, VGA_ATTR_INV
    mov al, 0xB3                    ; │
    stosw

    ; Item text — highlight if selected
    cmp dl, [menu_sel]
    jne .dmb_normal
    mov ah, VGA_ATTR_MENU_HI        ; Highlighted menu item (bright white on black)
    jmp .dmb_text
.dmb_normal:
    mov ah, VGA_ATTR_INV
.dmb_text:
    ; Write item text (padded to MENU_WIDTH-2)
    push cx
    mov cx, MENU_WIDTH - 2
.dmb_char:
    jcxz .dmb_trunc                 ; Safety: stop if width exhausted
    lodsb
    or al, al
    jz .dmb_pad                     ; Null found → SI already past it
    stosw
    dec cx
    jmp .dmb_char
.dmb_trunc:
    ; Width exhausted before null — skip rest of string
.dmb_trunc_skip:
    cmp byte [si], 0
    je .dmb_trunc_end
    inc si
    jmp .dmb_trunc_skip
.dmb_trunc_end:
    inc si                          ; Skip past null terminator
.dmb_pad:
    mov al, ' '
    rep stosw
    pop cx

    ; Right border
    mov ah, VGA_ATTR_INV
    mov al, 0xB3                    ; │
    stosw

    inc dl                          ; Next item
    pop cx
    loop .dmb_row

    ; Bottom border
    inc dh
    movzx di, dh
    imul di, SCREEN_COLS * 2
    push bx
    shl bx, 1
    add di, bx
    pop bx

    mov ah, VGA_ATTR_INV
    mov al, 0xC0                    ; └
    stosw
    mov al, 0xC4                    ; ─
    mov cx, MENU_WIDTH - 2
    rep stosw
    mov al, 0xD9                    ; ┘
    stosw

    pop bx
    pop cx
    ret

