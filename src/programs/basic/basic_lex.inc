; =============================================================================
; basic_lex.inc — Tokenizer and detokenizer for BASIC source lines
;
; A statement-aware single-pass tokenizer.  Source line goes in (NUL- or
; CR/LF-terminated); a tokenized payload comes out, terminated by TOK_EOL.
;
; Token byte ranges (see basic_tokens.inc):
;   0x00          TOK_EOL
;   0x20..0x7E    raw ASCII punctuation (`+-*/^()=,;:#` etc.)
;   0x80..0xBF    statement keywords
;   0xC0..0xDF    built-in functions
;   0xE0..0xEF    multi-char operators (MOD, AND, OR, NOT, <>, <=, >=)
;   0xF0          TOK_INT_LIT  + 2-byte signed
;   0xF1          TOK_LINEREF  + 2-byte unsigned (1..32767)
;   0xF2          TOK_STR_LIT  + 1-byte len + bytes
;   0xF3          TOK_VAR_NAME + name0 + name1
;
; Public:
;   bas_tokenize_line   DS:SI = NUL-terminated source text
;                       ES:DI = output buffer (≥256 bytes)
;                       Out: CX = bytes written incl. TOK_EOL
;                            CF=0 ok, CF=1 → AL = BERR_*
;
;   bas_detokenize_line DS:SI = tokenized payload
;                       Prints LIST-formatted source via SYS_PRINT_CHAR.
;                       SI ends pointing past TOK_EOL.
;
;   bas_tok_kw_name     AL = token byte (≥0x80) → DS:SI = name (CF=0)
;                       CF=1 if no entry.
; =============================================================================

; ---------- Module-private state (in BSS) -----------------------------------
;
; These four bytes live in the BSS scratch area carved out of basic_data.inc.
; We don't want a `db` here because this file is included inside the code
; section, so we use the bas_scratch_a slot in basic_data BSS for backing.
;
;   bas_lex_mode      tracks line-ref context (0 = normal, 1 = single ref,
;                                              2 = ON list, 3 = LIST n[-m])
;   bas_lex_in_rem    1 = next loop iteration copies raw bytes (after REM)
;
; They're allocated as 2 bytes inside bas_scratch_a (word) and 2 bytes
; inside bas_scratch_b (word).  See aliases below.
; ---------------------------------------------------------------------------
bas_lex_mode      equ bas_scratch_a       ; byte
bas_lex_in_rem    equ bas_scratch_a + 1   ; byte
bas_lex_dummy_lex equ bas_scratch_b       ; word reserved


; =============================================================================
; bas_tok_kw_name — token byte (≥0x80) → name string pointer.
;
; In : AL = token
; Out: DS:SI = NUL-terminated name (when CF=0)
;      CF=0 found, CF=1 missing entry
; Preserves: AX, BX, CX, DX, DI, ES, BP
; =============================================================================
bas_tok_kw_name:
    push bx
    cmp al, 0x80
    jb .bkn_miss
    cmp al, 0xEF
    ja .bkn_miss
    mov bl, al
    xor bh, bh
    sub bx, 0x80
    shl bx, 1                       ; word index
    mov si, [bas_det_table + bx]
    test si, si
    jz .bkn_miss
    pop bx
    clc
    ret
.bkn_miss:
    xor si, si
    pop bx
    stc
    ret


; =============================================================================
; bas_tokenize_line
; =============================================================================
bas_tokenize_line:
    push bp
    push bx
    push dx
    mov bp, di                      ; BP = output buffer base
    xor cx, cx                      ; CX = bytes written
    mov byte [bas_lex_mode], 0
    mov byte [bas_lex_in_rem], 0
    cld

.tk_loop:
    ; If we're in REM-tail mode, copy raw bytes until terminator.
    cmp byte [bas_lex_in_rem], 0
    je .tk_normal
    lodsb
    test al, al
    jz .tk_finish
    cmp al, 13
    je .tk_finish
    cmp al, 10
    je .tk_finish
    call bas_lex_emit
    jc .tk_overflow
    jmp .tk_loop

