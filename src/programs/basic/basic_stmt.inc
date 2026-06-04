; =============================================================================
; basic_stmt.inc — Statement dispatcher + handlers for the v1 statement set.
;
; v1 supports (functional):
;   PRINT     (literal strings, integer literals; `;`/`,` separators; ?)
;   REM       (no-op; payload already consumed at tokenize time)
;   END       (stop run if running; otherwise no-op)
;   STOP      (same as END)
;   LIST      (list program; optional [n[-m]] range)
;   NEW       (clear program + state)
;   CLEAR     (clear state, keep program)
;   RUN       (execute program from start)
;   SYSTEM    (exit interpreter)
;   LOAD "..."
;   SAVE "..."
;   FILES
;   CLS       (clear screen)
;   HELP      (print built-in help)
;
; v1 returns BERR_SYNTAX (or a polite stub message) for unimplemented
; statements (IF/GOTO/FOR/etc.).  Subsequent phases implement them.
;
; Public:
;   bas_run_direct       In: DS:SI = tokenized line (TOK_EOL terminated)
;                            CX = line length (incl TOK_EOL)
;                        Executes one statement (or `:`-separated set).
;                        Errors longjmp via bas_error.
;
;   bas_run_program      In: nothing
;                        Iterates program lines from start, executing each.
;                        Honours END/STOP via bas_in_run flag.
;
;   bas_break_check      Poll Ctrl+C / Esc via SYS_PEEK_KEY+SYS_READ_KEY;
;                        if pressed, calls bas_error(BERR_BREAK).
; =============================================================================


; =============================================================================
; bas_run_direct — execute tokenized line at DS:SI
; =============================================================================
bas_run_direct:
    push bp
    push bx
    push cx
    push dx
    push si
    push di
.brd_next_stmt:
    ; Skip leading TOK_EOL / colons (empty statement)
    mov al, [si]
    cmp al, ':'
    je .brd_skip
    cmp al, TOK_EOL
    je .brd_done
    ; Dispatch by first token
    inc si
    cmp al, TOK_REM
    je .brd_rem
    cmp al, TOK_PRINT
    je .brd_print
    cmp al, TOK_END
    je .brd_end
    cmp al, TOK_STOP
    je .brd_end
    cmp al, TOK_NEW
    je .brd_new
    cmp al, TOK_CLEAR
    je .brd_clear
    cmp al, TOK_SYSTEM
    je .brd_system
    cmp al, TOK_LIST
    je .brd_list
    cmp al, TOK_RUN
    je .brd_run
    cmp al, TOK_FILES
    je .brd_files
    cmp al, TOK_CLS
    je .brd_cls
    cmp al, TOK_LOAD
    je .brd_load
    cmp al, TOK_SAVE
    je .brd_save
    cmp al, TOK_HELP
    je .brd_help
; --- Anything else → check for implicit LET (TOK_VAR_NAME) or error -----
    cmp al, TOK_VAR_NAME
    je .brd_let_implicit
    cmp al, TOK_LET
    je .brd_let
    cmp al, TOK_IF
    je .brd_if
    cmp al, TOK_GOTO
    je .brd_goto
    cmp al, TOK_GOSUB
    je .brd_gosub
    cmp al, TOK_RETURN
    je .brd_return
    cmp al, TOK_FOR
    je .brd_for
    cmp al, TOK_NEXT
    je .brd_next
    cmp al, TOK_WHILE
    je .brd_while
    cmp al, TOK_WEND
    je .brd_wend
    cmp al, TOK_ON
    je .brd_on
    cmp al, TOK_INPUT
    je .brd_input
    cmp al, TOK_LOCATE
    je .brd_locate
    cmp al, TOK_COLOR
    je .brd_color
    cmp al, TOK_POKE
    je .brd_poke
    cmp al, TOK_DEF
    je .brd_def
    cmp al, TOK_RANDOMIZE
    je .brd_random
    jmp .brd_syntax
.brd_let_implicit:
    dec si                          ; back up so LET handler re-reads var name
    call bas_stmt_let
    jc .brd_propagate
    jmp .brd_eat_to_sep
.brd_let:
    call bas_stmt_let
    jc .brd_propagate
    jmp .brd_eat_to_sep
.brd_if:
    call bas_stmt_if                ; may consume rest of line or jump
    jc .brd_propagate
    jmp .brd_done                   ; IF always consumes to TOK_EOL itself
.brd_goto:
    call bas_stmt_goto
    jc .brd_propagate
    jmp .brd_done
.brd_gosub:
    call bas_stmt_gosub
    jc .brd_propagate
    jmp .brd_done
.brd_return:
    call bas_stmt_return
    jc .brd_propagate
    jmp .brd_done
.brd_for:
    call bas_stmt_for
    jc .brd_propagate
    jmp .brd_eat_to_sep
.brd_next:
    call bas_stmt_next
    jc .brd_propagate
    jmp .brd_done                   ; NEXT either jumps (handled) or pops; rest of line skipped
.brd_while:
    call bas_stmt_while
    jc .brd_propagate
    jmp .brd_done
.brd_wend:
    call bas_stmt_wend
    jc .brd_propagate
    jmp .brd_done
.brd_on:
    call bas_stmt_on
    jc .brd_propagate
    jmp .brd_done
.brd_input:
    call bas_stmt_input
    jc .brd_propagate
    jmp .brd_eat_to_sep
.brd_locate:
    call bas_stmt_locate
    jc .brd_propagate
    jmp .brd_eat_to_sep
.brd_color:
    call bas_stmt_color
    jc .brd_propagate
    jmp .brd_eat_to_sep
.brd_poke:
    call bas_stmt_poke
    jc .brd_propagate
    jmp .brd_eat_to_sep
.brd_def:
    call bas_stmt_def_seg
    jc .brd_propagate
    jmp .brd_eat_to_sep
.brd_random:
    call bas_stmt_randomize
    jc .brd_propagate
    jmp .brd_eat_to_sep
.brd_skip:
    inc si
    jmp .brd_next_stmt
.brd_done:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop bp
    ret

; --- REM: consume until TOK_EOL (no colon-following allowed) ---------------
.brd_rem:
.brd_rem_loop:
    mov al, [si]
    cmp al, TOK_EOL
    je .brd_done
    inc si
    jmp .brd_rem_loop

; --- END / STOP -----------------------------------------------------------
.brd_end:
    mov byte [bas_in_run], 0
    jmp .brd_eat_to_sep

; --- NEW -----------------------------------------------------------------
.brd_new:
    call bas_cmd_new
    jmp .brd_eat_to_sep

; --- CLEAR ---------------------------------------------------------------
.brd_clear:
    call bas_cmd_clear
    jmp .brd_eat_to_sep

