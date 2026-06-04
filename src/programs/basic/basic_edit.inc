; =============================================================================
; basic_edit.inc — REPL line entry + program line list operations.
;
; Public:
;   bas_readline      Reads a source line from the keyboard into bas_line_buf.
;                     Output: DS:SI = bas_line_buf, NUL-terminated.
;                             AX = length (≤ BAS_LINE_BUF_LEN-1).
;                     Supports backspace.  Always echoes input.  Terminates
;                     on Enter (CR).  Ignores keys with no ASCII value.
;
;   bas_handle_input_line
;                     Decides whether the line is a "program line" (starts
;                     with digits) or a "direct statement", and dispatches.
;
;   bas_prog_clear    Resets program area + line index to empty.
;
;   bas_prog_insert   Insert/replace a tokenized line into the program.
;                     In:  AX = line number (1..32767)
;                          DS:SI = tokenized payload (TOK_EOL-terminated)
;                          CX = payload length INCLUDING TOK_EOL
;                     Out: CF=0 ok, CF=1 → AL = BERR_PROGTOOBIG
;                     If a line with this number exists, replace it.
;                     If CX==1 (only TOK_EOL) → delete the line.
;
;   bas_prog_find     Locate a line by line number.
;                     In:  AX = line number
;                     Out: CF=0 → SI = byte offset within program area
;                          CF=1 → line not found
;
;   bas_prog_rebuild_index
;                     Rebuild bas_line_index from program area.
;
; The program area is a packed sequence of lines:
;     +0 word line_no
;     +2 byte payload_len  (TOK_EOL byte counts; length 1 = bare EOL)
;     +3 ...   payload bytes
; Lines are kept sorted by line_no.
; =============================================================================

; =============================================================================
; bas_readline — line editor
; =============================================================================
bas_readline:
    push bx
    push cx
    push di
    push es
    mov ax, ds
    mov es, ax
    mov di, bas_line_buf
    xor cx, cx                      ; CX = chars in buffer
.brl_loop:
    mov ah, SYS_READ_KEY
    int 0x80                        ; AH=scancode, AL=ASCII
    cmp al, 13
    je .brl_enter
    cmp al, 8                       ; Backspace
    je .brl_bs
    cmp al, 0
    je .brl_loop                    ; extended key — ignore
    cmp al, ' '
    jb .brl_loop                    ; other control — ignore
    cmp cx, BAS_LINE_BUF_LEN - 2
    jae .brl_loop                   ; buffer full — ignore
    mov [es:di], al
    inc di
    inc cx
    ; Echo
    push ax
    mov ah, SYS_PRINT_CHAR
    int 0x80
    pop ax
    jmp .brl_loop
.brl_bs:
    test cx, cx
    jz .brl_loop
    dec di
    dec cx
    ; Echo BS-space-BS
    mov al, 8
    mov ah, SYS_PRINT_CHAR
    int 0x80
    mov al, ' '
    mov ah, SYS_PRINT_CHAR
    int 0x80
    mov al, 8
    mov ah, SYS_PRINT_CHAR
    int 0x80
    jmp .brl_loop
.brl_enter:
    mov byte [es:di], 0
    ; Echo CRLF
    mov al, 13
    mov ah, SYS_PRINT_CHAR
    int 0x80
    mov al, 10
    mov ah, SYS_PRINT_CHAR
    int 0x80
    mov ax, cx
    mov si, bas_line_buf
    pop es
    pop di
    pop cx
    pop bx
    ret


; =============================================================================
; bas_handle_input_line — dispatch a freshly-read line.
;
; In: DS:SI = NUL-terminated source line (bas_line_buf).
;     AX    = length.
; Out: nothing.  Errors take the bas_error longjmp path.
; =============================================================================
bas_handle_input_line:
    push bx
    push cx
    push si
    push di
    push es

    ; Skip leading whitespace
.bhil_skipws:
    mov al, [si]
    cmp al, ' '
    je .bhil_eat
    cmp al, 9
    je .bhil_eat
    jmp .bhil_done_skip