.tk_normal:
    ; Skip whitespace
.tk_skip_ws:
    mov al, [si]
    cmp al, ' '
    je .tk_eat_ws
    cmp al, 9
    je .tk_eat_ws
    jmp .tk_after_ws
.tk_eat_ws:
    inc si
    jmp .tk_skip_ws
.tk_after_ws:
    test al, al
    jz .tk_finish
    cmp al, 13
    je .tk_finish
    cmp al, 10
    je .tk_finish

    ; Dispatch by first character class
    cmp al, '"'
    je .tk_string
    cmp al, "'"
    je .tk_apostrophe
    cmp al, '?'
    je .tk_qmark
    cmp al, '<'
    je .tk_lt
    cmp al, '>'
    je .tk_gt
    cmp al, '0'
    jb .tk_punct
    cmp al, '9'
    jbe .tk_number
    ; Letter (A-Z or a-z)?
    mov bl, al
    or  bl, 0x20
    cmp bl, 'a'
    jb .tk_punct
    cmp bl, 'z'
    ja .tk_punct
    jmp .tk_alpha

; --- Punctuation: raw byte emit -----------------------------------------
.tk_punct:
    inc si                          ; consume
    call bas_lex_emit
    jc .tk_overflow
    call bas_lex_after_emit
    jmp .tk_loop

; --- Apostrophe → REM + raw tail ----------------------------------------
.tk_apostrophe:
    inc si
    mov al, TOK_REM
    call bas_lex_emit
    jc .tk_overflow
    mov al, ' '
    call bas_lex_emit
    jc .tk_overflow
    mov byte [bas_lex_in_rem], 1
    jmp .tk_loop

; --- `?` → PRINT --------------------------------------------------------
.tk_qmark:
    inc si
    mov al, TOK_PRINT
    call bas_lex_emit
    jc .tk_overflow
    call bas_lex_after_emit
    jmp .tk_loop

; --- '<' optionally followed by '=' or '>' ------------------------------
.tk_lt:
    inc si
    mov al, [si]
    cmp al, '='
    je .tk_lt_eq
    cmp al, '>'
    je .tk_lt_gt
    mov al, '<'
    call bas_lex_emit
    jc .tk_overflow
    jmp .tk_after_op

.tk_lt_eq:
    inc si
    mov al, TOK_LE
    call bas_lex_emit
    jc .tk_overflow
    jmp .tk_after_op

.tk_lt_gt:
    inc si
    mov al, TOK_NE
    call bas_lex_emit
    jc .tk_overflow
    jmp .tk_after_op

; --- '>' optionally followed by '=' -------------------------------------
.tk_gt:
    inc si
    mov al, [si]
    cmp al, '='
    je .tk_gt_eq
    mov al, '>'
    call bas_lex_emit
    jc .tk_overflow
    jmp .tk_after_op

.tk_gt_eq:
    inc si
    mov al, TOK_GE
    call bas_lex_emit
    jc .tk_overflow
.tk_after_op:
    call bas_lex_after_emit
    jmp .tk_loop

; --- String literal -----------------------------------------------------
.tk_string:
    inc si                          ; skip opening quote
    call bas_lex_string
    jc .tk_fail
    call bas_lex_after_emit
    jmp .tk_loop

; --- Decimal number literal --------------------------------------------
.tk_number:
    call bas_lex_number
    jc .tk_fail
    call bas_lex_after_emit
    jmp .tk_loop

; --- Letter: keyword match or variable name ----------------------------
.tk_alpha:
    call bas_lex_word
    jc .tk_fail
    call bas_lex_after_emit
    ; If word emitted TOK_REM, switch to REM-tail mode
    cmp byte [bas_lex_in_rem], 0
    je .tk_loop
    ; Emit a leading space before raw text for cleaner LIST
    mov al, ' '
    call bas_lex_emit
    jc .tk_overflow
    jmp .tk_loop

.tk_finish:
    mov al, TOK_EOL
    call bas_lex_emit
    jc .tk_overflow
    pop dx
    pop bx
    pop bp
    clc
    ret

.tk_overflow:
    mov al, BERR_LINETOOLONG