; --- SYSTEM (exit interpreter) -------------------------------------------
.brd_system:
    mov ah, SYS_EXIT
    xor al, al
    int 0x80
    ; Should not return; fall through anyway
    jmp .brd_eat_to_sep

; --- LIST -----------------------------------------------------------------
.brd_list:
    call bas_stmt_list
    jmp .brd_eat_to_sep

; --- RUN -----------------------------------------------------------------
.brd_run:
    ; Skip any optional line-ref argument; RUN with line number not yet
    ; implemented — treat it like a bare RUN with a warning if specified.
    ; (Eat any tokens until separator/EOL.)
    call .brd_consume_args
    call bas_run_program
    jmp .brd_done                   ; bas_run_program returns once END/STOP/last line

; --- FILES ---------------------------------------------------------------
.brd_files:
    call bas_cmd_files
    jmp .brd_eat_to_sep

; --- CLS -----------------------------------------------------------------
.brd_cls:
    call bas_stmt_cls
    jmp .brd_eat_to_sep

; --- LOAD "name" ---------------------------------------------------------
.brd_load:
    call .brd_parse_string_arg      ; CX = len, DS:DI = name buffer; on err CF=1
    jc .brd_propagate
    push si                         ; preserve token cursor
    mov si, di
    call bas_load_file
    pop si
    jc .brd_propagate
    jmp .brd_eat_to_sep

; --- SAVE "name" ---------------------------------------------------------
.brd_save:
    call .brd_parse_string_arg
    jc .brd_propagate
    push si
    mov si, di
    call bas_save_file
    pop si
    jc .brd_propagate
    jmp .brd_eat_to_sep

; --- HELP ----------------------------------------------------------------
.brd_help:
    mov si, bas_help_text
    call bas_puts_nul
    jmp .brd_eat_to_sep

; --- PRINT (with literal-string / int-literal subset for v1) -------------
.brd_print:
    call bas_stmt_print
    jc .brd_propagate
    jmp .brd_eat_to_sep

; --- Syntax error → longjmp via bas_error --------------------------------
.brd_syntax:
    mov al, BERR_SYNTAX
    call bas_error                  ; never returns

.brd_propagate:
    call bas_error                  ; AL already set, never returns

; --- Eat tokens of current statement up to next : or TOK_EOL ------------
.brd_eat_to_sep:
    mov al, [si]
    cmp al, TOK_EOL
    je .brd_done
    cmp al, ':'
    je .brd_skip
    ; Skip token (and any operand bytes)
    inc si
    cmp al, TOK_INT_LIT
    je .brd_skip_2
    cmp al, TOK_LINEREF
    je .brd_skip_2
    cmp al, TOK_STR_LIT
    je .brd_skip_str
    cmp al, TOK_VAR_NAME
    je .brd_skip_2
    jmp .brd_eat_to_sep
.brd_skip_2:
    add si, 2
    jmp .brd_eat_to_sep
.brd_skip_str:
    mov al, [si]
    inc si
    movzx ax, al
    add si, ax
    jmp .brd_eat_to_sep

.brd_consume_args:
    ; Local: skip current statement's operands without executing
.brd_ca_loop:
    mov al, [si]
    cmp al, TOK_EOL
    je .brd_ca_done
    cmp al, ':'
    je .brd_ca_done
    inc si
    cmp al, TOK_INT_LIT
    je .brd_ca_2
    cmp al, TOK_LINEREF
    je .brd_ca_2
    cmp al, TOK_STR_LIT
    je .brd_ca_str
    cmp al, TOK_VAR_NAME
    je .brd_ca_2
    jmp .brd_ca_loop
.brd_ca_2:
    add si, 2
    jmp .brd_ca_loop
.brd_ca_str:
    mov al, [si]
    inc si
    movzx ax, al
    add si, ax
    jmp .brd_ca_loop
.brd_ca_done:
    ret

; ----------------------------------------------------------------------------
; .brd_parse_string_arg — expects next token to be TOK_STR_LIT, returns
; CX = length, DS:DI = pointer to start of bytes (inside the token stream).
; Advances SI past the string token.  CF=1 → AL = BERR_SYNTAX.
; ----------------------------------------------------------------------------
.brd_parse_string_arg:
    mov al, [si]
    cmp al, TOK_STR_LIT
    jne .bpsa_err
    inc si
    mov al, [si]                    ; length
    inc si
    movzx cx, al
    mov di, si                      ; DI = byte ptr
    add si, cx                      ; advance past bytes
    clc
    ret
.bpsa_err:
    mov al, BERR_SYNTAX
    stc
    ret


; =============================================================================
; bas_stmt_list — LIST [n [- m]]
; For v1 we accept any line-ref operands but just list ALL lines.
; =============================================================================
bas_stmt_list:
    push ax
    push bx
    push cx
    push si

    ; Consume any list args (we ignore them for v1)
.bsl_skip_args:
    mov al, [si]
    cmp al, TOK_EOL
    je .bsl_listit
    cmp al, ':'
    je .bsl_listit
    inc si
    cmp al, TOK_INT_LIT
    je .bsl_arg2
    cmp al, TOK_LINEREF
    je .bsl_arg2
    jmp .bsl_skip_args
.bsl_arg2:
    add si, 2
    jmp .bsl_skip_args

.bsl_listit:
    call bas_prog_first
    jc .bsl_done
.bsl_loop:
    mov ax, [bx + BLINE_OFF_LINENO]
    mov dx, ax
    call bas_lex_print_unsigned
    mov al, ' '
    call bas_lex_putc
    mov si, bx
    add si, BLINE_OFF_PAYLOAD
    call bas_detokenize_line
    call bas_crlf
    call bas_prog_next
    jnc .bsl_loop
.bsl_done:
    pop si
    pop cx
    pop bx
    pop ax
    ret


; =============================================================================
; bas_stmt_cls — clear screen via VGA buffer write.
; =============================================================================
bas_stmt_cls:
    push ax
    push cx
    push di
    push es
    mov ax, 0xB800
    mov es, ax
    xor di, di
    mov cx, 80 * 25
    mov ax, 0x0720                  ; space, light-grey on black
    rep stosw
    ; Reset cursor to top-left via BIOS
    mov ah, 0x02
    xor bh, bh
    xor dx, dx
    int 0x10
    pop es
    pop di
    pop cx
    pop ax
    ret


; =============================================================================
; bas_stmt_print — print arguments.  v1 subset:
;   bare PRINT          → CRLF
;   PRINT "str"         → print string
;   PRINT 123           → print integer
;   PRINT "a"; "b"      → "ab" (no separator)
;   PRINT "a", "b"      → "a" + spaces to next tab zone + "b"
;   PRINT "a"; 1; "b"   → "a 1 b"
;   trailing `;` or `,` → suppress final CRLF
;
; Anything else (variables, expressions) → syntax error in v1.
; =============================================================================
bas_stmt_print:
    push ax
    push bx
    push cx
    push dx
    push si
    mov cx, 1                       ; CX bit0 = need CRLF at end
