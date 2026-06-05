; =============================================================================
; DIALOG / PROMPT HELPERS
; =============================================================================

; -----------------------------------------------------------------------------
; ed_file_picker — Show file dialog with list of files
; Sets filename and filename_len if a file is selected
; Uses CLIPBOARD_ADDR as temp buffer for directory (clobbers clipboard)
; -----------------------------------------------------------------------------
%define FP_DIR_BUF  CLIPBOARD_ADDR      ; 512-byte temp buffer for directory
%define FP_BOX_TOP  4                   ; Dialog top row
%define FP_BOX_LEFT 20                  ; Dialog left column
%define FP_BOX_W    30                  ; Dialog width (chars)

ed_file_picker:
    push es
    push di
    push bx

    ; Hide cursor (move off-screen)
    mov dh, 25
    mov dl, 0
    call mn_set_cursor

    ; Clear input buffer
    mov byte [_fp_input_len], 0
    mov byte [_fp_input_buf], 0

    ; --- Load directory into temp buffer ---
    push ds
    pop es
    mov bx, FP_DIR_BUF
    call mn_list_files                  ; CL = file count, buffer filled

    ; --- Count valid (non-system, non-deleted) entries ---
    mov si, FP_DIR_BUF + MNFS_HDR_SIZE
    xor dx, dx
    mov cx, MNFS_MAX_ENTRIES

.fp_count:
    cmp byte [si], 0x00
    je .fp_count_skip
    cmp byte [si], MNFS_DELETED
    je .fp_count_skip
    test byte [si + MNFS_ENT_ATTR], MNFS_ATTR_SYSTEM
    jnz .fp_count_skip
    inc dx
.fp_count_skip:
    add si, MNFS_ENTRY_SIZE
    loop .fp_count

    mov [_fp_count], dl             ; May be 0
    mov byte [_fp_sel], 0

    ; --- Draw and interact ---
.fp_redraw:
    call ed_fp_draw_dialog

.fp_input:
    call mn_read_key

    ; Esc = cancel
    cmp al, 0x1B
    je .fp_cancel

    ; Enter = confirm
    cmp al, 0x0D
    je .fp_confirm

    ; Backspace
    cmp al, 0x08
    je .fp_bksp

    ; Extended key? (AL=0)
    or al, al
    jnz .fp_printable

    cmp ah, KEY_UP
    je .fp_up
    cmp ah, KEY_DOWN
    je .fp_down
    jmp .fp_input

.fp_up:
    cmp byte [_fp_sel], 0
    je .fp_input
    dec byte [_fp_sel]
    jmp .fp_redraw

.fp_down:
    cmp byte [_fp_count], 0
    je .fp_input
    mov al, [_fp_count]
    dec al
    cmp [_fp_sel], al
    jae .fp_input
    inc byte [_fp_sel]
    jmp .fp_redraw

.fp_printable:
    ; Printable char (AL >= 0x20)
    cmp al, 0x20
    jb .fp_input
    ; Append to input buffer if not full
    movzx bx, byte [_fp_input_len]
    cmp bl, 12                      ; Max 12 chars (8.3 + dot)
    jae .fp_input
    mov [_fp_input_buf + bx], al
    inc byte [_fp_input_len]
    mov byte [_fp_input_buf + bx + 1], 0  ; NUL terminate
    jmp .fp_redraw

.fp_bksp:
    cmp byte [_fp_input_len], 0
    je .fp_input
    dec byte [_fp_input_len]
    movzx bx, byte [_fp_input_len]
    mov byte [_fp_input_buf + bx], 0
    jmp .fp_redraw

.fp_confirm:
    ; If input field has text, use typed name
    cmp byte [_fp_input_len], 0
    jnz .fp_use_typed
    ; Otherwise use highlighted file from list
    cmp byte [_fp_count], 0
    je .fp_cancel                   ; No files and no input
    jmp .fp_select_from_list

.fp_use_typed:
    ; Convert typed name to 8.3 padded format in filename buffer
    call ed_fp_parse_typed_name
    jmp .fp_done