.tk_fail:
    pop dx
    pop bx
    pop bp
    stc
    ret


; =============================================================================
; bas_lex_emit — write AL to ES:DI; bump CX; CF=1 if buffer overflow.
; Preserves: AX, BX, DX, SI, BP, ES
; =============================================================================
bas_lex_emit:
    cmp cx, BAS_TOKEN_BUF_LEN - 1   ; reserve 1 byte for final TOK_EOL
    jae .lem_full
    mov [es:di], al
    inc di
    inc cx
    clc
    ret
.lem_full:
    stc
    ret


; =============================================================================
; bas_lex_after_emit — update line-ref context based on the last emitted
; byte (assumed to be in AL on entry).
;
; Mode transitions (mode = bas_lex_mode):
;   any GOTO/GOSUB/THEN/ELSE/RUN → mode = 1
;   ON                            → mode = 2
;   LIST                          → mode = 3
;   ':'                           → mode = 0
;   ','                           → keep current
;   anything else in mode 1       → mode = 0
;
; A REM token also triggers REM-tail mode (caller switches to copy).
; =============================================================================
bas_lex_after_emit:
    cmp al, TOK_REM
    jne .lae_not_rem
    mov byte [bas_lex_in_rem], 1
    ret
.lae_not_rem:
    cmp al, TOK_GOTO
    je .lae_set1
    cmp al, TOK_GOSUB
    je .lae_set1
    cmp al, TOK_THEN
    je .lae_set1
    cmp al, TOK_ELSE
    je .lae_set1
    cmp al, TOK_RUN
    je .lae_set1
    cmp al, TOK_ON
    je .lae_set2
    cmp al, TOK_LIST
    je .lae_set3
    cmp al, ':'
    je .lae_clear
    cmp al, ','
    je .lae_keep
    cmp al, '-'
    je .lae_keep
    cmp byte [bas_lex_mode], 1
    jne .lae_done
    mov byte [bas_lex_mode], 0
.lae_done:
    ret
.lae_set1:
    mov byte [bas_lex_mode], 1
    ret
.lae_set2:
    mov byte [bas_lex_mode], 2
    ret
.lae_set3:
    mov byte [bas_lex_mode], 3
    ret
.lae_clear:
    mov byte [bas_lex_mode], 0
    ret
.lae_keep:
    ret


; =============================================================================
; bas_lex_string — emit TOK_STR_LIT + len + bytes.
; On entry: SI points at first byte after the opening '"'.
; On exit:  SI past closing '"' (or past terminator if unterminated).
;           CF=1 → AL = BERR_*.
; =============================================================================
bas_lex_string:
    push bx
    push dx
    mov al, TOK_STR_LIT
    call bas_lex_emit
    jc .lstr_overflow
    ; Reserve length byte
    cmp cx, BAS_TOKEN_BUF_LEN - 1
    jae .lstr_overflow
    mov byte [es:di], 0
    mov bx, di                      ; remember length-byte ES:BX
    inc di
    inc cx
    xor dl, dl                      ; DL = collected length
.lstr_loop:
    mov al, [si]
    test al, al
    jz .lstr_eol
    cmp al, 13
    je .lstr_eol
    cmp al, 10
    je .lstr_eol
    cmp al, '"'
    je .lstr_close
    cmp dl, 255
    jae .lstr_toolong
    inc si
    call bas_lex_emit
    jc .lstr_overflow
    inc dl
    jmp .lstr_loop
.lstr_close:
    inc si                          ; consume closing quote
.lstr_eol:
    mov [es:bx], dl
    pop dx
    pop bx
    clc
    ret
.lstr_toolong:
    pop dx
    pop bx
    mov al, BERR_STRTOOLONG
    stc
    ret
.lstr_overflow:
    pop dx
    pop bx
    mov al, BERR_LINETOOLONG
    stc
    ret


; =============================================================================
; bas_lex_number — emit TOK_INT_LIT or TOK_LINEREF based on context.
; On entry: SI at first digit.
; On exit:  SI past last digit; CF=1 → AL = BERR_*.
; =============================================================================
bas_lex_number:
    push bx
    push dx
    xor ax, ax                      ; accumulator
