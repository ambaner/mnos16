; =============================================================================
; basic_err.inc — Central error handler + ERR/ERL state.
;
; Every routine that can fail returns CF=1 and AL = BERR_* on error.
; The statement dispatcher (and direct-mode evaluator) checks CF after each
; helper call; on error it tail-calls bas_error.
;
; bas_error never returns.  It:
;   1. Sets bas_err = code, bas_erl = current line (0 in direct mode).
;   2. Frees the temp-string pool (best effort).
;   3. Zeros the value/operator/GOSUB/FOR/WHILE stacks.
;   4. Closes all open channels (modes I/O/A) freeing buffers WITHOUT writing
;      output buffers (documented data-loss policy).
;   5. Resets DEF SEG to the BASIC program's own data segment.
;   6. Prints "?<Name> Error[ in <line>]" + CRLF.
;   7. Sets bas_in_run = 0.
;   8. Long-jumps to the saved REPL SP and bas_repl_resume label.
;
; Save / restore convention:
;   At REPL prompt prologue, basic.asm does:
;       mov [bas_repl_sp], sp
;     immediately before calling into anything that can fault.
;   Long-jump trampoline restores SS:SP from BSS scalar and jumps to
;   bas_repl_resume (label defined in basic.asm).
; =============================================================================

; ----------- Error name table (lookup by code 1..BERR_MAX) ------------------
; Each entry = NUL-terminated short name.  Printed as `?<Name> Error`.

bas_err_table:
    dw bas_err_unk      ; 0 unused
    dw bas_err_syn      ; 1
    dw bas_err_undl     ; 2
    dw bas_err_oom      ; 3
    dw bas_err_type     ; 4
    dw bas_err_div0     ; 5
    dw bas_err_ovf      ; 6
    dw bas_err_badf     ; 7
    dw bas_err_retw     ; 8
    dw bas_err_nextw    ; 9
    dw bas_err_wendw    ; 10
    dw bas_err_complex  ; 11
    dw bas_err_tmvars   ; 12
    dw bas_err_progbig  ; 13
    dw bas_err_strlong  ; 14
    dw bas_err_subscr   ; 15
    dw bas_err_dupdef   ; 16
    dw bas_err_undim    ; 17
    dw bas_err_gsover   ; 18
    dw bas_err_forover  ; 19
    dw bas_err_whover   ; 20
    dw bas_err_inperr   ; 21
    dw bas_err_inppast  ; 22
    dw bas_err_badch    ; 23
    dw bas_err_aopen    ; 24
    dw bas_err_notopen  ; 25
    dw bas_err_tmfiles  ; 26
    dw bas_err_fnfound  ; 27
    dw bas_err_dskfull  ; 28
    dw bas_err_dskio    ; 29
    dw bas_err_fexists  ; 30
    dw bas_err_break    ; 31
    dw bas_err_linelong ; 32
    dw bas_err_badln    ; 33
    dw bas_err_noprog   ; 34
    dw bas_err_intern   ; 35
    dw bas_err_outdata  ; 36

bas_err_unk:      db 'Internal',0
bas_err_syn:      db 'Syntax',0
bas_err_undl:     db 'Undefined line',0
bas_err_oom:      db 'Out of memory',0
bas_err_type:     db 'Type mismatch',0
bas_err_div0:     db 'Division by zero',0
bas_err_ovf:      db 'Overflow',0
bas_err_badf:     db 'Illegal function call',0
bas_err_retw:     db 'RETURN without GOSUB',0
bas_err_nextw:    db 'NEXT without FOR',0
bas_err_wendw:    db 'WEND without WHILE',0
bas_err_complex:  db 'Expression too complex',0
bas_err_tmvars:   db 'Too many variables',0
bas_err_progbig:  db 'Program too big',0
bas_err_strlong:  db 'String too long',0
bas_err_subscr:   db 'Subscript out of range',0
bas_err_dupdef:   db 'Duplicate definition',0
bas_err_undim:    db 'Undimensioned array',0
bas_err_gsover:   db 'GOSUB stack overflow',0
bas_err_forover:  db 'FOR stack overflow',0
bas_err_whover:   db 'WHILE stack overflow',0
bas_err_inperr:   db 'Input',0
bas_err_inppast:  db 'Input past end',0
bas_err_badch:    db 'Bad file number',0
bas_err_aopen:    db 'File already open',0
bas_err_notopen:  db 'File not open',0
bas_err_tmfiles:  db 'Too many files',0
bas_err_fnfound:  db 'File not found',0
bas_err_dskfull:  db 'Disk full',0
bas_err_dskio:    db 'Disk I/O',0
bas_err_fexists:  db 'File exists',0
bas_err_break:    db 'Break',0
bas_err_linelong: db 'Line too long',0
bas_err_badln:    db 'Bad line number',0
bas_err_noprog:   db 'No program',0
bas_err_intern:   db 'Internal',0
bas_err_outdata:  db 'Out of DATA',0


; =============================================================================
; bas_error — central error path; NEVER RETURNS.
;
; In: AL = BERR_* code
; @noreturn  — never returns to caller; longjmps via bas_err_recover_sp
; =============================================================================
bas_error:
    push ax                         ; save error code on stack temporarily
    ; --- 1. Save ERR / ERL ----------------------------------------------
    xor ah, ah
    mov [bas_err], ax
    mov ax, 0
    cmp byte [bas_in_run], 0
    je .be_no_line
    ; Recover line number from bas_cur_line (= offset into prog area)
    mov bx, [bas_cur_line]
    test bx, bx
    jz .be_no_line
    ; bx is offset within prog area; line_no is at offset 0
    mov ax, [BAS_PROG_BASE + bx + BLINE_OFF_LINENO]
