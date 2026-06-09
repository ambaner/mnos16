; =============================================================================
; basic_io.inc — File I/O: OPEN/CLOSE/PRINT#/INPUT#/EOF on channels #1..#4.
;
; Channel record (32 B at bas_channels + n*32):
;   BCH_MODE     byte  BCHAN_CLOSED / 'I' / 'O' / 'A'
;   BCH_NAME     11    MNFS 8.3 name (kept for flush-on-CLOSE)
;   BCH_BUF_SEG  word  HMA segment of buffer
;   BCH_BUF_OFF  word  HMA offset
;   BCH_LEN      word  bytes in buffer
;   BCH_POS      word  cursor (0..LEN)
;   BCH_CAP      word  buffer capacity
;
; Buffer = 4 KB HMA, mn_alloc'd at OPEN, mn_free'd at CLOSE.
; OUTPUT/APPEND flush via mn_replace_file at CLOSE (staged through bas_load_buf).
; =============================================================================
%ifndef BASIC_IO_INC
%define BASIC_IO_INC

BAS_CHAN_BUF_CAP   equ 4096

; ---------------------------------------------------------------------------
; bas_chan_rec — channel# (1..MAX) in AX → BX = record ptr.
; ---------------------------------------------------------------------------
bas_chan_rec:
    test ax, ax
    jle .bcr_bad
    cmp ax, BAS_MAX_CHANNELS
    jg .bcr_bad
    dec ax
    mov bx, ax
    shl bx, 5
    add bx, bas_channels
    clc
    ret
.bcr_bad:
    mov al, BERR_BADCHAN
    stc
    ret