.fp_select_from_list:
    ; Find the Nth valid entry (N = _fp_sel)
    mov si, FP_DIR_BUF + MNFS_HDR_SIZE
    mov cl, [_fp_sel]
    mov ch, 0
    mov dx, MNFS_MAX_ENTRIES
.fp_find_sel:
    cmp byte [si], 0x00
    je .fp_find_next
    cmp byte [si], MNFS_DELETED
    je .fp_find_next
    test byte [si + MNFS_ENT_ATTR], MNFS_ATTR_SYSTEM
    jnz .fp_find_next
    or cl, cl
    jz .fp_found_it
    dec cl
.fp_find_next:
    add si, MNFS_ENTRY_SIZE
    dec dx
    jnz .fp_find_sel
    jmp .fp_cancel

.fp_found_it:
    push si
    mov di, filename
    mov cx, MNFS_NAME_LEN
    rep movsb
    mov byte [di], 0
    mov byte [filename_len], MNFS_NAME_LEN
    pop si
    jmp .fp_done

.fp_cancel:
    mov byte [filename_len], 0

.fp_done:
    call ed_draw_screen
    pop bx
    pop di
    pop es
    ret

; -----------------------------------------------------------------------------
; ed_fp_parse_typed_name — Convert "NAME.EXT" to 8.3 padded format
; Reads from _fp_input_buf, writes to filename/filename_len
; -----------------------------------------------------------------------------
ed_fp_parse_typed_name:
    mov si, _fp_input_buf
    mov di, filename
    ; Fill with spaces first (8+3)
    push di
    mov al, ' '
    mov cx, 11
    rep stosb
    pop di

    ; Copy name part (up to 8 chars, stop at dot or end)
    mov cx, 8
.ptn_name:
    lodsb
    or al, al
    jz .ptn_done
    cmp al, '.'
    je .ptn_ext
    ; Convert to uppercase
    cmp al, 'a'
    jb .ptn_store
    cmp al, 'z'
    ja .ptn_store
    sub al, 0x20
.ptn_store:
    stosb
    dec cx
    jnz .ptn_name
    ; Skip any remaining name chars before dot
.ptn_skip:
    lodsb
    or al, al
    jz .ptn_done
    cmp al, '.'
    jne .ptn_skip

.ptn_ext:
    ; Position DI at extension (offset 8)
    mov di, filename + 8
    mov cx, 3
.ptn_ext_ch:
    lodsb
    or al, al
    jz .ptn_done
    cmp al, 'a'
    jb .ptn_ext_st
    cmp al, 'z'
    ja .ptn_ext_st
    sub al, 0x20
.ptn_ext_st:
    stosb
    dec cx
    jnz .ptn_ext_ch

.ptn_done:
    mov byte [filename + 11], 0     ; NUL terminate
    mov byte [filename_len], MNFS_NAME_LEN
    ret

; --- Draw the file picker dialog box ---
ed_fp_draw_dialog:
    push es
    push di

    mov ax, VGA_SEG
    mov es, ax

    ; Draw top border at FP_BOX_TOP, FP_BOX_LEFT
    mov di, FP_BOX_TOP
    imul di, SCREEN_COLS * 2
    add di, FP_BOX_LEFT * 2

    mov ah, VGA_ATTR_INV
    mov al, 0xDA                    ; ┌
    stosw
    mov al, 0xC4                    ; ─
    mov cx, FP_BOX_W - 2
    rep stosw
    mov al, 0xBF                    ; ┐
    stosw

    ; Title row
    mov di, FP_BOX_TOP + 1
    imul di, SCREEN_COLS * 2
    add di, FP_BOX_LEFT * 2

    mov ah, VGA_ATTR_INV
    mov al, 0xB3                    ; │
    stosw
    ; Write title centered (from _fp_title_ptr)
    mov al, ' '
    mov cx, 4
    rep stosw
    mov si, [_fp_title_ptr]
    xor dx, dx                      ; DX = title char count
.fp_title_ch:
    lodsb
    or al, al
    jz .fp_title_pad
    mov ah, VGA_ATTR_INV
    stosw
    inc dx
    jmp .fp_title_ch