.be_no_line:
    mov [bas_erl], ax

    ; --- 2. Free temp string pool --------------------------------------
    call bas_temp_pool_free_all
    ; Clear DEF FN recursion guard so a future call works after error.
    mov byte [bas_userfn_depth], 0

    ; --- 3. Reset stacks ------------------------------------------------
    xor ax, ax
    mov [bas_gs_top], ax
    mov [bas_for_top], ax
    mov [bas_wh_top], ax
    mov [bas_ex_top], ax
    mov [bas_op_top], ax

    ; --- 4. Close all channels (discard) -------------------------------
    call bas_channels_close_all_discard

    ; --- 5. Reset DEF SEG to our own data segment ----------------------
    mov ax, ds
    mov [bas_def_seg], ax

    ; --- 6. Print "?<Name> Error[ in <line>]" --------------------------
    mov al, '?'
    call bas_lex_putc
    pop ax                          ; AL = error code
    push ax
    xor ah, ah
    cmp al, BERR_MAX
    jbe .be_code_ok
    mov al, 0                       ; clamp to "Internal"
.be_code_ok:
    mov bx, ax
    shl bx, 1
    mov si, [bas_err_table + bx]
    call bas_puts_nul
    mov si, bas_err_msg_suffix
    call bas_puts_nul
    ; Append " in <line>" if running
    cmp byte [bas_in_run], 0
    je .be_no_in
    mov ax, [bas_erl]
    test ax, ax
    jz .be_no_in
    mov si, bas_err_msg_in
    call bas_puts_nul
    mov dx, ax
    call bas_lex_print_unsigned
.be_no_in:
    call bas_crlf
    pop ax                          ; pop saved error code (discard)

    ; --- 7. Clear run flag, reset REM/lex state ------------------------
    mov byte [bas_in_run], 0
    mov byte [bas_lex_mode], 0
    mov byte [bas_lex_in_rem], 0

    ; --- 8. Longjmp back to REPL --------------------------------------
    mov sp, [bas_repl_sp]
    jmp bas_repl_resume

bas_err_msg_suffix: db ' Error',0
bas_err_msg_in:     db ' in ',0


; =============================================================================
; bas_puts_nul — print DS:SI NUL-terminated string via SYS_PRINT_CHAR loop.
; (We don't use SYS_PRINT_STRING here because it expects a different format.)
; =============================================================================
bas_puts_nul:
    push ax
    push si
.bps_loop:
    mov al, [si]
    test al, al
    jz .bps_done
    inc si
    call bas_lex_putc
    jmp .bps_loop
.bps_done:
    pop si
    pop ax
    ret

bas_crlf:
    push ax
    mov al, 13
    call bas_lex_putc
    mov al, 10
    call bas_lex_putc
    pop ax
    ret


; =============================================================================
; bas_temp_pool_free_all — walk the temp string pool and free every entry
; via MEM_FREE.  Then zero bas_temp_top.  Best effort; ignores MEM_FREE
; errors (which can only happen with a corrupted block).
;
; Each pool slot is 4 bytes: word seg, word off.
; =============================================================================
bas_temp_pool_free_all:
    push ax
    push bx
    push cx
    push si
    push es
    mov cx, [bas_temp_top]
    test cx, cx
    jz .btpa_done
    mov si, bas_temp_pool
.btpa_loop:
    mov ax, [si]                    ; seg
    mov bx, [si+2]                  ; off
    mov es, ax
    mov dl, 1                       ; owner ID 1
    call mn_free
    add si, 4
    loop .btpa_loop
.btpa_done:
    xor ax, ax
    mov [bas_temp_top], ax
    pop es
    pop si
    pop cx
    pop bx
    pop ax
    ret


; =============================================================================
; bas_channels_close_all_discard — free buffers of all open channels.
; For modes O/A this DISCARDS unwritten data (no flush).  Marks each
; channel BCHAN_CLOSED.
; =============================================================================
bas_channels_close_all_discard:
    push ax
    push bx
    push cx
    push si
    push es
    mov cx, BAS_MAX_CHANNELS
    mov si, bas_channels
.bccd_loop:
    cmp byte [si + BCH_MODE], BCHAN_CLOSED
    je .bccd_skip
    ; Free buffer
    mov ax, [si + BCH_BUF_SEG]
    test ax, ax
    jz .bccd_zap
    mov bx, [si + BCH_BUF_OFF]
    mov es, ax
    mov dl, 1
    call mn_free
.bccd_zap:
    mov byte [si + BCH_MODE], BCHAN_CLOSED
    xor ax, ax
    mov [si + BCH_BUF_SEG], ax
    mov [si + BCH_BUF_OFF], ax
    mov [si + BCH_LEN], ax
    mov [si + BCH_POS], ax
    mov [si + BCH_CAP], ax
.bccd_skip:
    add si, BAS_CHAN_REC_SIZE
    loop .bccd_loop
    pop es
    pop si
    pop cx
    pop bx
    pop ax
    ret