.lnm_loop:
    mov bl, [si]
    cmp bl, '0'
    jb .lnm_done
    cmp bl, '9'
    ja .lnm_done
    ; AX = AX * 10 + (bl - '0')
    mov dx, ax
    shl ax, 1
    jc .lnm_overflow
    shl ax, 1
    jc .lnm_overflow
    add ax, dx
    jc .lnm_overflow
    shl ax, 1
    jc .lnm_overflow
    sub bl, '0'
    xor bh, bh
    add ax, bx
    jc .lnm_overflow
    cmp ax, 0x8000
    jae .lnm_overflow
    inc si
    jmp .lnm_loop

.lnm_done:
    push ax                         ; preserve value across emits
    cmp byte [bas_lex_mode], 0
    je .lnm_emit_int
    ; Line-ref context
    test ax, ax
    jz .lnm_bad_lineno
    mov al, TOK_LINEREF
    call bas_lex_emit
    jc .lnm_overflow_pop
    pop ax
    call bas_lex_emit_word
    jc .lnm_overflow
    ; Update mode: 1 → 0; 3 → 0 after second number (state 3 stays 3 only once)
    cmp byte [bas_lex_mode], 1
    jne .lnm_check3
    mov byte [bas_lex_mode], 0
    jmp .lnm_ok
.lnm_check3:
    cmp byte [bas_lex_mode], 3
    jne .lnm_ok
    mov byte [bas_lex_mode], 0
.lnm_ok:
    pop dx
    pop bx
    clc
    ret

.lnm_emit_int:
    mov al, TOK_INT_LIT
    call bas_lex_emit
    jc .lnm_overflow_pop
    pop ax
    call bas_lex_emit_word
    jc .lnm_overflow
    pop dx
    pop bx
    clc
    ret

.lnm_overflow:
    pop dx
    pop bx
    mov al, BERR_OVERFLOW
    stc
    ret
.lnm_overflow_pop:
    pop ax
    pop dx
    pop bx
    mov al, BERR_LINETOOLONG
    stc
    ret
.lnm_bad_lineno:
    pop ax
    pop dx
    pop bx
    mov al, BERR_BADLINENUM
    stc
    ret


; -- Helper: emit AX as 16-bit LE (2 bytes) ----------------------------------
bas_lex_emit_word:
    push ax
    call bas_lex_emit               ; low byte
    jc .lew_fail
    pop ax
    mov al, ah
    call bas_lex_emit               ; high byte
    ret
.lew_fail:
    pop ax
    stc
    ret


; =============================================================================
; bas_lex_word — match a keyword from the table, or emit a variable name.
;
; On entry: SI at first letter.
; On exit:  SI past identifier (or matched keyword spelling).
;           CF=1 → AL = BERR_LINETOOLONG.
;
; Algorithm:
;   1. Walk bas_keyword_table linearly; for each entry compare its name
;      against the source bytes case-insensitively.  On match, require the
;      source byte AFTER the match to not be A-Z / 0-9 / '$' (so 'INPUT$'
;      and 'INPUTX' are not matched by 'INPUT').
;   2. If no keyword matches, treat as identifier: letter, optional digit,
;      optional '$'.  Emit TOK_VAR_NAME + name0 + name1.
; =============================================================================
bas_lex_word:
    push bx
    push dx
    push bp
    mov bp, si                      ; BP = identifier start
    mov bx, bas_keyword_table

.lw_kw_entry:
    mov al, [bx]                    ; entry length, or 0 = end
    test al, al
    jz .lw_no_kw
    mov ah, al                      ; AH = length to compare
    inc bx                          ; BX = first char of name
    mov si, bp                      ; SI = source cursor (reset per entry)
.lw_cmp:
    mov dl, [bx]
    mov dh, [si]
    cmp dh, 'a'
    jb .lw_dh_ok
    cmp dh, 'z'
    ja .lw_dh_ok
    sub dh, 0x20