.bhil_eat:
    inc si
    jmp .bhil_skipws
.bhil_done_skip:
    test al, al
    jz .bhil_ret                    ; empty line: just re-prompt
    cmp al, 13
    je .bhil_ret
    cmp al, 10
    je .bhil_ret

    cmp al, '0'
    jb .bhil_direct
    cmp al, '9'
    ja .bhil_direct

    ; --- Program line entry --------------------------------------------
    ; Parse leading line number
    xor bx, bx                      ; BX = line number accumulator
.bhil_lnum:
    mov al, [si]
    cmp al, '0'
    jb .bhil_lnum_done
    cmp al, '9'
    ja .bhil_lnum_done
    inc si
    sub al, '0'
    push ax
    mov ax, bx
    mov cx, 10
    mul cx
    test dx, dx
    jnz .bhil_badln
    cmp ax, BAS_MAX_LINENO
    ja .bhil_badln
    mov bx, ax
    pop ax
    xor ah, ah
    add bx, ax
    jc .bhil_badln
    cmp bx, BAS_MAX_LINENO
    ja .bhil_badln
    jmp .bhil_lnum
.bhil_lnum_done:
    test bx, bx
    jz .bhil_badln
    ; Skip whitespace after line number
.bhil_skipws2:
    mov al, [si]
    cmp al, ' '
    je .bhil_eat2
    cmp al, 9
    je .bhil_eat2
    jmp .bhil_after_lnum
.bhil_eat2:
    inc si
    jmp .bhil_skipws2
.bhil_after_lnum:
    ; If nothing follows → delete line BX
    test al, al
    jz .bhil_delete
    cmp al, 13
    je .bhil_delete
    cmp al, 10
    je .bhil_delete
    ; Tokenize rest of line into bas_token_buf
    mov ax, ds
    mov es, ax
    mov di, bas_token_buf
    push bx
    call bas_tokenize_line
    pop bx
    jc .bhil_lex_error
    ; CX = tokenized length (incl TOK_EOL)
    mov ax, bx                      ; AX = line number
    mov si, bas_token_buf
    call bas_prog_insert
    jc .bhil_insert_error
    jmp .bhil_ret

.bhil_delete:
    mov ax, bx
    mov si, bas_token_buf
    mov byte [si], TOK_EOL
    mov cx, 1
    call bas_prog_insert
    jmp .bhil_ret

.bhil_direct:
    ; --- Direct mode: tokenize, run statement ---------------------------
    mov ax, ds
    mov es, ax
    mov di, bas_token_buf
    call bas_tokenize_line
    jc .bhil_lex_error
    ; Execute the tokenized line via the statement dispatcher.
    ; DS:SI = bas_token_buf, CX = length, line_no = 0 (direct mode)
    mov si, bas_token_buf
    mov word [bas_cur_line], 0
    mov word [bas_cur_off], 0
    call bas_run_direct
    jmp .bhil_ret

.bhil_lex_error:
    call bas_error                  ; AL already set
    ; never returns
.bhil_insert_error:
    call bas_error
.bhil_badln:
    mov al, BERR_BADLINENUM
    call bas_error
.bhil_ret:
    pop es
    pop di
    pop si
    pop cx
    pop bx
    ret


; =============================================================================
; bas_prog_clear — empty program area, zero line index.
; =============================================================================
bas_prog_clear:
    push ax
    xor ax, ax
    mov [bas_prog_len], ax
    mov [bas_line_count], ax
    pop ax
    ret


; =============================================================================
; bas_prog_find — find line by number.
;
; In:  AX = line number
; Out: CF=0 → SI = byte offset within program area (relative to BAS_PROG_BASE)
;      CF=1 → not found
;
; Linear scan over program area (program is sorted).
; =============================================================================
bas_prog_find:
    push bx
    push cx
    push dx
    mov dx, ax                      ; DX = target line
    mov bx, BAS_PROG_BASE           ; current scan position
    mov cx, [bas_prog_len]
    test cx, cx
    jz .bpf_miss
    add cx, BAS_PROG_BASE           ; CX = end-of-prog absolute address