.bsp_loop:
    mov al, [si]
    cmp al, TOK_EOL
    je .bsp_finish
    cmp al, ':'
    je .bsp_finish
    cmp al, ';'
    je .bsp_sep_semi
    cmp al, ','
    je .bsp_sep_comma
    cmp al, TOK_STR_LIT
    je .bsp_str
    ; Anything else: evaluate as expression
    call bas_expr_eval
    jc .bsp_fail
    ; Print signed integer + trailing space (GW PRINT prints leading space
    ; for positive numbers and trailing space; we just trailing-space).
    push dx
    test dx, dx
    jns .bsp_pos
    mov al, '-'
    call bas_lex_putc
    neg dx
    jmp .bsp_print_unum
.bsp_pos:
    mov al, ' '
    call bas_lex_putc
.bsp_print_unum:
    call bas_lex_print_unsigned
    pop dx
    mov al, ' '
    call bas_lex_putc
    mov cx, 1
    jmp .bsp_loop

.bsp_sep_semi:
    inc si
    mov cx, 0                       ; suppress final CRLF
    jmp .bsp_loop
.bsp_sep_comma:
    inc si
    mov cx, 0
    call .bsp_tab_to_zone
    jmp .bsp_loop
.bsp_str:
    inc si
    mov al, [si]
    inc si
    mov dl, al                      ; length
.bsp_str_loop:
    test dl, dl
    jz .bsp_after
    mov al, [si]
    inc si
    call bas_lex_putc
    dec dl
    jmp .bsp_str_loop
.bsp_after:
    mov cx, 1
    jmp .bsp_loop
.bsp_finish:
    test cx, cx
    jz .bsp_done
    call bas_crlf
.bsp_done:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret
.bsp_fail:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret

; Print spaces to advance to next 14-col tab zone (very simplified: we
; don't track column, so just emit BAS_TAB_ZONE spaces).
.bsp_tab_to_zone:
    push ax
    push cx
    mov cx, BAS_TAB_ZONE
.bspt_loop:
    mov al, ' '
    call bas_lex_putc
    loop .bspt_loop
    pop cx
    pop ax
    ret


; =============================================================================
; bas_run_program — execute program from first line.
; =============================================================================
bas_run_program:
    push ax
    push bx
    push cx
    push si
    cmp word [bas_prog_len], 0
    jne .brp_have
    mov al, BERR_NOPROGRAM
    call bas_error
.brp_have:
    mov word [bas_cur_line], 0
    call bas_run_program_from_cur
    pop si
    pop cx
    pop bx
    pop ax
    ret


; =============================================================================
; bas_break_check — poll Ctrl+C / Esc.  Drains key from BIOS buffer if hit.
; =============================================================================
bas_break_check:
    push ax
    mov ah, SYS_PEEK_KEY
    int 0x80
    jz .bbc_none                    ; ZF=1 → no key
    ; AL = ASCII; AH = scancode (don't care for v1)
    cmp al, 0x03
    je .bbc_break
    cmp al, 0x1B
    je .bbc_break
    ; Not a break key — leave it in the buffer for INPUT/INKEY$
    jmp .bbc_none
.bbc_break:
    ; Consume the key
    mov ah, SYS_READ_KEY
    int 0x80
    pop ax
    mov al, BERR_BREAK
    call bas_error
.bbc_none:
    pop ax
    ret


bas_help_text:
    db 'MNOS16 BASIC v1 commands:',13,10
    db '  PRINT expr[;,expr...] LET A=expr  REM',13,10
    db '  IF expr THEN stmt [ELSE stmt]   GOTO/GOSUB/RETURN n',13,10
    db '  FOR I=a TO b [STEP s]   NEXT [I]   WHILE expr/WEND',13,10
    db '  ON expr GOTO/GOSUB n,n,...   INPUT [prompt;] var',13,10
    db '  CLS  LOCATE r,c  COLOR f[,b]  POKE o,v  DEF SEG[=s]',13,10
    db '  RANDOMIZE [seed]  funcs: ABS SGN INT RND TIME PEEK',13,10
    db '  LIST  RUN  NEW  CLEAR  LOAD/SAVE "file.bas"  FILES',13,10
    db '  SYSTEM exits.  Ctrl+C / Esc breaks a running program.',13,10
    db 'Variables: A..Z[0..9] (numeric int).  Strings/arrays/files: TBD.',13,10
    db 0


; =============================================================================
; bas_stmt_let — LET A = expr  /  A = expr
;
; On entry SI may point at: TOK_LET-already-consumed → first token of varname,
; OR (implicit case) caller has set SI back to TOK_VAR_NAME byte.
; Either way we expect [si] == TOK_VAR_NAME for v1 (numeric scalar only).
; =============================================================================
bas_stmt_let:
    push bx
    push cx
    push dx
    mov al, [si]
    cmp al, TOK_VAR_NAME
    jne .bsl_syn
    inc si
    mov al, [si]                    ; name0
    mov ah, [si+1]                  ; name1
    add si, 2
    ; Save name for later assign
    mov [bas_scratch_b], ax
    ; Expect '='
    mov bl, [si]
    cmp bl, '='
    jne .bsl_syn
    inc si
    ; String variable assignment not supported in v1 expressions
    test al, 0x80
    jnz .bsl_typestr
    ; Eval expression
    call bas_expr_eval
    jc .bsl_propagate
    ; Assign DX into variable
    mov ax, [bas_scratch_b]
    call bas_var_set_int
    jc .bsl_propagate
    pop dx
    pop cx
    pop bx
    clc
    ret
.bsl_syn:
    pop dx
    pop cx
    pop bx
    mov al, BERR_SYNTAX
    stc
    ret
.bsl_typestr:
    pop dx
    pop cx
    pop bx
    mov al, BERR_TYPE
    stc
    ret
.bsl_propagate:
    pop dx
    pop cx
    pop bx
    stc
    ret


; =============================================================================
; bas_stmt_goto — GOTO line  (line is TOK_LINEREF + word)
; Sets bas_cur_line to target's offset, sets bas_jumped=1.
; In direct mode, kicks off bas_run_program from the target.
; =============================================================================
bas_stmt_goto:
    push bx
    mov al, [si]
    cmp al, TOK_LINEREF
    jne .bg_syn
    inc si
    mov ax, [si]
    add si, 2
    ; Find target line by number
    push ax
    call bas_prog_find              ; in AX → out SI = offset (or CF=1)
    jnc .bg_found
    pop ax
    pop bx
    mov al, BERR_UNDEF_LINE
    stc
    ret
.bg_found:
    add sp, 2                       ; discard saved AX
    ; SI was repurposed by bas_prog_find — it returned the offset there!
    ; Set bas_cur_line = SI (which is now the offset)
    mov [bas_cur_line], si
    mov word [bas_jumped], 1
    ; If we're already in RUN, just return — outer loop honors the jump.
    cmp byte [bas_in_run], 0
    jne .bg_done
    ; Direct-mode GOTO → start RUN at this target
    call bas_run_program_from_cur
.bg_done:
    pop bx
    clc
    ret
.bg_syn:
    pop bx
    mov al, BERR_SYNTAX
    stc
    ret


; =============================================================================
; bas_stmt_gosub — GOSUB line
; Pushes return point onto gosub stack, then jumps like GOTO.
; v1 RETURN point = "next line after current line" (no mid-line resume).
; =============================================================================
bas_stmt_gosub:
    push bx
    cmp word [bas_gs_top], BAS_GOSUB_DEPTH
    jae .bgs_over
    mov al, [si]
    cmp al, TOK_LINEREF
    jne .bgs_syn
    inc si
    mov ax, [si]
    add si, 2
    ; Push current line offset onto gosub stack.
    ; If in direct mode, store sentinel 0xFFFF so RETURN halts the run.
    mov bx, [bas_gs_top]
    shl bx, 2                       ; * 4 bytes per slot
    add bx, bas_gs_stack
    cmp byte [bas_in_run], 0
    jne .bgs_running
    mov word [bx], 0xFFFF
    jmp .bgs_stored_caller
.bgs_running:
    mov dx, [bas_cur_line]
    mov [bx], dx
.bgs_stored_caller:
    mov word [bx+2], 0              ; (statement offset, unused in v1)
    inc word [bas_gs_top]
    ; Jump to target
    push ax
    call bas_prog_find
    jnc .bgs_found
    pop ax
    ; Pop the gosub entry back
    dec word [bas_gs_top]
    pop bx
    mov al, BERR_UNDEF_LINE
    stc
    ret
.bgs_found:
    add sp, 2
    mov [bas_cur_line], si
    mov word [bas_jumped], 1
    cmp byte [bas_in_run], 0
    jne .bgs_done
    call bas_run_program_from_cur
.bgs_done:
    pop bx
    clc
    ret
.bgs_syn:
    pop bx
    mov al, BERR_SYNTAX
    stc
    ret
.bgs_over:
    pop bx
    mov al, BERR_GOSUBOVER
    stc
    ret


; =============================================================================
; bas_stmt_return — RETURN
; Pops return point off gosub stack, sets bas_cur_line, sets bas_jumped=1
; AFTER advancing to the next line (since v1 returns to line-after-call).
; =============================================================================
bas_stmt_return:
    push bx
    cmp word [bas_gs_top], 0
    jbe .bsr_under
    dec word [bas_gs_top]
    mov bx, [bas_gs_top]
    shl bx, 2
    add bx, bas_gs_stack
    mov dx, [bx]                    ; DX = caller's line offset (or 0xFFFF sentinel)
    cmp dx, 0xFFFF
    je .bsr_to_direct
    ; Find next line after caller's line
    mov bx, BAS_PROG_BASE
    add bx, dx
    mov al, [bx + BLINE_OFF_LEN]
    xor ah, ah
    add ax, BLINE_HDR_SIZE
    add bx, ax
    ; Check if past end-of-prog → stop RUN
    mov ax, [bas_prog_len]
    add ax, BAS_PROG_BASE
    cmp bx, ax
    jb .bsr_have
.bsr_to_direct:
    mov byte [bas_in_run], 0
    pop bx
    clc
    ret
.bsr_have:
    sub bx, BAS_PROG_BASE
    mov [bas_cur_line], bx
    mov word [bas_jumped], 1
    pop bx
    clc
    ret
.bsr_under:
    pop bx
    mov al, BERR_RETWITHOUT
    stc
    ret


; =============================================================================
; bas_stmt_if — IF expr THEN { stmt | linenum } [ELSE { stmt | linenum }]
; v1: THEN body and ELSE body are each ONE statement OR ONE line number.
; =============================================================================
bas_stmt_if:
    push bx
    push cx
    push dx
    ; Eval condition
    call bas_expr_eval
    jc .bsi_propagate
    ; Expect THEN
    mov al, [si]
    cmp al, TOK_THEN
    jne .bsi_syn
    inc si
    ; If true → run THEN branch; else skip past it to ELSE or EOL
    test dx, dx
    jnz .bsi_then_true
    ; --- False: skip THEN body (one stmt or one linenum) up to ELSE/EOL ---
.bsi_skip_then:
    mov al, [si]
    cmp al, TOK_EOL
    je .bsi_done_pop
    cmp al, TOK_ELSE
    je .bsi_run_else
    cmp al, ':'
    je .bsi_done_pop
    inc si
    cmp al, TOK_INT_LIT
    je .bsi_skip2_then
    cmp al, TOK_LINEREF
    je .bsi_skip2_then
    cmp al, TOK_STR_LIT
    je .bsi_skip_str_then
    cmp al, TOK_VAR_NAME
    je .bsi_skip2_then
    jmp .bsi_skip_then
.bsi_skip2_then:
    add si, 2
    jmp .bsi_skip_then
.bsi_skip_str_then:
    mov al, [si]
    inc si
    movzx ax, al
    add si, ax
    jmp .bsi_skip_then
.bsi_run_else:
    inc si                          ; past TOK_ELSE
    call .bsi_run_branch
    jmp .bsi_done_pop

.bsi_then_true:
    ; Either next is TOK_LINEREF (= GOTO shorthand) or a statement
    mov al, [si]
    cmp al, TOK_LINEREF
    jne .bsi_then_stmt
    ; THEN <linenum> → implicit GOTO
    inc si
    mov ax, [si]
    add si, 2
    push ax
    call bas_prog_find
    jnc .bsi_then_found
    pop ax
    mov al, BERR_UNDEF_LINE
    jmp .bsi_propagate
.bsi_then_found:
    add sp, 2
    mov [bas_cur_line], si
    mov word [bas_jumped], 1
    cmp byte [bas_in_run], 0
    jne .bsi_done_pop
    call bas_run_program_from_cur
    jmp .bsi_done_pop
.bsi_then_stmt:
    call .bsi_run_branch
    ; After running THEN, skip rest of line (no ELSE handling when THEN ran)
.bsi_done_pop:
    pop dx
    pop cx
    pop bx
    ; Advance SI to end-of-line
.bsi_consume_eol:
    mov al, [si]
    cmp al, TOK_EOL
    je .bsi_done
    inc si
    cmp al, TOK_INT_LIT
    je .bsi_consume2
    cmp al, TOK_LINEREF
    je .bsi_consume2
    cmp al, TOK_VAR_NAME
    je .bsi_consume2
    cmp al, TOK_STR_LIT
    je .bsi_consume_str
    jmp .bsi_consume_eol
.bsi_consume2:
    add si, 2
    jmp .bsi_consume_eol
.bsi_consume_str:
    mov al, [si]
    inc si
    movzx ax, al
    add si, ax
    jmp .bsi_consume_eol
.bsi_done:
    clc
    ret
.bsi_syn:
    pop dx
    pop cx
    pop bx
    mov al, BERR_SYNTAX
    stc
    ret
.bsi_propagate:
    pop dx
    pop cx
    pop bx
    stc
    ret

; .bsi_run_branch — run ONE statement at SI (recursive call into dispatcher)
.bsi_run_branch:
    ; If next token is a line ref (e.g., ELSE 200) → implicit GOTO
    mov al, [si]
    cmp al, TOK_LINEREF
    jne .bsi_rb_stmt
    inc si
    mov ax, [si]
    add si, 2
    push ax
    call bas_prog_find
    jnc .bsi_rb_found
    pop ax
    mov al, BERR_UNDEF_LINE
    stc
    ret
.bsi_rb_found:
    add sp, 2
    mov [bas_cur_line], si
    mov word [bas_jumped], 1
    cmp byte [bas_in_run], 0
    jne .bsi_rb_ok
    call bas_run_program_from_cur
.bsi_rb_ok:
    clc
    ret
.bsi_rb_stmt:
    call bas_run_direct             ; dispatch one statement (will skip rest of stmts via colon)
    clc
    ret


; =============================================================================
; bas_run_program_from_cur — execute program starting at bas_cur_line.
; Used by GOTO / GOSUB in direct mode.
; =============================================================================
bas_run_program_from_cur:
    push ax
    push bx
    push cx
    push si
    mov byte [bas_in_run], 1
    call bas_prog_rebuild_index
.brpc_loop:
    cmp byte [bas_in_run], 0
    je .brpc_done
    ; Resolve current line address
    mov bx, [bas_cur_line]
    add bx, BAS_PROG_BASE
    ; Bounds check
    mov ax, [bas_prog_len]
    add ax, BAS_PROG_BASE
    cmp bx, ax
    jae .brpc_done
    call bas_break_check
    ; Precompute next line offset (for non-jumping advance)
    mov al, [bx + BLINE_OFF_LEN]
    xor ah, ah
    add ax, BLINE_HDR_SIZE
    ; Save next offset in scratch
    mov dx, [bas_cur_line]
    add dx, ax
    mov [bas_scratch_d], dx
    mov word [bas_jumped], 0
    ; Execute
    mov si, bx
    add si, BLINE_OFF_PAYLOAD
    call bas_run_direct
    ; Advance
    cmp word [bas_jumped], 0
    jne .brpc_loop                  ; jump occurred — bas_cur_line is target
    mov dx, [bas_scratch_d]
    mov [bas_cur_line], dx
    jmp .brpc_loop
.brpc_done:
    mov byte [bas_in_run], 0
    pop si
    pop cx
    pop bx
    pop ax
    ret


; =============================================================================
; bas_stmt_for — FOR var = start TO limit [STEP step]
; v1: FOR must be the last statement on its line (body starts at next line).
; v1: only allowed during RUN (in_run=1).
; =============================================================================
bas_stmt_for:
    push bx
    push cx
    push dx
    cmp byte [bas_in_run], 0
    je .bsf_syn                     ; FOR in direct mode = syntax for v1
    ; Expect TOK_VAR_NAME
    mov al, [si]
    cmp al, TOK_VAR_NAME
    jne .bsf_syn
    inc si
    mov al, [si]
    mov ah, [si+1]
    add si, 2
    ; String var not allowed
    test al, 0x80
    jnz .bsf_type
    mov [bas_scratch_b], ax         ; save packed name
    ; Expect '='
    mov bl, [si]
    cmp bl, '='
    jne .bsf_syn
    inc si
    ; Eval start expression
    call bas_expr_eval
    jc .bsf_prop
    ; Assign to var
    mov ax, [bas_scratch_b]
    call bas_var_set_int
    jc .bsf_prop
    ; Expect TOK_TO
    mov al, [si]
    cmp al, TOK_TO
    jne .bsf_syn
    inc si
    ; Eval limit
    call bas_expr_eval
    jc .bsf_prop
    mov [bas_scratch_c], dx         ; save limit
    ; Optional TOK_STEP
    mov al, [si]
    cmp al, TOK_STEP
    je .bsf_has_step
    mov ax, 1                       ; default step = 1
    jmp .bsf_no_step
.bsf_has_step:
    inc si
    call bas_expr_eval
    jc .bsf_prop
    mov ax, dx
.bsf_no_step:
    ; AX = step
    ; Check FOR stack depth
    cmp word [bas_for_top], BAS_FOR_DEPTH
    jae .bsf_over
    ; Push frame: 16 bytes per slot
    mov bx, [bas_for_top]
    shl bx, 4                       ; * 16
    add bx, bas_for_stack
    mov dx, [bas_scratch_b]
    mov [bx + 0], dx                ; var name
    mov dx, [bas_scratch_c]
    mov [bx + 2], dx                ; limit
    mov [bx + 4], ax                ; step
    ; body_line = next line offset (after FOR's line)
    push bx
    mov bx, [bas_cur_line]
    add bx, BAS_PROG_BASE
    mov al, [bx + BLINE_OFF_LEN]
    xor ah, ah
    add ax, BLINE_HDR_SIZE
    mov dx, [bas_cur_line]
    add dx, ax                      ; DX = body line offset
    pop bx
    mov [bx + 6], dx
    inc word [bas_for_top]
    pop dx
    pop cx
    pop bx
    clc
    ret
.bsf_syn:
    pop dx
    pop cx
    pop bx
    mov al, BERR_SYNTAX
    stc
    ret
.bsf_type:
    pop dx
    pop cx
    pop bx
    mov al, BERR_TYPE
    stc
    ret
.bsf_over:
    pop dx
    pop cx
    pop bx
    mov al, BERR_FOROVER
    stc
    ret
.bsf_prop:
    pop dx
    pop cx
    pop bx
    stc
    ret


; =============================================================================
; bas_stmt_next — NEXT [var]
; Peeks top FOR frame, adds step to var, compares to limit. If past limit,
; pops frame and falls through (continues past NEXT). Otherwise jumps to
; body line.
; =============================================================================
bas_stmt_next:
    push bx
    push cx
    push dx
    cmp word [bas_for_top], 0
    jbe .bsn_under
    ; Optional var name
    mov al, [si]
    cmp al, TOK_VAR_NAME
    jne .bsn_no_var
    inc si
    add si, 2                       ; skip var name (we don't enforce match in v1)
.bsn_no_var:
    ; Peek top frame
    mov bx, [bas_for_top]
    dec bx
    shl bx, 4
    add bx, bas_for_stack
    ; Load var name; fetch current var value
    mov ax, [bx + 0]
    push bx
    call bas_var_get_int            ; returns value in BX, CF=0 on success
    mov dx, bx                      ; capture value before pop restores frame addr
    pop bx
    jc .bsn_prop
    ; Add step
    mov cx, [bx + 4]                ; step
    add dx, cx
    ; Write back
    mov ax, [bx + 0]
    push bx
    call bas_var_set_int
    pop bx
    jc .bsn_prop
    ; Compare to limit (signed). step>=0 => loop while var<=limit; step<0 => while var>=limit
    mov ax, [bx + 2]                ; limit (DX is current value)
    test cx, cx
    js .bsn_neg
    cmp dx, ax                      ; var - limit
    jg .bsn_done_pop                ; var > limit → end loop
    jmp .bsn_jump_back
.bsn_neg:
    cmp dx, ax
    jl .bsn_done_pop                ; var < limit → end loop
.bsn_jump_back:
    ; Jump back to body line
    mov dx, [bx + 6]
    mov [bas_cur_line], dx
    mov word [bas_jumped], 1
    pop dx
    pop cx
    pop bx
    clc
    ret
.bsn_done_pop:
    dec word [bas_for_top]
    pop dx
    pop cx
    pop bx
    clc
    ret
.bsn_under:
    pop dx
    pop cx
    pop bx
    mov al, BERR_NEXTWITHOUT
    stc
    ret
.bsn_prop:
    pop dx
    pop cx
    pop bx
    stc
    ret


; =============================================================================
; bas_stmt_while — WHILE expr
; If true: push frame (cur_line offset) and continue.
; If false: scan forward to matching WEND, set cur_line = line after WEND.
; =============================================================================
bas_stmt_while:
    push bx
    push cx
    push dx
    call bas_expr_eval
    jc .bsw_prop
    test dx, dx
    jz .bsw_false
    ; True: push frame
    cmp word [bas_wh_top], BAS_WHILE_DEPTH
    jae .bsw_over
    mov bx, [bas_wh_top]
    shl bx, 2
    add bx, bas_wh_stack
    mov dx, [bas_cur_line]
    mov [bx + 0], dx
    mov word [bx + 2], 0
    inc word [bas_wh_top]
    pop dx
    pop cx
    pop bx
    clc
    ret
.bsw_false:
    ; Scan forward to matching WEND
    call bas_find_matching_wend
    jc .bsw_no_wend
    ; SI now points to the line offset of WEND. Set bas_cur_line = line AFTER WEND.
    push bx
    mov bx, BAS_PROG_BASE
    add bx, si                      ; bx = WEND line addr
    mov al, [bx + BLINE_OFF_LEN]
    xor ah, ah
    add ax, BLINE_HDR_SIZE
    add si, ax
    pop bx
    mov [bas_cur_line], si
    mov word [bas_jumped], 1
    pop dx
    pop cx
    pop bx
    clc
    ret
.bsw_no_wend:
    pop dx
    pop cx
    pop bx
    mov al, BERR_WENDWITHOUT
    stc
    ret
.bsw_over:
    pop dx
    pop cx
    pop bx
    mov al, BERR_WHILEOVER
    stc
    ret
.bsw_prop:
    pop dx
    pop cx
    pop bx
    stc
    ret


; =============================================================================
; bas_stmt_wend — pop top WHILE frame, jump back to its WHILE line
; =============================================================================
bas_stmt_wend:
    push bx
    cmp word [bas_wh_top], 0
    jbe .bswe_under
    dec word [bas_wh_top]
    mov bx, [bas_wh_top]
    shl bx, 2
    add bx, bas_wh_stack
    mov dx, [bx + 0]
    mov [bas_cur_line], dx
    mov word [bas_jumped], 1
    pop bx
    clc
    ret
.bswe_under:
    pop bx
    mov al, BERR_WENDWITHOUT
    stc
    ret


; =============================================================================
; bas_stmt_on — ON expr GOTO|GOSUB line[,line...]
; =============================================================================
bas_stmt_on:
    push bx
    push cx
    push dx
    call bas_expr_eval
    jc .bso_prop
    mov [bas_scratch_b], dx         ; save selector
    ; Expect GOTO or GOSUB
    mov al, [si]
    inc si
    cmp al, TOK_GOTO
    je .bso_goto_mode
    cmp al, TOK_GOSUB
    je .bso_gosub_mode
    jmp .bso_syn
.bso_goto_mode:
    xor cx, cx                      ; CX bit0 = 0 → GOTO mode
    jmp .bso_walk
.bso_gosub_mode:
    mov cx, 1                       ; GOSUB mode
.bso_walk:
    ; Walk comma-separated line refs, decrement selector each time.
    ; When selector hits 1, take that one.
    mov dx, [bas_scratch_b]
    ; If selector <= 0 → no jump (fall through)
    test dx, dx
    jle .bso_skip_rest
.bso_each:
    mov al, [si]
    cmp al, TOK_LINEREF
    jne .bso_syn
    inc si
    mov ax, [si]
    add si, 2
    ; If selector == 1, this is our target
    cmp dx, 1
    je .bso_take
    ; else skip and continue
    dec dx
    mov al, [si]
    cmp al, ','
    jne .bso_skip_rest              ; ran out — selector beyond list, fall through
    inc si
    jmp .bso_each
.bso_take:
    ; AX = line number; CX bit0 = mode (0=GOTO, 1=GOSUB)
    test cx, cx
    jnz .bso_do_gosub
    ; GOTO
    push ax
    call bas_prog_find
    jc .bso_undef
    add sp, 2
    mov [bas_cur_line], si
    mov word [bas_jumped], 1
    pop dx
    pop cx
    pop bx
    clc
    ret
.bso_do_gosub:
    ; Push gosub frame, then jump
    cmp word [bas_gs_top], BAS_GOSUB_DEPTH
    jae .bso_gsover
    push ax
    mov bx, [bas_gs_top]
    shl bx, 2
    add bx, bas_gs_stack
    cmp byte [bas_in_run], 0
    jne .bso_gs_run
    mov word [bx], 0xFFFF
    jmp .bso_gs_stored
.bso_gs_run:
    mov dx, [bas_cur_line]
    mov [bx], dx
.bso_gs_stored:
    mov word [bx+2], 0
    inc word [bas_gs_top]
    pop ax
    push ax
    call bas_prog_find
    jc .bso_undef_pop_gs
    add sp, 2
    mov [bas_cur_line], si
    mov word [bas_jumped], 1
    pop dx
    pop cx
    pop bx
    clc
    ret
.bso_undef_pop_gs:
    dec word [bas_gs_top]
    add sp, 2
    pop dx
    pop cx
    pop bx
    mov al, BERR_UNDEF_LINE
    stc
    ret
.bso_skip_rest:
    ; Consume remaining line refs, fall through to "no jump"
.bso_skip_loop:
    mov al, [si]
    cmp al, TOK_EOL
    je .bso_no_jump
    cmp al, ':'
    je .bso_no_jump
    cmp al, ','
    je .bso_skip_inc
    cmp al, TOK_LINEREF
    je .bso_skip_lr
    inc si
    jmp .bso_skip_loop
.bso_skip_lr:
    inc si
    add si, 2
    jmp .bso_skip_loop
.bso_skip_inc:
    inc si
    jmp .bso_skip_loop
.bso_no_jump:
    pop dx
    pop cx
    pop bx
    clc
    ret
.bso_undef:
    add sp, 2
    pop dx
    pop cx
    pop bx
    mov al, BERR_UNDEF_LINE
    stc
    ret
.bso_gsover:
    pop dx
    pop cx
    pop bx
    mov al, BERR_GOSUBOVER
    stc
    ret
.bso_syn:
    pop dx
    pop cx
    pop bx
    mov al, BERR_SYNTAX
    stc
    ret
.bso_prop:
    pop dx
    pop cx
    pop bx
    stc
    ret


; =============================================================================
; bas_stmt_input — INPUT [prompt; ] var
; v1: one numeric variable only. Prints prompt (or "? "), reads line,
; parses signed decimal, assigns to var.
; =============================================================================
bas_stmt_input:
    push bx
    push cx
    push dx
    push di
    ; Optional prompt string + ';' or ','
    mov al, [si]
    cmp al, TOK_STR_LIT
    jne .bsi2_no_prompt
    inc si
    mov al, [si]
    inc si
    movzx cx, al
.bsi2_pp_loop:
    test cx, cx
    jz .bsi2_pp_done
    mov al, [si]
    inc si
    call bas_lex_putc
    dec cx
    jmp .bsi2_pp_loop
.bsi2_pp_done:
    ; Expect ';' or ','
    mov al, [si]
    cmp al, ';'
    je .bsi2_prompt_sep
    cmp al, ','
    je .bsi2_prompt_sep
    jmp .bsi2_syn
.bsi2_prompt_sep:
    inc si
.bsi2_no_prompt:
    ; Print "? "
    mov al, '?'
    call bas_lex_putc
    mov al, ' '
    call bas_lex_putc
    ; Expect TOK_VAR_NAME
    mov al, [si]
    cmp al, TOK_VAR_NAME
    jne .bsi2_syn
    inc si
    mov al, [si]
    mov ah, [si+1]
    add si, 2
    ; v1: numeric only
    test al, 0x80
    jnz .bsi2_type
    mov [bas_scratch_b], ax         ; save var name
    ; Read a line
    call bas_readline               ; fills bas_line_buf
    call bas_crlf
    ; Parse signed integer from buffer
    mov si, bas_line_buf
    call bas_parse_int              ; → DX = value, CF=1 on error
    jc .bsi2_inpterr
    mov ax, [bas_scratch_b]
    call bas_var_set_int
    jc .bsi2_prop
    pop di
    pop dx
    pop cx
    pop bx
    clc
    ret
.bsi2_syn:
    pop di
    pop dx
    pop cx
    pop bx
    mov al, BERR_SYNTAX
    stc
    ret
.bsi2_type:
    pop di
    pop dx
    pop cx
    pop bx
    mov al, BERR_TYPE
    stc
    ret
.bsi2_inpterr:
    pop di
    pop dx
    pop cx
    pop bx
    mov al, BERR_INPUTERR
    stc
    ret
.bsi2_prop:
    pop di
    pop dx
    pop cx
    pop bx
    stc
    ret


; =============================================================================
; bas_parse_int — parse signed decimal from DS:SI. Skips leading spaces.
; Returns DX = value, CF=1 if empty or invalid.
; =============================================================================
bas_parse_int:
    push ax
    push bx
    push cx
    xor dx, dx
    xor cx, cx                      ; CX bit0 = negative flag
.bpi_skip_ws:
    mov al, [si]
    cmp al, ' '
    je .bpi_skip
    cmp al, 9
    je .bpi_skip
    jmp .bpi_sign
.bpi_skip:
    inc si
    jmp .bpi_skip_ws
.bpi_sign:
    mov al, [si]
    cmp al, '-'
    jne .bpi_pos_sign
    inc si
    mov cx, 1
    jmp .bpi_digit
.bpi_pos_sign:
    cmp al, '+'
    jne .bpi_digit
    inc si
.bpi_digit:
    xor bx, bx                      ; BX = digit count
.bpi_loop:
    mov al, [si]
    cmp al, '0'
    jb .bpi_done
    cmp al, '9'
    ja .bpi_done
    sub al, '0'
    push ax
    mov ax, dx
    mov dx, 10
    imul dx
    mov dx, ax
    pop ax
    xor ah, ah
    add dx, ax
    inc si
    inc bx
    jmp .bpi_loop
.bpi_done:
    test bx, bx
    jz .bpi_err
    test cx, cx
    jz .bpi_ok
    neg dx
.bpi_ok:
    pop cx
    pop bx
    pop ax
    clc
    ret
.bpi_err:
    pop cx
    pop bx
    pop ax
    stc
    ret


; =============================================================================
; bas_find_matching_wend — scan forward from current line for matching WEND.
;
; In:  bas_cur_line = current WHILE line offset
; Out: CF=0 → SI = WEND line offset (relative to BAS_PROG_BASE)
;      CF=1 → no matching WEND found
;
; Tracks nested WHILE/WEND depth.
; =============================================================================
bas_find_matching_wend:
    push ax
    push bx
    push cx
    push dx
    ; Start at line after current
    mov bx, [bas_cur_line]
    add bx, BAS_PROG_BASE
    mov al, [bx + BLINE_OFF_LEN]
    xor ah, ah
    add ax, BLINE_HDR_SIZE
    add bx, ax                      ; BX = start scan addr (next line)
    mov cx, [bas_prog_len]
    add cx, BAS_PROG_BASE           ; CX = end of program
    mov dx, 1                       ; depth (we're inside one WHILE)
.bfmw_lineloop:
    cmp bx, cx
    jae .bfmw_notfound
    ; Walk payload tokens of this line
    push bx
    push cx
    mov al, [bx + BLINE_OFF_LEN]
    xor ah, ah
    mov cx, ax                      ; CX = payload length
    add bx, BLINE_OFF_PAYLOAD       ; BX = start of payload
.bfmw_tokloop:
    test cx, cx
    jz .bfmw_endpay
    mov al, [bx]
    cmp al, TOK_WHILE
    jne .bfmw_not_while
    inc dx
    inc bx
    dec cx
    jmp .bfmw_tokloop
.bfmw_not_while:
    cmp al, TOK_WEND
    jne .bfmw_other
    dec dx
    jnz .bfmw_other_skip            ; not matching ours
    ; Found it!
    pop cx
    pop bx
    mov si, bx
    sub si, BAS_PROG_BASE
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret
.bfmw_other_skip:
    inc bx
    dec cx
    jmp .bfmw_tokloop
.bfmw_other:
    ; Skip token + operands
    inc bx
    dec cx
    cmp al, TOK_INT_LIT
    je .bfmw_skip2
    cmp al, TOK_LINEREF
    je .bfmw_skip2
    cmp al, TOK_VAR_NAME
    je .bfmw_skip2
    cmp al, TOK_STR_LIT
    je .bfmw_skipstr
    jmp .bfmw_tokloop
.bfmw_skip2:
    test cx, cx
    jz .bfmw_endpay
    add bx, 2
    sub cx, 2
    js .bfmw_endpay
    jmp .bfmw_tokloop
.bfmw_skipstr:
    test cx, cx
    jz .bfmw_endpay
    mov al, [bx]
    inc bx
    dec cx
    movzx ax, al
    add bx, ax
    sub cx, ax
    js .bfmw_endpay
    jmp .bfmw_tokloop
.bfmw_endpay:
    pop cx
    pop bx
    ; Advance to next line
    mov al, [bx + BLINE_OFF_LEN]
    xor ah, ah
    add ax, BLINE_HDR_SIZE
    add bx, ax
    jmp .bfmw_lineloop
.bfmw_notfound:
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret


; =============================================================================
; bas_stmt_locate — LOCATE row, col   (1-based; v1: both required)
; =============================================================================
bas_stmt_locate:
    push ax
    push bx
    push cx
    push dx
    call bas_expr_eval
    jc .bsloc_prop
    mov [bas_scratch_b], dx         ; row
    mov al, [si]
    cmp al, ','
    jne .bsloc_syn
    inc si
    call bas_expr_eval
    jc .bsloc_prop
    ; DX = col, [bas_scratch_b] = row
    mov bx, [bas_scratch_b]
    dec bx                          ; 0-based row
    dec dx                          ; 0-based col
    ; Set cursor via BIOS INT 0x10 AH=02
    mov ah, 0x02
    xor bh, bh
    mov dh, bl                      ; row
    ; dl already = col
    int 0x10
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret
.bsloc_syn:
    pop dx
    pop cx
    pop bx
    pop ax
    mov al, BERR_SYNTAX
    stc
    ret
.bsloc_prop:
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret


; =============================================================================
; bas_stmt_color — COLOR fg [, bg]   (0..15 each)
; =============================================================================
bas_stmt_color:
    push ax
    push bx
    push cx
    push dx
    call bas_expr_eval
    jc .bscol_prop
    mov [bas_scratch_b], dx         ; fg
    mov ax, 7                       ; default bg = light grey
    mov [bas_scratch_c], ax
    mov al, [si]
    cmp al, ','
    jne .bscol_apply
    inc si
    call bas_expr_eval
    jc .bscol_prop
    mov [bas_scratch_c], dx
.bscol_apply:
    ; Read current cursor attr from B800 byte 1, set to (bg<<4)|(fg&0x0F)
    mov ax, [bas_scratch_c]
    and al, 0x0F
    shl al, 4
    mov bl, al                      ; bg << 4
    mov ax, [bas_scratch_b]
    and al, 0x0F
    or al, bl
    ; Store as the "current attr" — BASIC has no global attr, so we
    ; just set the attribute byte in subsequent character writes via
    ; INT 10h AH=09 in PRINT... actually PRINT uses SYS_PRINT_CHAR.
    ; v1: store globally; not yet wired through PRINT. Documented
    ; behaviour: COLOR sets the BIOS write-char attr but PRINT may
    ; not honour it until v2.
    mov [bas_color_attr], al
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret
.bscol_prop:
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret


; =============================================================================
; bas_stmt_poke — POKE offset, value   (writes byte at [DEF_SEG : offset])
; =============================================================================
bas_stmt_poke:
    push ax
    push bx
    push cx
    push dx
    push es
    call bas_expr_eval
    jc .bspoke_prop
    mov [bas_scratch_b], dx
    mov al, [si]
    cmp al, ','
    jne .bspoke_syn
    inc si
    call bas_expr_eval
    jc .bspoke_prop
    mov ax, [bas_def_seg]
    mov es, ax
    mov bx, [bas_scratch_b]
    mov [es:bx], dl
    pop es
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret
.bspoke_syn:
    pop es
    pop dx
    pop cx
    pop bx
    pop ax
    mov al, BERR_SYNTAX
    stc
    ret
.bspoke_prop:
    pop es
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret


; =============================================================================
; bas_stmt_def_seg — DEF SEG [= expr]
; If no '=' present, reset to default (TPA segment from CS).
; =============================================================================
bas_stmt_def_seg:
    push ax
    push dx
    ; Expect TOK_SEG
    mov al, [si]
    cmp al, TOK_SEG
    jne .bsdef_syn
    inc si
    mov al, [si]
    cmp al, '='
    jne .bsdef_reset
    inc si
    call bas_expr_eval
    jc .bsdef_prop
    mov [bas_def_seg], dx
    pop dx
    pop ax
    clc
    ret
.bsdef_reset:
    mov ax, cs
    mov [bas_def_seg], ax
    pop dx
    pop ax
    clc
    ret
.bsdef_syn:
    pop dx
    pop ax
    mov al, BERR_SYNTAX
    stc
    ret
.bsdef_prop:
    pop dx
    pop ax
    stc
    ret


; =============================================================================
; bas_stmt_randomize — RANDOMIZE [seed]
; If no operand: use TIME tick as seed.
; =============================================================================
bas_stmt_randomize:
    push ax
    push dx
    push es
    mov al, [si]
    cmp al, TOK_EOL
    je .bsrnd_use_time
    cmp al, ':'
    je .bsrnd_use_time
    call bas_expr_eval
    jc .bsrnd_prop
    mov [bas_rnd_seed], dx
    pop es
    pop dx
    pop ax
    clc
    ret
.bsrnd_use_time:
    mov ax, 0x0040
    mov es, ax
    mov dx, [es:0x006C]
    mov [bas_rnd_seed], dx
    pop es
    pop dx
    pop ax
    clc
    ret
.bsrnd_prop:
    pop es
    pop dx
    pop ax
    stc
    ret