.fp_title_pad:
    mov ah, VGA_ATTR_INV
    mov al, ' '
    mov cx, FP_BOX_W - 2 - 4       ; Total content minus left padding
    sub cx, dx                      ; Minus title chars
    rep stosw
    mov al, 0xB3                    ; │
    stosw

    ; Input field row: │ File: [__________] │
    mov di, FP_BOX_TOP + 2
    imul di, SCREEN_COLS * 2
    add di, FP_BOX_LEFT * 2
    mov ah, VGA_ATTR_INV
    mov al, 0xB3                    ; │
    stosw
    ; "File: " label
    mov si, str_fp_file_lbl
.fp_lbl_ch:
    lodsb
    or al, al
    jz .fp_lbl_done
    mov ah, VGA_ATTR_INV
    stosw
    jmp .fp_lbl_ch
.fp_lbl_done:
    ; Draw input content (bright white on blue for visibility)
    mov ah, VGA_ATTR_NORM           ; Input field stands out
    mov si, _fp_input_buf
    movzx cx, byte [_fp_input_len]
    jcxz .fp_inp_pad
.fp_inp_ch:
    lodsb
    stosw
    dec cx
    jnz .fp_inp_ch
.fp_inp_pad:
    ; Pad remaining input area with underscores
    mov al, '_'
    mov cx, FP_BOX_W - 2 - 7       ; 7 = "│ File: " width minus border
    movzx dx, byte [_fp_input_len]
    sub cx, dx
    jle .fp_inp_border
    rep stosw
.fp_inp_border:
    mov ah, VGA_ATTR_INV
    mov al, ' '
    stosw
    mov al, 0xB3                    ; │
    stosw

    ; Separator line
    mov di, FP_BOX_TOP + 3
    imul di, SCREEN_COLS * 2
    add di, FP_BOX_LEFT * 2
    mov ah, VGA_ATTR_INV
    mov al, 0xC3                    ; ├
    stosw
    mov al, 0xC4                    ; ─
    mov cx, FP_BOX_W - 2
    rep stosw
    mov al, 0xB4                    ; ┤
    stosw

    ; Draw file entries
    mov si, FP_DIR_BUF + MNFS_HDR_SIZE
    xor bl, bl                      ; BL = valid entry index
    mov byte [_fp_row], FP_BOX_TOP + 4
    mov cx, MNFS_MAX_ENTRIES

.fp_draw_entry:
    push cx
    cmp byte [si], 0x00
    je .fp_draw_skip
    cmp byte [si], MNFS_DELETED
    je .fp_draw_skip
    test byte [si + MNFS_ENT_ATTR], MNFS_ATTR_SYSTEM
    jnz .fp_draw_skip

    ; Draw this file entry
    push si
    movzx di, byte [_fp_row]
    imul di, SCREEN_COLS * 2
    add di, FP_BOX_LEFT * 2

    ; Left border
    mov ah, VGA_ATTR_INV
    mov al, 0xB3
    stosw

    ; Determine attribute (highlight if selected)
    cmp bl, [_fp_sel]
    jne .fp_draw_norm
    mov ah, VGA_ATTR_MENU_HI
    jmp .fp_draw_text
.fp_draw_norm:
    mov ah, VGA_ATTR_INV
.fp_draw_text:
    ; Space before name
    mov al, ' '
    stosw

    ; Print filename (8 chars)
    push cx
    mov cx, 8
.fp_name_ch:
    lodsb
    cmp al, ' '                     ; Skip trailing spaces in name
    je .fp_name_sp
    stosw
    dec cx
    jnz .fp_name_ch
    jmp .fp_dot
.fp_name_sp:
    ; Remaining name chars are spaces, skip them
    dec cx
.fp_skip_sp:
    jcxz .fp_dot
    lodsb
    dec cx
    jmp .fp_skip_sp
.fp_dot:
    ; Print dot
    mov al, '.'
    stosw
    ; Print extension (3 chars)
    mov cx, 3
.fp_ext_ch:
    lodsb
    cmp al, ' '
    je .fp_ext_done
    stosw
    dec cx
    jnz .fp_ext_ch