.bpf_loop:
    cmp bx, cx
    jae .bpf_miss
    mov ax, [bx + BLINE_OFF_LINENO]
    cmp ax, dx
    je .bpf_hit
    ja .bpf_miss                    ; sorted → past target
    mov al, [bx + BLINE_OFF_LEN]
    xor ah, ah
    add ax, BLINE_HDR_SIZE
    add bx, ax
    jmp .bpf_loop
.bpf_hit:
    mov si, bx
    sub si, BAS_PROG_BASE           ; SI = offset within prog area
    pop dx
    pop cx
    pop bx
    clc
    ret
.bpf_miss:
    pop dx
    pop cx
    pop bx
    stc
    ret


; =============================================================================
; bas_prog_insert — insert/replace/delete a tokenized line.
;
; In:  AX = line number
;      DS:SI = tokenized payload (TOK_EOL-terminated)
;      CX = payload length (≥ 1)
; Out: CF=0 ok; CF=1 → AL = BERR_*
;
; If a line with this number exists, it is replaced (possibly with delete
; if CX == 1 and only TOK_EOL).
; If no such line exists and CX > 1, insert at the sorted position.
; If no such line exists and CX == 1 (delete request), nothing to do.
;
; Uses BSS scratch slots (rather than stack juggling) to track state across
; the shift-and-write block:
;   bas_pi_lineno    word: line number
;   bas_pi_payload   word: source payload offset (DS-relative)
;   bas_pi_paylen    word: source payload length (incl TOK_EOL)
;   bas_pi_slot_addr word: absolute address of slot in prog area
;   bas_pi_old_size  word: old slot total size (0 if insertion)
;   bas_pi_new_size  word: new slot total size (0 if delete)
;   bas_pi_end_addr  word: end-of-prog absolute address (BASE + prog_len)
;   bas_pi_new_total word: new prog_len
;
; All these alias the existing scratch words bas_scratch_a..d plus a few
; reuse spots — see aliases just below.
; =============================================================================

bas_pi_lineno    equ bas_scratch_a
bas_pi_payload   equ bas_scratch_b
bas_pi_paylen    equ bas_scratch_c
bas_pi_slot_addr equ bas_scratch_d
bas_pi_old_size  equ bas_save_chan       ; reuse: not used during edit
bas_pi_new_size  equ bas_print_col       ; reuse: not used during edit
bas_pi_end_addr  equ bas_heap_seg        ; reuse (heap_seg unused for now)

bas_prog_insert:
    pushf
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cld

    mov [bas_pi_lineno], ax
    mov [bas_pi_payload], si
    mov [bas_pi_paylen], cx

    ; --- Find insertion / replacement position --------------------------
    mov dx, ax
    mov bx, BAS_PROG_BASE
    mov ax, [bas_prog_len]
    add ax, BAS_PROG_BASE
    mov [bas_pi_end_addr], ax
.bpi_scan:
    cmp bx, [bas_pi_end_addr]
    jae .bpi_at_end
    mov ax, [bx + BLINE_OFF_LINENO]
    cmp ax, dx
    je .bpi_found_existing
    ja .bpi_found_gap
    mov al, [bx + BLINE_OFF_LEN]
    xor ah, ah
    add ax, BLINE_HDR_SIZE
    add bx, ax
    jmp .bpi_scan

.bpi_at_end:
.bpi_found_gap:
    xor ax, ax
    mov [bas_pi_old_size], ax
    jmp .bpi_have_slot

.bpi_found_existing:
    mov al, [bx + BLINE_OFF_LEN]
    xor ah, ah
    add ax, BLINE_HDR_SIZE
    mov [bas_pi_old_size], ax

.bpi_have_slot:
    mov [bas_pi_slot_addr], bx

    ; --- Compute new slot size -----------------------------------------
    mov cx, [bas_pi_paylen]
    cmp cx, 1
    ja .bpi_not_delete
    xor ax, ax                      ; delete → new slot = 0
    jmp .bpi_have_new
