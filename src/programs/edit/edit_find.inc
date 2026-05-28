; =============================================================================
; FIND / REPLACE / GO TO LINE
; =============================================================================

ed_cmd_find:
    call ed_prompt_search
    cmp byte [search_len], 0
    je .find_cancelled
    call ed_find_next_from_cursor
.find_cancelled:
    ret

ed_cmd_find_next:
    cmp byte [search_len], 0
    je .fn_done
    call ed_find_next_from_cursor
.fn_done:
    ret

ed_cmd_replace:
    ; Prompt for search string
    call ed_prompt_search
    cmp byte [search_len], 0
    je .repl_cancelled
    ; Prompt for replace string
    call ed_prompt_replace
    ; Find and replace ONE occurrence
    call ed_find_and_replace_one
.repl_cancelled:
    ret

ed_cmd_replace_all:
    ; Prompt for search string
    call ed_prompt_search
    cmp byte [search_len], 0
    je .ra_cancelled
    ; Prompt for replace string
    call ed_prompt_replace
    ; Replace ALL occurrences
    call ed_sync_gap_to_cursor
    call ed_get_cursor_offset
    call ed_find_and_replace
.ra_cancelled:
    ret

ed_cmd_goto_line:
    call ed_prompt_line_number      ; AX = line number (1-based)
    or ax, ax
    jz .gl_cancelled
    dec ax                          ; Convert to 0-based
    cmp ax, [total_lines]
    jb .gl_valid
    mov ax, [total_lines]
    dec ax
.gl_valid:
    mov [cursor_row], ax
    mov word [cursor_col], 0
    call ed_sync_gap_to_cursor
    call ed_scroll_into_view
    call ed_draw_edit_area
    call ed_set_hw_cursor
.gl_cancelled:
    ret

; -----------------------------------------------------------------------------
; ed_find_next_from_cursor — Search forward for search string (wraps around)
; -----------------------------------------------------------------------------
ed_find_next_from_cursor:
    ; Ensure gap is synced to visual cursor position
    call ed_sync_gap_to_cursor

    ; Get current offset + 1
    call ed_get_cursor_offset
    inc ax                          ; Start searching after cursor

%ifdef EDIT_DEBUG
    push ax
    mov dx, ax
    mov ah, SYS_DBG_HEX16
    xor bx, bx
    int 0x80                        ; Print search start offset
    pop ax
%endif

    push ax                         ; Save start position for wrap detection

    ; Scan text from offset AX for the search string
    call ed_search_text             ; Returns: AX = found offset, CF if not found
    jc .fnfc_wrap

%ifdef EDIT_DEBUG
    push ax
    mov dx, ax
    mov ah, SYS_DBG_HEX16
    xor bx, bx
    int 0x80                        ; Print found offset
    pop ax
%endif

    ; Found — move cursor to found position
    pop bx                          ; Discard saved start

%ifdef EDIT_DEBUG
    ; Trace: AX before gap_move_to and gap_start
    push ax
    mov dx, ax
    mov ah, SYS_DBG_HEX16
    mov bx, 0xAAAA                  ; marker AAAA = "before gap_move_to"
    int 0x80
    mov dx, [gap_start]
    mov ah, SYS_DBG_HEX16
    mov bx, 0xBBBB                  ; marker BBBB = gap_start value
    int 0x80
    mov dx, [gap_end]
    mov ah, SYS_DBG_HEX16
    mov bx, 0xCCCC                  ; marker CCCC = gap_end value
    int 0x80
    pop ax
%endif

    call ed_gap_move_to

%ifdef EDIT_DEBUG
    push ax
    mov dx, [gap_start]
    mov ah, SYS_DBG_HEX16
    mov bx, 0xDDDD                  ; marker DDDD = gap_start after move
    int 0x80
    pop ax
%endif

    call ed_recalc_cursor_from_gap

%ifdef EDIT_DEBUG
    push ax
    mov dx, [cursor_col]
    mov ah, SYS_DBG_HEX16
    mov bx, 0xEEEE                  ; marker EEEE = cursor_col after recalc
    int 0x80
    pop ax
%endif

    call ed_scroll_into_view
    call ed_draw_edit_area
    call ed_set_hw_cursor
    ret

.fnfc_wrap:
    ; Not found after cursor — try wrapping from beginning
    pop bx                          ; BX = original start offset
    cmp bx, 1
    jle .fnfc_not_found             ; Already searched from start, no wrap needed

%ifdef EDIT_DEBUG
    push si
    mov si, .dbg_wrap
    mov ah, SYS_DBG_PRINT
    xor bx, bx
    int 0x80
    pop si
%endif

    xor ax, ax                      ; Search from offset 0
    call ed_search_text
    jc .fnfc_not_found

%ifdef EDIT_DEBUG
    push ax
    mov dx, ax
    mov ah, SYS_DBG_HEX16
    xor bx, bx
    int 0x80                        ; Print wrap-found offset
    pop ax