; ---------------------------------------------------------------------------
; bas_str_to_fname — copy bas_expr_result string into bas_line_buf, then
; normalize into bas_fname_buf.  CF=1 on syntactic problems.
; @returns cf
; ---------------------------------------------------------------------------
bas_str_to_fname:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    push ds
    mov cx, [bas_expr_result + BES_LEN]
    test cx, cx
    jz .bstf_bad
    cmp cx, 12
    ja .bstf_bad
    push ds
    pop es
    mov di, bas_line_buf
    mov ax, [bas_expr_result + BES_VAL_LO]
    mov bx, [bas_expr_result + BES_VAL_HI]
    mov ds, ax
    mov si, bx
    push cx
    cld
    rep movsb
    pop cx
    pop ds
    push ds
    mov si, bas_line_buf
    call bas_fname_normalize
    pop ds
    test ax, ax
    jz .bstf_bad2
    ; Success path — restore the 6 saved regs in reverse push order.
    ; (DS was already restored at line above via the matching pop ds.
    ;  An extra pop here was bug #5 — see CHANGELOG v0.9.19.0.)
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    BAS_RET_OK
.bstf_bad:
    pop ds
.bstf_bad2:
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    BAS_RET_ERR BERR_FILENOTFOUND

; ---------------------------------------------------------------------------
; bas_chan_alloc — alloc 4 KB HMA buffer for channel.
;   In:  BX = chan rec ptr
;   Out: BUF_SEG/OFF/CAP set; LEN=POS=0;  CF=1 → AL=BERR_OOM
; ---------------------------------------------------------------------------
bas_chan_alloc:
    push cx
    push dx
    push di
    push bp
    mov bp, sp
    sub sp, 2
    mov [bp-2], bx
    ; --- Permanent diag: dump heap state before the 4 KB channel alloc.    ---
    ; mn_dbg_hex16 is a no-op at the kernel level in release builds; under
    ; KERNELD it prints to COM1 so we can quickly diagnose channel-OOM bugs.
    push ax
    push bx
    push cx
    push dx
    call mn_avail                           ; AX=largest free, DX=total free
    push dx                                 ; save total
    mov dx, ax                              ; print largest
    mov bx, bca_dbg_largest
    call mn_dbg_hex16
    pop dx                                  ; restore total
    mov bx, bca_dbg_total
    call mn_dbg_hex16
    mov dx, BAS_CHAN_BUF_CAP
    mov bx, bca_dbg_want
    call mn_dbg_hex16
    pop dx
    pop cx
    pop bx
    pop ax
    ; --- /diag -------------------------------------------------------------
    mov cx, BAS_CHAN_BUF_CAP
    xor dx, dx
    call mn_alloc                       ; AX=seg, BX=off (BX clobbered)
    jc .bca_fail
    mov di, bx
    mov bx, [bp-2]
    mov [bx + BCH_BUF_SEG], ax
    mov [bx + BCH_BUF_OFF], di
    mov word [bx + BCH_CAP], BAS_CHAN_BUF_CAP
    mov word [bx + BCH_LEN], 0
    mov word [bx + BCH_POS], 0
    mov sp, bp
    pop bp
    pop di
    pop dx
    pop cx
    clc
    ret
.bca_fail:
    mov sp, bp
    pop bp
    pop di
    pop dx
    pop cx
    mov al, BERR_OOM
    stc
    ret
bca_dbg_largest: db 'BAS:diag.chan-largest ',0
bca_dbg_total:   db 'BAS:diag.chan-total   ',0
bca_dbg_want:    db 'BAS:diag.chan-want    ',0

; ---------------------------------------------------------------------------
; bas_chan_free — free buffer and mark closed.
;   In:  BX = chan rec ptr
; ---------------------------------------------------------------------------
bas_chan_free:
    push ax
    push bx
    push di
    mov di, bx
    mov ax, [di + BCH_BUF_SEG]
    test ax, ax
    jz .bcf_clear
    mov bx, [di + BCH_BUF_OFF]
    call mn_free
.bcf_clear:
    mov bx, di
    mov byte [bx + BCH_MODE], BCHAN_CLOSED
    xor ax, ax
    mov [bx + BCH_BUF_SEG], ax
    mov [bx + BCH_BUF_OFF], ax
    mov [bx + BCH_LEN], ax
    mov [bx + BCH_POS], ax
    mov [bx + BCH_CAP], ax
    pop di
    pop bx
    pop ax
    ret

; ---------------------------------------------------------------------------
; bas_chan_load — read file (bas_fname_buf) into channel buffer.
;   In:  BX = chan rec ptr (buffer already allocated)
;   Out: BCH_LEN = bytes read; CF=1 → AL=BERR_*
; ---------------------------------------------------------------------------
bas_chan_load:
    push ax
    push cx
    push dx
    push si
    push di
    push es
    push bp
    mov bp, sp
    sub sp, 2
    mov [bp-2], bx
    ; Read into bas_load_buf (conventional staging)
    push ds
    pop es
    mov bx, bas_load_buf
    mov si, bas_fname_buf
    mov cx, (BAS_LOAD_BUF_LEN / 512)
    call mn_read_file
    jc .bcl_notfound
    mov cx, ax                          ; CX = byte count
    cmp cx, BAS_CHAN_BUF_CAP
    ja .bcl_toobig
    ; Copy bas_load_buf → chan buf
    mov bx, [bp-2]
    mov [bx + BCH_LEN], cx
    mov word [bx + BCH_POS], 0
    test cx, cx
    jz .bcl_done
    mov ax, [bx + BCH_BUF_SEG]
    mov di, [bx + BCH_BUF_OFF]
    mov si, bas_load_buf
    push es
    mov es, ax
    cld
    rep movsb
    pop es
.bcl_done:
    mov sp, bp
    pop bp
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    clc
    ret
.bcl_notfound:
    mov sp, bp
    pop bp
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    mov al, BERR_FILENOTFOUND
    stc
    ret
.bcl_toobig:
    mov sp, bp
    pop bp
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    mov al, BERR_DISKFULL
    stc
    ret

; ---------------------------------------------------------------------------
; bas_chan_flush — flush OUTPUT/APPEND channel to disk via REPLACE.
;   In:  BX = chan rec ptr
; ---------------------------------------------------------------------------
bas_chan_flush:
    push ax
    push cx
    push dx
    push si
    push di
    push es
    push bp
    mov bp, sp
    sub sp, 4
    mov [bp-2], bx                      ; save chan ptr
    mov cx, [bx + BCH_LEN]
    mov [bp-4], cx                      ; save byte count
    cmp cx, BAS_LOAD_BUF_LEN
    ja .bcfl_toobig
    test cx, cx
    jz .bcfl_no_copy
    mov ax, [bx + BCH_BUF_SEG]
    mov si, [bx + BCH_BUF_OFF]
    push ds
    push ds
    pop es
    mov di, bas_load_buf
    mov ds, ax
    cld
    rep movsb
    pop ds
.bcfl_no_copy:
    ; ES:BX = bas_load_buf, DS:SI = chan name, ECX = bytes
    mov bx, [bp-2]
    mov si, bx
    add si, BCH_NAME
    push ds
    pop es
    mov bx, bas_load_buf
    mov cx, [bp-4]
    movzx ecx, cx
    xor dl, dl
    call mn_replace_file
    jc .bcfl_io
    mov sp, bp
    pop bp
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    clc
    ret
.bcfl_toobig:
    mov sp, bp
    pop bp
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    mov al, BERR_DISKFULL
    stc
    ret
.bcfl_io:
    mov sp, bp
    pop bp
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    mov al, BERR_DISKIO
    stc
    ret

; ---------------------------------------------------------------------------
; bas_chan_putbyte — append byte AL to channel buffer.  CF=1 → BERR_DISKFULL
;   In:  AL = byte, BX = chan rec ptr
; ---------------------------------------------------------------------------
bas_chan_putbyte:
    push cx
    push dx
    push si
    push es
    mov cx, [bx + BCH_LEN]
    cmp cx, [bx + BCH_CAP]
    jae .bcpb_full
    mov dx, [bx + BCH_BUF_SEG]
    mov si, [bx + BCH_BUF_OFF]
    add si, cx
    mov es, dx
    mov [es:si], al
    inc cx
    mov [bx + BCH_LEN], cx
    mov [bx + BCH_POS], cx
    pop es
    pop si
    pop dx
    pop cx
    clc
    ret
.bcpb_full:
    pop es
    pop si
    pop dx
    pop cx
    mov al, BERR_DISKFULL
    stc
    ret

; ---------------------------------------------------------------------------
; bas_chan_getbyte — read byte at POS, advance POS.  AL=byte, CF=1 if at EOF.
;   In:  BX = chan rec ptr
; ---------------------------------------------------------------------------
bas_chan_getbyte:
    push cx
    push dx
    push si
    push es
    mov cx, [bx + BCH_POS]
    cmp cx, [bx + BCH_LEN]
    jae .bcgb_eof
    mov dx, [bx + BCH_BUF_SEG]
    mov si, [bx + BCH_BUF_OFF]
    add si, cx
    mov es, dx
    mov al, [es:si]
    inc cx
    mov [bx + BCH_POS], cx
    pop es
    pop si
    pop dx
    pop cx
    clc
    ret
.bcgb_eof:
    pop es
    pop si
    pop dx
    pop cx
    stc
    ret

; ---------------------------------------------------------------------------
; bas_chan_putstr — write all bytes of bas_expr_result string to channel BX.
; ---------------------------------------------------------------------------
bas_chan_putstr:
    push ax
    push cx
    push si
    push di
    push es
    mov cx, [bas_expr_result + BES_LEN]
    test cx, cx
    jz .bcps_done
    mov ax, [bas_expr_result + BES_VAL_LO]
    mov si, [bas_expr_result + BES_VAL_HI]
    mov es, ax
.bcps_loop:
    mov al, [es:si]
    push si
    push es
    call bas_chan_putbyte
    pop es
    pop si
    jc .bcps_fail
    inc si
    loop .bcps_loop
.bcps_done:
    pop es
    pop di
    pop si
    pop cx
    pop ax
    clc
    ret
.bcps_fail:
    pop es
    pop di
    pop si
    pop cx
    pop ax
    stc
    ret

; ---------------------------------------------------------------------------
; bas_chan_print_int — write decimal of DX to channel BX.
; ---------------------------------------------------------------------------
bas_chan_print_int:
    push ax
    push bx
    push cx
    push dx
    push di
    push bp
    mov bp, sp
    mov di, bx                          ; preserve chan ptr in DI
    test dx, dx
    jns .bcpi_pos
    mov al, '-'
    mov bx, di
    call bas_chan_putbyte
    jc .bcpi_fail
    neg dx
.bcpi_pos:
    mov ax, dx
    xor cx, cx
    mov bx, 10
.bcpi_div:
    xor dx, dx
    div bx
    push dx
    inc cx
    test ax, ax
    jnz .bcpi_div
.bcpi_emit:
    pop ax
    add al, '0'
    push cx
    mov bx, di
    call bas_chan_putbyte
    pop cx
    jc .bcpi_fail
    loop .bcpi_emit
    mov sp, bp
    pop bp
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret
.bcpi_fail:
    mov sp, bp
    pop bp
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret

; ---------------------------------------------------------------------------
; bas_chan_readline — read until LF/CR/EOF into bas_line_buf, NUL-terminated.
;   In:  BX = chan rec ptr
;   Out: CX = bytes read (NOT counting NUL); CF=1 if no bytes AND already EOF
; ---------------------------------------------------------------------------
bas_chan_readline:
    push ax
    push si
    push di
    mov di, bx                          ; chan ptr
    mov si, bas_line_buf
    xor cx, cx
.bcrl_loop:
    mov bx, di
    call bas_chan_getbyte
    jc .bcrl_eof
    cmp al, 13
    je .bcrl_loop
    cmp al, 10
    je .bcrl_done
    cmp cx, 254
    jae .bcrl_done
    mov [si], al
    inc si
    inc cx
    jmp .bcrl_loop
.bcrl_eof:
    test cx, cx
    jz .bcrl_eof_empty
.bcrl_done:
    mov byte [si], 0
    pop di
    pop si
    pop ax
    clc
    ret
.bcrl_eof_empty:
    mov byte [si], 0
    pop di
    pop si
    pop ax
    stc
    ret

; ---------------------------------------------------------------------------
; bas_chan_eof — EOF(channel) → DX = -1 if at end, 0 otherwise.
;   In:  AX = channel#
;   Out: DX = result; CF=1 → AL = BERR_*
; ---------------------------------------------------------------------------
bas_chan_eof:
    call bas_chan_rec
    jc .bce_fail
    cmp byte [bx + BCH_MODE], BCHAN_CLOSED
    je .bce_notopen
    mov dx, [bx + BCH_POS]
    cmp dx, [bx + BCH_LEN]
    jb .bce_more
    mov dx, -1
    clc
    ret
.bce_more:
    xor dx, dx
    clc
    ret
.bce_notopen:
    mov al, BERR_NOTOPEN
    stc
    ret
.bce_fail:
    stc
    ret

; ---------------------------------------------------------------------------
; bas_stmt_open — OPEN strexpr FOR (INPUT|OUTPUT|APPEND) AS [#] N
; ---------------------------------------------------------------------------
bas_stmt_open:
    push bx
    push cx
    push dx
    push bp
    mov bp, sp
    sub sp, 4
    ; [bp-2] = mode byte
    ; [bp-4] = saved fname (not strictly needed; we keep it in bas_fname_buf)
    call bas_expr_eval
    jc .bso_propagate
    cmp byte [bas_expr_result + BES_TYPE], BVAR_STRING
    jne .bso_type
    call bas_str_to_fname
    jc .bso_propagate
    mov al, [si]
    cmp al, TOK_FOR
    jne .bso_syn
    inc si
    mov al, [si]
    inc si
    cmp al, TOK_INPUT
    je .bso_mode_in
    cmp al, TOK_OUTPUT
    je .bso_mode_out
    cmp al, TOK_APPEND
    je .bso_mode_app
    jmp .bso_syn
.bso_mode_in:
    mov word [bp-2], BCHAN_INPUT
    jmp .bso_after_mode
.bso_mode_out:
    mov word [bp-2], BCHAN_OUTPUT
    jmp .bso_after_mode
.bso_mode_app:
    mov word [bp-2], BCHAN_APPEND
.bso_after_mode:
    mov al, [si]
    cmp al, TOK_AS
    jne .bso_syn
    inc si
    ; optional '#'
    cmp byte [si], '#'
    jne .bso_no_hash
    inc si
.bso_no_hash:
    call bas_expr_eval_int              ; DX = channel#
    jc .bso_propagate
    mov ax, dx
    call bas_chan_rec                   ; BX = chan ptr
    jc .bso_propagate
    cmp byte [bx + BCH_MODE], BCHAN_CLOSED
    jne .bso_already
    ; Store name into chan
    push si
    push di
    push es
    push ds
    pop es
    mov di, bx
    add di, BCH_NAME
    mov si, bas_fname_buf
    mov cx, 11
    cld
    rep movsb
    pop es
    pop di
    pop si
    ; Mode byte
    mov ax, [bp-2]
    mov [bx + BCH_MODE], al
    ; Allocate buffer
    call bas_chan_alloc
    jc .bso_alloc_fail
    ; INPUT/APPEND: read existing file into buffer
    cmp byte [bx + BCH_MODE], BCHAN_OUTPUT
    je .bso_done
    call bas_chan_load
    jc .bso_load_fail
    ; APPEND: position at end
    cmp byte [bx + BCH_MODE], BCHAN_APPEND
    jne .bso_done
    mov ax, [bx + BCH_LEN]
    mov [bx + BCH_POS], ax
.bso_done:
    mov sp, bp
    pop bp
    pop dx
    pop cx
    pop bx
    clc
    ret
.bso_load_fail:
    ; If APPEND and file not found, treat as empty
    cmp byte [bx + BCH_MODE], BCHAN_APPEND
    jne .bso_close_and_fail
    cmp al, BERR_FILENOTFOUND
    jne .bso_close_and_fail
    mov word [bx + BCH_LEN], 0
    mov word [bx + BCH_POS], 0
    mov sp, bp
    pop bp
    pop dx
    pop cx
    pop bx
    clc
    ret
.bso_close_and_fail:
    push ax
    call bas_chan_free
    pop ax
    mov sp, bp
    pop bp
    pop dx
    pop cx
    pop bx
    stc
    ret
.bso_alloc_fail:
    mov sp, bp
    pop bp
    pop dx
    pop cx
    pop bx
    stc
    ret
.bso_already:
    mov sp, bp
    pop bp
    pop dx
    pop cx
    pop bx
    mov al, BERR_ALREADYOPEN
    stc
    ret
.bso_type:
    mov sp, bp
    pop bp
    pop dx
    pop cx
    pop bx
    mov al, BERR_TYPE
    stc
    ret
.bso_syn:
    mov sp, bp
    pop bp
    pop dx
    pop cx
    pop bx
    mov al, BERR_SYNTAX
    stc
    ret
.bso_propagate:
    mov sp, bp
    pop bp
    pop dx
    pop cx
    pop bx
    stc
    ret

; ---------------------------------------------------------------------------
; bas_stmt_close — CLOSE [[#] N [, [#] N]*]   (bare CLOSE closes all)
; ---------------------------------------------------------------------------
bas_stmt_close:
    push bx
    push cx
    push dx
    ; Bare CLOSE?  Peek next token.
    mov al, [si]
    cmp al, TOK_EOL
    je .bsc_close_all
    cmp al, ':'
    je .bsc_close_all
.bsc_one:
    ; optional '#'
    cmp byte [si], '#'
    jne .bsc_no_hash
    inc si
.bsc_no_hash:
    call bas_expr_eval_int              ; DX = channel#
    jc .bsc_propagate
    mov ax, dx
    call bas_chan_rec
    jc .bsc_propagate
    cmp byte [bx + BCH_MODE], BCHAN_CLOSED
    je .bsc_next                        ; CLOSE of already-closed is no-op
    cmp byte [bx + BCH_MODE], BCHAN_INPUT
    je .bsc_just_free
    ; OUTPUT or APPEND: flush
    call bas_chan_flush
    jc .bsc_propagate
.bsc_just_free:
    call bas_chan_free
.bsc_next:
    cmp byte [si], ','
    jne .bsc_done
    inc si
    jmp .bsc_one
.bsc_close_all:
    mov cx, BAS_MAX_CHANNELS
    mov bx, bas_channels
.bsc_all_loop:
    cmp byte [bx + BCH_MODE], BCHAN_CLOSED
    je .bsc_all_skip
    cmp byte [bx + BCH_MODE], BCHAN_INPUT
    je .bsc_all_free
    push bx
    push cx
    call bas_chan_flush
    pop cx
    pop bx
    ; Ignore flush failures on bulk close (best effort)
.bsc_all_free:
    push bx
    push cx
    call bas_chan_free
    pop cx
    pop bx
.bsc_all_skip:
    add bx, 32
    loop .bsc_all_loop
.bsc_done:
    pop dx
    pop cx
    pop bx
    clc
    ret
.bsc_propagate:
    pop dx
    pop cx
    pop bx
    stc
    ret

; ---------------------------------------------------------------------------
; bas_stmt_print_hash — PRINT #N, expr_list
;   Entry: SI just past TOK_PRINT, points at '#'
; ---------------------------------------------------------------------------
bas_stmt_print_hash:
    push bx
    push cx
    push dx
    push bp
    mov bp, sp
    sub sp, 2
    ; [bp-2] = chan ptr
    inc si                              ; past '#'
    call bas_expr_eval_int
    jc .bsph_propagate
    mov ax, dx
    call bas_chan_rec
    jc .bsph_propagate
    cmp byte [bx + BCH_MODE], BCHAN_OUTPUT
    je .bsph_mode_ok
    cmp byte [bx + BCH_MODE], BCHAN_APPEND
    je .bsph_mode_ok
    jmp .bsph_notopen
.bsph_mode_ok:
    mov [bp-2], bx
    ; Expect comma
    cmp byte [si], ','
    jne .bsph_syn
    inc si
.bsph_item:
    cmp byte [si], TOK_EOL
    je .bsph_eol
    cmp byte [si], ':'
    je .bsph_eol
    call bas_expr_eval
    jc .bsph_propagate
    mov bx, [bp-2]
    cmp byte [bas_expr_result + BES_TYPE], BVAR_STRING
    je .bsph_emit_str
    mov dx, [bas_expr_result + BES_VAL_LO]
    call bas_chan_print_int
    jc .bsph_propagate
    jmp .bsph_after_item
.bsph_emit_str:
    call bas_chan_putstr
    jc .bsph_propagate
.bsph_after_item:
    cmp byte [si], ','
    je .bsph_comma
    cmp byte [si], ';'
    je .bsph_semi
    jmp .bsph_eol
.bsph_comma:
    inc si
    mov al, ','
    mov bx, [bp-2]
    call bas_chan_putbyte
    jc .bsph_propagate
    jmp .bsph_item
.bsph_semi:
    inc si
    jmp .bsph_item
.bsph_eol:
    ; Emit CRLF terminator
    mov bx, [bp-2]
    mov al, 13
    call bas_chan_putbyte
    jc .bsph_propagate
    mov al, 10
    mov bx, [bp-2]
    call bas_chan_putbyte
    jc .bsph_propagate
    mov sp, bp
    pop bp
    pop dx
    pop cx
    pop bx
    clc
    ret
.bsph_syn:
    mov sp, bp
    pop bp
    pop dx
    pop cx
    pop bx
    mov al, BERR_SYNTAX
    stc
    ret
.bsph_notopen:
    mov sp, bp
    pop bp
    pop dx
    pop cx
    pop bx
    mov al, BERR_NOTOPEN
    stc
    ret
.bsph_propagate:
    mov sp, bp
    pop bp
    pop dx
    pop cx
    pop bx
    stc
    ret

; ---------------------------------------------------------------------------
; bas_stmt_input_hash — INPUT #N, var[, var]*
;   Entry: SI just past TOK_INPUT, points at '#'
; ---------------------------------------------------------------------------
bas_stmt_input_hash:
    push bx
    push cx
    push dx
    push bp
    mov bp, sp
    sub sp, 4
    ; [bp-2] = chan ptr
    ; [bp-4] = parse cursor into bas_line_buf
    inc si                              ; past '#'
    call bas_expr_eval_int
    jc .bsih_propagate
    mov ax, dx
    call bas_chan_rec
    jc .bsih_propagate
    cmp byte [bx + BCH_MODE], BCHAN_INPUT
    jne .bsih_notopen
    mov [bp-2], bx
    cmp byte [si], ','
    jne .bsih_syn
    inc si
    ; Read a line from the channel.
    call bas_chan_readline
    jc .bsih_eofline                    ; CF=1 only when empty AND eof
    mov word [bp-4], bas_line_buf
.bsih_var_loop:
    cmp byte [si], TOK_VAR_NAME
    jne .bsih_syn
    inc si
    mov al, [si]
    mov ah, [si+1]
    add si, 2
    mov [bas_scratch_b], ax             ; save name (AL=name0, AH=name1)
    ; Skip leading spaces/tabs in source line
    mov bx, [bp-4]
.bsih_skip_ws:
    mov dl, [bx]
    test dl, dl
    jz .bsih_field_empty
    cmp dl, ' '
    je .bsih_skip_advance
    cmp dl, 9
    je .bsih_skip_advance
    jmp .bsih_have_field
.bsih_skip_advance:
    inc bx
    jmp .bsih_skip_ws
.bsih_have_field:
.bsih_field_empty:
    mov ax, [bas_scratch_b]
    test al, 0x80
    jnz .bsih_str_var
    ; Numeric: parse decimal via bas_parse_int (uses DS:SI)
    push si
    mov si, bx
    call bas_parse_int                  ; returns DX, SI advanced
    mov [bp-4], si
    pop si
    mov ax, [bas_scratch_b]
    call bas_var_set_int
    jc .bsih_propagate
    jmp .bsih_after_field
.bsih_str_var:
    ; String: take bytes until ',' or NUL
    push si
    mov si, bx
    xor cx, cx
.bsih_str_scan:
    mov dl, [si]
    test dl, dl
    jz .bsih_str_done
    cmp dl, ','
    je .bsih_str_done
    inc si
    inc cx
    cmp cx, BAS_STR_VAR_LEN
    jb .bsih_str_scan
.bsih_str_done:
    mov [bp-4], si
    pop si
    ; Build a literal descriptor pointing into bas_line_buf (BX = field start)
    mov ax, ds
    mov [bas_expr_result + BES_VAL_LO], ax
    mov [bas_expr_result + BES_VAL_HI], bx
    mov [bas_expr_result + BES_LEN], cx
    mov byte [bas_expr_result + BES_TYPE], BVAR_STRING
    mov byte [bas_expr_result + BES_FLAGS], BSTR_LITERAL
    mov ax, [bas_scratch_b]
    call bas_var_set_str
    jc .bsih_propagate
.bsih_after_field:
    ; Advance past comma in source line if present
    mov bx, [bp-4]
    cmp byte [bx], ','
    jne .bsih_no_src_comma
    inc bx
    mov [bp-4], bx
.bsih_no_src_comma:
    ; More vars?
    cmp byte [si], ','
    jne .bsih_done
    inc si
    jmp .bsih_var_loop
.bsih_done:
    mov sp, bp
    pop bp
    pop dx
    pop cx
    pop bx
    clc
    ret
.bsih_eofline:
    mov sp, bp
    pop bp
    pop dx
    pop cx
    pop bx
    mov al, BERR_INPUTPAST
    stc
    ret
.bsih_syn:
    mov sp, bp
    pop bp
    pop dx
    pop cx
    pop bx
    mov al, BERR_SYNTAX
    stc
    ret
.bsih_notopen:
    mov sp, bp
    pop bp
    pop dx
    pop cx
    pop bx
    mov al, BERR_NOTOPEN
    stc
    ret
.bsih_propagate:
    mov sp, bp
    pop bp
    pop dx
    pop cx
    pop bx
    stc
    ret

%endif