.lw_dh_ok:
    cmp dl, dh
    jne .lw_kw_mismatch
    inc bx
    inc si
    dec ah
    jnz .lw_cmp
    ; Matched the keyword characters — check trailing character class.
    mov dh, [si]
    cmp dh, '$'
    je .lw_kw_reject
    cmp dh, 'a'
    jb .lw_check_upper
    cmp dh, 'z'
    ja .lw_check_upper
    jmp .lw_kw_reject
.lw_check_upper:
    cmp dh, 'A'
    jb .lw_check_digit
    cmp dh, 'Z'
    ja .lw_check_digit
    jmp .lw_kw_reject
.lw_check_digit:
    cmp dh, '0'
    jb .lw_kw_accept
    cmp dh, '9'
    ja .lw_kw_accept
    jmp .lw_kw_reject
.lw_kw_accept:
    mov al, [bx]                    ; token byte
    pop bp
    push bp
    call bas_lex_emit
    jc .lw_overflow
    pop bp
    pop dx
    pop bx
    clc
    ret

.lw_kw_mismatch:
    ; AH = remaining chars in this entry's name (not yet compared).
    ; Need to advance BX past the entry: skip the remaining name bytes,
    ; then 1 token byte + 1 flags byte.
    movzx dx, ah
    add bx, dx
    add bx, 2
    jmp .lw_kw_entry
.lw_kw_reject:
    ; All length-N chars matched, but suffix invalid → reject this keyword
    ; and advance to next entry.  BX currently points to the token byte.
    add bx, 2
    jmp .lw_kw_entry

.lw_no_kw:
    ; --- Emit variable name -------------------------------------------------
    mov si, bp                      ; reset SI to identifier start
    mov al, [si]
    inc si
    cmp al, 'a'
    jb .lw_v_upper
    cmp al, 'z'
    ja .lw_v_upper
    sub al, 0x20
.lw_v_upper:
    sub al, 'A'                     ; AL = letter index 0..25
    mov dl, al                      ; DL = name0
    mov dh, 0xFF                    ; DH = name1 (none)
    mov ah, [si]
    cmp ah, '0'
    jb .lw_v_no_digit
    cmp ah, '9'
    ja .lw_v_no_digit
    sub ah, '0'
    mov dh, ah
    inc si
.lw_v_no_digit:
    mov ah, [si]
    cmp ah, '$'
    jne .lw_v_no_dollar
    or  dl, 0x80
    inc si
.lw_v_no_dollar:
    mov al, TOK_VAR_NAME
    call bas_lex_emit
    jc .lw_overflow
    mov al, dl
    call bas_lex_emit
    jc .lw_overflow
    mov al, dh
    call bas_lex_emit
    jc .lw_overflow
    pop bp
    pop dx
    pop bx
    clc
    ret

.lw_overflow:
    pop bp
    pop dx
    pop bx
    mov al, BERR_LINETOOLONG
    stc
    ret


; =============================================================================
; bas_detokenize_line — print tokenized payload as source text.
;
; In:  DS:SI = first byte of payload
; Out: SI past TOK_EOL; nothing on the stack changed.
; Uses console syscalls.
; =============================================================================
bas_detokenize_line:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    xor cx, cx                      ; CX:bit0 = need-leading-space flag

.dt_loop:
    mov al, [si]
    inc si
    cmp al, TOK_EOL
    je .dt_done

    cmp al, TOK_INT_LIT
    je .dt_int
    cmp al, TOK_LINEREF
    je .dt_lineref
    cmp al, TOK_STR_LIT
    je .dt_str
    cmp al, TOK_VAR_NAME
    je .dt_var
    cmp al, 0x80
    jae .dt_keyword
    ; Punctuation/ASCII
    cmp al, ' '
    je .dt_emit_space_token
    cmp al, ','
    je .dt_emit_no_space
    cmp al, ';'
    je .dt_emit_no_space
    cmp al, ':'
    je .dt_emit_no_space
    cmp al, '('
    je .dt_emit_no_space
    cmp al, ')'
    je .dt_emit_no_space
    cmp al, '#'
    je .dt_emit_no_space
    ; Default: just print
    call bas_lex_putc
    or  cl, 1
    jmp .dt_loop