.fp_ext_done:
    pop cx

    ; Pad rest of line
    ; Calculate how many chars we've written so far in content area
    ; DI is current VGA pos, start was FP_BOX_LEFT*2 + row_offset + 2(border)
    push bx
    movzx bx, byte [_fp_row]
    imul bx, SCREEN_COLS * 2
    add bx, FP_BOX_LEFT * 2
    add bx, 2                       ; After left border
    ; Chars written = (DI - bx) / 2
    push ax
    mov ax, di
    sub ax, bx
    shr ax, 1                       ; AX = chars written in content area
    mov cx, FP_BOX_W - 2
    sub cx, ax                      ; CX = remaining chars to pad
    pop ax
    pop bx

    mov al, ' '
    cmp bl, [_fp_sel]
    jne .fp_pad_norm
    mov ah, VGA_ATTR_MENU_HI
    jmp .fp_pad
.fp_pad_norm:
    mov ah, VGA_ATTR_INV
.fp_pad:
    rep stosw

    ; Right border
    mov ah, VGA_ATTR_INV
    mov al, 0xB3
    stosw

    pop si
    inc byte [_fp_row]
    inc bl

.fp_draw_skip:
    add si, MNFS_ENTRY_SIZE
    pop cx
    dec cx
    jnz .fp_draw_entry

    ; Bottom border
    movzx di, byte [_fp_row]
    imul di, SCREEN_COLS * 2
    add di, FP_BOX_LEFT * 2
    mov ah, VGA_ATTR_INV
    mov al, 0xC0                    ; └
    stosw
    mov al, 0xC4                    ; ─
    mov cx, FP_BOX_W - 2
    rep stosw
    mov al, 0xD9                    ; ┘
    stosw

    pop di
    pop es
    ret

; File picker temp vars
_fp_count     db 0
_fp_sel       db 0
_fp_row       db 0
_fp_title_ptr dw 0
_fp_input_len db 0
_fp_input_buf times 14 db 0         ; Max 12 chars + NUL + padding

str_fp_title      db 'Open File', 0
str_fp_save_title db 'Save As', 0
str_fp_file_lbl   db 'File: ', 0

; -----------------------------------------------------------------------------
; ed_prompt_filename — Show filename prompt on status bar
; Sets filename and filename_len
; -----------------------------------------------------------------------------
ed_prompt_filename:
    mov si, str_prompt_filename
    mov di, filename
    mov cl, 11                      ; Max 11 chars (8.3)
    call ed_input_prompt
    mov [filename_len], al          ; AL = chars entered
    ret

; -----------------------------------------------------------------------------
; ed_prompt_search — Prompt for search string
; -----------------------------------------------------------------------------
ed_prompt_search:
    mov si, str_prompt_search
    mov di, SEARCH_BUF_ADDR
    mov cl, SEARCH_BUF_SIZE - 1
    call ed_input_prompt
    mov [search_len], al
    ret

; -----------------------------------------------------------------------------
; ed_prompt_replace — Prompt for replace string
; -----------------------------------------------------------------------------
ed_prompt_replace:
    mov si, str_prompt_replace
    mov di, REPLACE_BUF_ADDR
    mov cl, SEARCH_BUF_SIZE - 1
    call ed_input_prompt
    mov [replace_len], al
    ret

; -----------------------------------------------------------------------------
; ed_prompt_line_number — Prompt for line number
; Returns: AX = number entered (0 if cancelled)
; -----------------------------------------------------------------------------
ed_prompt_line_number:
    mov si, str_prompt_goto
    mov di, _num_buf
    mov cl, 5                       ; Max 5 digits
    call ed_input_prompt
    or al, al
    jz .pln_cancel
    ; Convert string to number
    mov si, _num_buf
    call ed_atoi                    ; AX = number
    ret
.pln_cancel:
    xor ax, ax
    ret

; -----------------------------------------------------------------------------
; ed_input_prompt — Modal dialog input prompt (centered box)
; Input: SI = prompt/title string, DI = output buffer, CL = max chars
; Returns: AL = chars entered (0 if Esc pressed)
; Draws a centered bordered dialog, handles input, redraws screen on exit
; -----------------------------------------------------------------------------
%define MD_BOX_TOP   9
%define MD_BOX_LEFT  20
%define MD_BOX_W     40
%define MD_INPUT_MAX 34                 ; MD_BOX_W - 4 (borders + padding)