%endif

    ; Found via wrap — move cursor
    call ed_gap_move_to
    call ed_recalc_cursor_from_gap
    call ed_scroll_into_view
    call ed_draw_edit_area
    call ed_set_hw_cursor
    ret

.fnfc_not_found:
%ifdef EDIT_DEBUG
    push si
    mov si, .dbg_nf
    mov ah, SYS_DBG_PRINT
    xor bx, bx
    int 0x80
    pop si
%endif
    call ed_msg_not_found
    ret

%ifdef EDIT_DEBUG
.dbg_wrap: db '[FIND] wrap', 0
.dbg_nf:   db '[FIND] not_found', 0
%endif

; -----------------------------------------------------------------------------
; ed_search_text — Search for search_buf in text starting at offset AX
; Input: AX = start offset
; Returns: AX = found offset, CF set if not found
; -----------------------------------------------------------------------------
ed_search_text:
    push bx
    push cx
    push dx
    push di

    mov dx, ax                      ; DX = current search position
    call ed_get_text_length
    mov bx, ax                      ; BX = text length

.st_outer:
    cmp dx, bx
    jge .st_not_found

    ; Compare search string at position DX
    push dx
    mov cx, dx
    mov di, SEARCH_BUF_ADDR
    movzx ax, byte [search_len]

.st_compare:
    or al, al
    jz .st_found                    ; All chars matched
    ; Get char at logical offset CX
    push ax                         ; Save remaining count
    mov ax, cx
    call ed_get_char_at_offset      ; AL = char from text at offset CX
    cmp al, [di]                    ; Compare with search char
    pop ax                          ; Restore count (flags preserved across pop)
    jne .st_mismatch
    inc cx
    inc di
    dec al
    jmp .st_compare

.st_mismatch:
    pop dx
    inc dx
    jmp .st_outer

.st_found:
    pop dx
    mov ax, dx                      ; Return found offset
    clc
    pop di
    pop dx
    pop cx
    pop bx
    ret

.st_not_found:
    stc
    pop di
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; ed_get_char_at_offset — Get character at logical offset
; Input: AX = offset
; Output: AL = character
; -----------------------------------------------------------------------------
ed_get_char_at_offset:
    push bx
    mov bx, ax
    add bx, GAP_BUF_START
    ; If offset is past gap_start, adjust for gap
    cmp bx, [gap_start]
    jb .gcao_read
    ; Add gap size
    push cx
    mov cx, [gap_end]
    sub cx, [gap_start]
    add bx, cx
    pop cx
.gcao_read:
    mov al, [bx]
    pop bx
    ret

; -----------------------------------------------------------------------------
; ed_find_and_replace_one — Find next match and replace single occurrence
; -----------------------------------------------------------------------------
ed_find_and_replace_one:
    call ed_sync_gap_to_cursor
    call ed_get_cursor_offset
    inc ax                          ; Start after cursor
    call ed_search_text
    jc .fro_wrap

    ; Found — move and replace
    call ed_gap_move_to
    jmp .fro_do_replace

.fro_wrap:
    ; Try from beginning
    xor ax, ax
    call ed_search_text
    jc .fro_not_found
    call ed_gap_move_to

.fro_do_replace:
    ; Delete search_len characters
    movzx cx, byte [search_len]
.fro_del:
    or cx, cx
    jz .fro_ins
    call ed_gap_delete_fwd
    dec cx
    jmp .fro_del
.fro_ins:
    ; Insert replace string
    mov si, REPLACE_BUF_ADDR
    movzx cx, byte [replace_len]
.fro_ins_loop:
    or cx, cx
    jz .fro_done
    lodsb
    call ed_gap_insert
    dec cx
    jmp .fro_ins_loop
.fro_done:
    call ed_recalc_cursor_from_gap
    call ed_scroll_into_view
    call ed_draw_edit_area
    call ed_set_hw_cursor
    ret

.fro_not_found:
    call ed_msg_not_found
    ret

; -----------------------------------------------------------------------------
; ed_find_and_replace — Find/Replace loop (all occurrences)
; -----------------------------------------------------------------------------
ed_find_and_replace:
    ; Simple: find next occurrence, replace it
    call ed_get_cursor_offset
.far_loop:
    call ed_search_text
    jc .far_done                    ; No more matches

    ; Move to found position and replace
    push ax
    call ed_gap_move_to

    ; Delete search_len characters
    movzx cx, byte [search_len]
.far_del:
    or cx, cx
    jz .far_ins
    call ed_gap_delete_fwd
    dec cx
    jmp .far_del

.far_ins:
    ; Insert replace string
    mov si, REPLACE_BUF_ADDR
    movzx cx, byte [replace_len]
.far_ins_loop:
    or cx, cx
    jz .far_next
    lodsb
    call ed_gap_insert
    dec cx
    jmp .far_ins_loop

.far_next:
    pop ax
    ; Continue searching after replacement
    call ed_get_cursor_offset
    jmp .far_loop

.far_done:
    call ed_recalc_cursor_from_gap
    call ed_scroll_into_view
    call ed_draw_edit_area
    call ed_set_hw_cursor
    ret