.bpi_not_delete:
    mov ax, cx
    add ax, BLINE_HDR_SIZE
.bpi_have_new:
    mov [bas_pi_new_size], ax

    ; --- Fit check -----------------------------------------------------
    mov ax, [bas_prog_len]
    sub ax, [bas_pi_old_size]
    add ax, [bas_pi_new_size]
    cmp ax, BAS_PROG_LIMIT
    ja .bpi_toobig
    push ax                         ; save new_total for later

    ; --- Shift trailing bytes ------------------------------------------
    mov bx, [bas_pi_slot_addr]
    mov si, bx
    add si, [bas_pi_old_size]       ; src = slot + old
    mov di, bx
    add di, [bas_pi_new_size]       ; dst = slot + new
    mov cx, [bas_pi_end_addr]
    sub cx, bx
    sub cx, [bas_pi_old_size]       ; CX = bytes to shift
    mov ax, ds
    mov es, ax
    cmp di, si
    je .bpi_no_shift
    jb .bpi_shift_fwd
    ; Reverse copy (dst > src, overlapping)
    add si, cx
    dec si
    add di, cx
    dec di
    std
    rep movsb
    cld
    jmp .bpi_no_shift
.bpi_shift_fwd:
    rep movsb
.bpi_no_shift:

    ; --- Write new slot (if not delete) --------------------------------
    mov bx, [bas_pi_slot_addr]
    cmp word [bas_pi_new_size], 0
    je .bpi_no_write
    mov ax, [bas_pi_lineno]
    mov [bx + BLINE_OFF_LINENO], ax
    mov cx, [bas_pi_paylen]
    mov [bx + BLINE_OFF_LEN], cl
    mov si, [bas_pi_payload]
    lea di, [bx + BLINE_HDR_SIZE]
    mov ax, ds
    mov es, ax
    rep movsb
.bpi_no_write:

    ; --- Commit new program length -------------------------------------
    pop ax                          ; new_total
    mov [bas_prog_len], ax

    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    popf
    clc
    ret

.bpi_toobig:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    popf
    mov al, BERR_PROGTOOBIG
    stc
    ret


; =============================================================================
; bas_prog_first / bas_prog_next — iterate lines.
; bas_prog_first: out CF=0 → BX = absolute addr of first line; CF=1 if empty.
; bas_prog_next:  in  BX = current absolute line addr; out CF=0 → BX advanced.
; =============================================================================
bas_prog_first:
    mov bx, BAS_PROG_BASE
    cmp word [bas_prog_len], 0
    je .bpfi_empty
    clc
    ret
.bpfi_empty:
    stc
    ret

bas_prog_next:
    push ax
    mov al, [bx + BLINE_OFF_LEN]
    xor ah, ah
    add ax, BLINE_HDR_SIZE
    add bx, ax
    pop ax
    push ax
    mov ax, [bas_prog_len]
    add ax, BAS_PROG_BASE
    cmp bx, ax
    pop ax
    jae .bpn_end
    clc
    ret
.bpn_end:
    stc
    ret


; =============================================================================
; bas_prog_rebuild_index — rebuild the line index from the program area.
; Used by GOTO/GOSUB lookup paths.  Stored as (word line_no, word offset).
; =============================================================================
bas_prog_rebuild_index:
    push ax
    push bx
    push cx
    push si
    xor cx, cx                      ; line count
    mov si, bas_line_index
    call bas_prog_first
    jc .bpri_done
.bpri_loop:
    cmp cx, BAS_MAX_LINES
    jae .bpri_done
    mov ax, [bx + BLINE_OFF_LINENO]
    mov [si], ax
    mov ax, bx
    sub ax, BAS_PROG_BASE
    mov [si+2], ax
    add si, 4
    inc cx
    call bas_prog_next
    jnc .bpri_loop
.bpri_done:
    mov [bas_line_count], cx
    pop si
    pop cx
    pop bx
    pop ax
    ret