ed_input_prompt:
    push es
    push bx
    push dx

    ; Save parameters
    mov [_md_buf_ptr], di           ; Output buffer pointer
    mov [_md_title_ptr], si         ; Title string
    movzx ax, cl
    cmp ax, MD_INPUT_MAX
    jbe .ip_max_ok
    mov ax, MD_INPUT_MAX
.ip_max_ok:
    mov [_md_max_len], ax           ; Max input chars
    mov word [_md_cur_len], 0       ; Current input length

    ; Draw the dialog
    call ed_md_draw_dialog

    ; Input loop
.ip_read:
    ; Position cursor inside the input field
    mov ax, [_md_cur_len]
    add al, MD_BOX_LEFT + 3         ; +3 for border + 2 spaces padding
    mov dl, al                      ; DL = column
    mov dh, MD_BOX_TOP + 1          ; DH = input row (row after top border)
    call mn_set_cursor

    ; Read key
    call mn_read_key

    ; Esc = cancel
    cmp al, 0x1B
    je .ip_cancel

    ; Enter = accept
    cmp al, 0x0D
    je .ip_accept

    ; Backspace
    cmp al, 0x08
    je .ip_backspace

    ; Printable char
    cmp al, 0x20
    jb .ip_read
    push ax                         ; Save char
    mov bx, [_md_cur_len]
    cmp bx, [_md_max_len]
    pop ax
    jge .ip_read                    ; At max, ignore

    ; Store char in buffer
    push bx
    mov bx, [_md_buf_ptr]
    add bx, [_md_cur_len]
    mov [bx], al
    inc word [_md_cur_len]
    mov byte [bx + 1], 0           ; NUL terminate
    pop bx

    ; Echo char to VGA
    push es
    push di
    mov bx, VGA_SEG
    mov es, bx
    mov di, (MD_BOX_TOP + 1) * SCREEN_COLS * 2
    add di, (MD_BOX_LEFT + 3) * 2
    push cx
    mov cx, [_md_cur_len]
    dec cx
    shl cx, 1
    add di, cx
    pop cx
    mov ah, VGA_ATTR_NORM
    stosw
    pop di
    pop es
    jmp .ip_read

.ip_backspace:
    cmp word [_md_cur_len], 0
    je .ip_read
    dec word [_md_cur_len]

    ; Clear char on VGA
    push es
    push di
    mov bx, VGA_SEG
    mov es, bx
    mov di, (MD_BOX_TOP + 1) * SCREEN_COLS * 2
    add di, (MD_BOX_LEFT + 3) * 2
    push cx
    mov cx, [_md_cur_len]
    shl cx, 1
    add di, cx
    pop cx
    mov ah, VGA_ATTR_NORM
    mov al, '_'
    stosw
    pop di
    pop es

    ; Update buffer NUL
    push bx
    mov bx, [_md_buf_ptr]
    add bx, [_md_cur_len]
    mov byte [bx], 0
    pop bx
    jmp .ip_read

.ip_accept:
    ; NUL-terminate buffer
    push bx
    mov bx, [_md_buf_ptr]
    add bx, [_md_cur_len]
    mov byte [bx], 0
    pop bx

    mov ax, [_md_cur_len]           ; AL = chars entered
    ; Redraw full screen to clear dialog
    push ax
    call ed_draw_screen
    pop ax

    pop dx
    pop bx
    pop es
    ret

.ip_cancel:
    ; NUL-terminate at start
    push bx
    mov bx, [_md_buf_ptr]
    mov byte [bx], 0
    pop bx
    xor al, al                      ; 0 = cancelled

    ; Redraw full screen to clear dialog
    push ax
    call ed_draw_screen
    pop ax

    pop dx
    pop bx
    pop es
    ret

; --- Modal dialog vars ---
_md_buf_ptr   dw 0
_md_title_ptr dw 0
_md_max_len   dw 0
_md_cur_len   dw 0