.dt_emit_space_token:
    mov al, ' '
    call bas_lex_putc
    and cl, 0xFE
    jmp .dt_loop
.dt_emit_no_space:
    call bas_lex_putc
    and cl, 0xFE
    jmp .dt_loop

.dt_int:
    mov ax, [si]
    add si, 2
    test cl, 1
    jz .dt_int_print
    call bas_lex_space
.dt_int_print:
    mov dx, ax
    call bas_lex_print_signed
    or  cl, 1
    jmp .dt_loop

.dt_lineref:
    mov ax, [si]
    add si, 2
    test cl, 1
    jz .dt_lr_print
    call bas_lex_space
.dt_lr_print:
    mov dx, ax
    call bas_lex_print_unsigned
    or  cl, 1
    jmp .dt_loop

.dt_str:
    mov al, '"'
    call bas_lex_putc
    mov al, [si]
    inc si
    mov dl, al                      ; DL = length
.dt_str_loop:
    test dl, dl
    jz .dt_str_end
    mov al, [si]
    inc si
    call bas_lex_putc
    dec dl
    jmp .dt_str_loop
.dt_str_end:
    mov al, '"'
    call bas_lex_putc
    or  cl, 1
    jmp .dt_loop

.dt_var:
    test cl, 1
    jz .dt_var_print
    call bas_lex_space
.dt_var_print:
    mov al, [si]
    inc si
    mov dh, al                      ; DH = raw name0 (with flag)
    and al, 0x1F
    add al, 'A'
    call bas_lex_putc
    mov al, [si]                    ; name1
    inc si
    cmp al, 10
    jae .dt_var_no_digit
    add al, '0'
    call bas_lex_putc
.dt_var_no_digit:
    test dh, 0x80
    jz .dt_var_done
    mov al, '$'
    call bas_lex_putc
.dt_var_done:
    or  cl, 1
    jmp .dt_loop

.dt_keyword:
    push si
    call bas_tok_kw_name            ; DS:SI = name (CF=1 on miss)
    jc .dt_kw_miss
    mov bx, si
    pop si
    test cl, 1
    jz .dt_kw_print
    call bas_lex_space
.dt_kw_print:
.dt_kw_chr_loop:
    mov al, [bx]
    test al, al
    jz .dt_kw_done
    call bas_lex_putc
    inc bx
    jmp .dt_kw_chr_loop
.dt_kw_done:
    or  cl, 1
    jmp .dt_loop
.dt_kw_miss:
    pop si
    mov al, '?'
    call bas_lex_putc
    or  cl, 1
    jmp .dt_loop

.dt_done:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret


; =============================================================================
; Small console helpers — internal to the lex module (named bas_lex_*).
; =============================================================================
bas_lex_putc:
    push ax
    push bx
    push cx
    push dx
    mov ah, SYS_PRINT_CHAR
    int 0x80
    pop dx
    pop cx
    pop bx
    pop ax
    ret

bas_lex_space:
    push ax
    mov al, ' '
    call bas_lex_putc
    pop ax
    ret

; Print DX as signed 16-bit decimal
bas_lex_print_signed:
    push ax
    push bx
    push cx
    push dx
    test dx, dx
    jns .lps_pos
    push dx
    mov al, '-'
    call bas_lex_putc
    pop dx
    neg dx
.lps_pos:
    call bas_lex_print_unsigned_raw
    pop dx
    pop cx
    pop bx
    pop ax
    ret

bas_lex_print_unsigned:
    push ax
    push bx
    push cx
    push dx
    call bas_lex_print_unsigned_raw
    pop dx
    pop cx
    pop bx
    pop ax
    ret

bas_lex_print_unsigned_raw:
    mov ax, dx
    xor cx, cx
    mov bx, 10
.lpu_div:
    xor dx, dx
    div bx
    push dx
    inc cx
    test ax, ax
    jnz .lpu_div
.lpu_emit:
    pop ax
    add al, '0'
    call bas_lex_putc
    loop .lpu_emit
    ret
