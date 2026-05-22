; =============================================================================
; GAP BUFFER OPERATIONS
; =============================================================================

; -----------------------------------------------------------------------------
; ed_gap_insert — Insert a character at cursor position (gap_start)
; Input: AL = character to insert
; -----------------------------------------------------------------------------
ed_gap_insert:
    push bx
    mov bx, [gap_start]
    cmp bx, [gap_end]
    je .gap_full                    ; Buffer full
    mov [bx], al
    inc word [gap_start]
    mov byte [modified], 1
    ; Update line count if newline
    cmp al, 0x0A
    jne .gi_done
    inc word [total_lines]
.gi_done:
    pop bx
    ret
.gap_full:
    pop bx
    ret

; -----------------------------------------------------------------------------
; ed_gap_delete_back — Delete character before cursor (backspace)
; Returns: CF set if at start (nothing to delete)
; -----------------------------------------------------------------------------
ed_gap_delete_back:
    push bx
    mov bx, [gap_start]
    cmp bx, GAP_BUF_START
    je .gdb_fail                    ; At start of buffer
    dec bx
    mov al, [bx]                    ; Character being deleted
    mov [gap_start], bx
    mov byte [modified], 1
    cmp al, 0x0A
    jne .gdb_ok
    dec word [total_lines]
.gdb_ok:
    clc
    pop bx
    ret
.gdb_fail:
    stc
    pop bx
    ret

; -----------------------------------------------------------------------------
; ed_gap_delete_fwd — Delete character at cursor (Delete key)
; Returns: CF set if at end (nothing to delete)
; -----------------------------------------------------------------------------
ed_gap_delete_fwd:
    push bx
    mov bx, [gap_end]
    cmp bx, GAP_BUF_END + 1
    je .gdf_fail                    ; At end of buffer
    mov al, [bx]                    ; Character being deleted
    inc bx
    mov [gap_end], bx
    mov byte [modified], 1
    cmp al, 0x0A
    jne .gdf_ok
    dec word [total_lines]
.gdf_ok:
    clc
    pop bx
    ret
.gdf_fail:
    stc
    pop bx
    ret

; -----------------------------------------------------------------------------
; ed_gap_move_to — Move gap to logical offset AX
; Input: AX = target offset in the text (0-based)
; Trashes: BX, CX, SI, DI
; -----------------------------------------------------------------------------
ed_gap_move_to:
    push es
    push ax

    ; Calculate current logical position of gap
    ; gap position = gap_start - GAP_BUF_START
    mov bx, [gap_start]
    sub bx, GAP_BUF_START           ; BX = current offset

    cmp ax, bx
    je .gmt_done                    ; Already there
    ja .gmt_move_right              ; Target is after gap → move right

    ; Move left: copy chars from before gap to after gap
    ; (move gap_start backward, gap_end backward)
    sub bx, ax                      ; BX = chars to move left
    mov cx, bx

    mov si, [gap_start]
    dec si                          ; Source: char before gap
    mov di, [gap_end]
    dec di                          ; Dest: before gap_end

    ; Copy CX chars from right to left
    std                             ; Reverse direction
.gmt_left_loop:
    lodsb                           ; [SI] -> AL, SI--
    mov [di], al
    dec di
    loop .gmt_left_loop
    cld                             ; Restore direction

    ; Update pointers
    sub word [gap_start], bx
    sub word [gap_end], bx
    jmp .gmt_done

.gmt_move_right:
    ; Move right: copy chars from after gap to before gap
    sub ax, bx                      ; AX = chars to move right
    mov cx, ax
    push cx                         ; Save count (loop clobbers AL)

    mov si, [gap_end]               ; Source: first char after gap
    mov di, [gap_start]             ; Dest: gap_start

.gmt_right_loop:
    mov al, [si]
    mov [di], al
    inc si
    inc di
    loop .gmt_right_loop

    ; Update pointers
    pop ax                          ; Restore count
    add word [gap_start], ax
    add word [gap_end], ax

.gmt_done:
    pop ax
    pop es
    ret

; -----------------------------------------------------------------------------
; ed_get_text_length — Get total text length (excluding gap)
; Returns: AX = text length
; -----------------------------------------------------------------------------
ed_get_text_length:
    mov ax, [gap_start]
    sub ax, GAP_BUF_START           ; Text before gap
    push bx
    mov bx, GAP_BUF_END + 1
    sub bx, [gap_end]              ; Text after gap
    add ax, bx
    pop bx
    ret

; -----------------------------------------------------------------------------
; ed_get_cursor_offset — Get logical offset of cursor in text
; Returns: AX = offset
; -----------------------------------------------------------------------------
ed_get_cursor_offset:
    mov ax, [gap_start]
    sub ax, GAP_BUF_START
    ret

; -----------------------------------------------------------------------------
; ed_get_line_offset — Get buffer pointer to start of line AX
; Input:  AX = line number (0-based)
; Output: SI = pointer into buffer for reading (handles gap)
;         Sets [_line_ptr] for ed_gap_char_at_si to use
; Trashes: CX, DX
; -----------------------------------------------------------------------------
ed_get_line_offset:
    push bx
    ; Start at beginning of buffer
    mov si, GAP_BUF_START
    mov cx, ax                      ; Lines to skip
    or cx, cx
    jz .glo_done                    ; Line 0 = start

    ; Walk through text counting newlines
.glo_scan:
    call ed_gap_char_at_si          ; AL = next char, SI advanced
    or al, al
    jz .glo_done                    ; End of text
    cmp al, 0x0A
    jne .glo_scan
    loop .glo_scan                  ; Found newline, decrement line count

.glo_done:
    pop bx
    ret

; -----------------------------------------------------------------------------
; ed_gap_char_at_si — Read one character from gap buffer via sequential pointer
; Input:  SI = current read pointer
; Output: AL = character (0 if past end), SI advanced
; Note:   Transparently skips the gap region
; -----------------------------------------------------------------------------
ed_gap_char_at_si:
    ; Check if past end of buffer
    cmp si, GAP_BUF_END + 1
    jae .gcas_end

    ; Check if we've hit the gap — skip it
    cmp si, [gap_start]
    jb .gcas_read
    cmp si, [gap_end]
    jae .gcas_read
    ; SI is inside the gap — jump to gap_end
    mov si, [gap_end]
    cmp si, GAP_BUF_END + 1
    jae .gcas_end

.gcas_read:
    mov al, [si]
    inc si
    ; If SI now enters the gap, advance past it
    cmp si, [gap_start]
    jne .gcas_ret
    cmp si, [gap_end]
    jae .gcas_ret
    mov si, [gap_end]
.gcas_ret:
    ret

.gcas_end:
    xor al, al
    ret