; --- Draw the modal dialog box ---
ed_md_draw_dialog:
    push es
    push di
    push cx
    push bx

    mov ax, VGA_SEG
    mov es, ax

    ; === Row 0: Top border with title ===
    ; ┌── Title ─────────────────────────────┐
    mov di, MD_BOX_TOP * SCREEN_COLS * 2 + MD_BOX_LEFT * 2
    mov ah, VGA_ATTR_INV
    mov al, 0xDA                    ; ┌
    stosw
    mov al, 0xC4                    ; ─
    stosw
    stosw                           ; Two ─ before space
    mov al, ' '
    stosw                           ; Space before title

    ; Write title
    push si
    mov si, [_md_title_ptr]
    xor cx, cx
.md_title_ch:
    lodsb
    or al, al
    jz .md_title_done
    mov ah, VGA_ATTR_INV
    stosw
    inc cx
    jmp .md_title_ch
.md_title_done:
    pop si
    ; Space after title then fill remaining with ─
    mov ah, VGA_ATTR_INV
    mov al, ' '
    stosw                           ; Space after title
    mov al, 0xC4                    ; ─
    push dx
    mov dx, MD_BOX_W - 5            ; Total minus: 1(┌) + 2(─) + 1(sp) + 1(sp) = 5 fixed + title + rest + ┐
    sub dx, cx                      ; dx = space for ─ chars + ┐
    mov cx, dx
    pop dx
    dec cx                          ; -1 for ┐
    rep stosw
    mov al, 0xBF                    ; ┐
    stosw

    ; === Row 1: Input field with underscores ===
    mov di, (MD_BOX_TOP + 1) * SCREEN_COLS * 2 + MD_BOX_LEFT * 2
    mov ah, VGA_ATTR_INV
    mov al, 0xB3                    ; │
    stosw
    mov al, ' '
    stosw
    stosw                           ; 2 spaces left padding

    ; Draw input underscores (in NORM attr for visibility)
    mov ah, VGA_ATTR_NORM
    mov al, '_'
    mov cx, MD_BOX_W - 6            ; Content area minus borders and padding
    rep stosw

    ; Right padding + border
    mov ah, VGA_ATTR_INV
    mov al, ' '
    stosw
    stosw
    mov al, 0xB3                    ; │
    stosw

    ; === Row 2: Hint row ===
    mov di, (MD_BOX_TOP + 2) * SCREEN_COLS * 2 + MD_BOX_LEFT * 2
    mov ah, VGA_ATTR_INV
    mov al, 0xB3                    ; │
    stosw
    ; Write hint text
    push si
    mov si, _md_hint_str
    xor cx, cx
.md_hint_ch:
    lodsb
    or al, al
    jz .md_hint_done
    mov ah, VGA_ATTR_INV
    stosw
    inc cx
    jmp .md_hint_ch
.md_hint_done:
    pop si
    ; Pad remaining
    mov ah, VGA_ATTR_INV
    mov al, ' '
    push dx
    mov dx, MD_BOX_W - 2
    sub dx, cx
    mov cx, dx
    pop dx
    rep stosw
    mov al, 0xB3                    ; │
    stosw

    ; === Row 3: Bottom border ===
    mov di, (MD_BOX_TOP + 3) * SCREEN_COLS * 2 + MD_BOX_LEFT * 2
    mov ah, VGA_ATTR_INV
    mov al, 0xC0                    ; └
    stosw
    mov al, 0xC4                    ; ─
    mov cx, MD_BOX_W - 2
    rep stosw
    mov al, 0xD9                    ; ┘
    stosw

    pop bx
    pop cx
    pop di
    pop es
    ret

_md_hint_str db '  Enter=OK  Esc=Cancel', 0

; -----------------------------------------------------------------------------
; ed_atoi — Convert decimal string to integer
; Input: SI = NUL-terminated string
; Returns: AX = number
; -----------------------------------------------------------------------------
ed_atoi:
    xor ax, ax
    mov bx, 10
.atoi_loop:
    movzx cx, byte [si]
    or cl, cl
    jz .atoi_done
    sub cl, '0'
    cmp cl, 9
    ja .atoi_done
    mul bx                          ; AX *= 10
    add ax, cx
    inc si
    jmp .atoi_loop
.atoi_done:
    ret

