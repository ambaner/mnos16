; =============================================================================
; basic_expr.inc — Typed recursive-descent expression evaluator (v2).
;
; v2 grammar (numeric + string):
;   expr     := or_expr
;   or_expr  := and_expr (OR and_expr)*                  ; ints only
;   and_expr := not_expr (AND not_expr)*                 ; ints only
;   not_expr := [NOT] cmp_expr                           ; ints only
;   cmp_expr := add_expr ((= | <> | < | > | <= | >=) add_expr)*
;                                                        ; ints OR strings
;   add_expr := mul_expr ((+|-) mul_expr)*               ; + does str concat
;   mul_expr := pow_expr ((*|/|MOD) pow_expr)*           ; ints only
;   pow_expr := unary (^ pow_expr)?                      ; ints only
;   unary    := [-|+] primary                            ; ints only
;   primary  := int_literal | str_literal | variable
;             | string_variable | '(' expr ')' | fn_call
;
; Public:
;   bas_expr_eval     In : DS:SI = token cursor (statement-local)
;                     Out: bas_expr_result populated (typed)
;                          DX = int value if type='N' (legacy fast path)
;                          SI advanced past expression
;                          CF=0 ok; CF=1 → AL = BERR_*
;
;   bas_expr_eval_int Same as above but errors BERR_TYPE if result is a
;                     string.  Use from callers that strictly need an int.
;
; All comparisons return -1 (true) / 0 (false), per GW-BASIC.  Division by
; zero → BERR_DIVZERO.  Arithmetic uses signed 16-bit; overflow wraps silently.
;
; Recursion depth is tracked in bas_ex_top (1-byte counter) and capped at
; BAS_EXPR_DEPTH (16); deeper → BERR_TOOCOMPLEX.
;
; Type-flow invariant: bas_expr_result holds the latest sub-expression's
; type.  Numeric sub-expressions update val_lo via bas_expr_eval's final
; sync step (skip per-op stores to keep code small).  String sub-expressions
; (TOK_STR_LIT, string var, string concat, string fn result) set
; bas_expr_result FULLY before returning to the next layer up.
; =============================================================================

; @returns cf
bas_expr_eval:
    mov word [bas_ex_top], 0
    mov byte [bas_expr_result + BES_TYPE], BVAR_NUMERIC
    mov byte [bas_expr_result + BES_FLAGS], 0
    call bas_expr_or
    jc .bee_done                    ; CF=1: propagate failure (AL = error code)
    cmp byte [bas_expr_result + BES_TYPE], BVAR_STRING
    je .bee_clear_cf                ; STRING: skip DX→VAL_LO write, then clear CF
    mov [bas_expr_result + BES_VAL_LO], dx
.bee_clear_cf:
    BAS_RET_OK                      ; success: forces CF=0 (bug #1 was a leak here)
.bee_done:
    ret                             ; CF already set (=1, propagated from callee)

; @returns cf
bas_expr_eval_int:
    call bas_expr_eval
    jc .bei_done                    ; CF=1: propagate failure (AL = error code)
    cmp byte [bas_expr_result + BES_TYPE], BVAR_STRING
    jne .bei_ok                     ; not a string → numeric success
    BAS_RET_ERR BERR_TYPE           ; string in int context → type-mismatch
.bei_ok:
    BAS_RET_OK                      ; clears CF leaked from the cmp above
.bei_done:
    ret                             ; CF already set (=1)

; Helper: returns CF=1 / AL=BERR_TYPE if bas_expr_result currently holds
; a string (used by operators that don't accept strings).
; @returns cf
bas_expr_require_int:
    cmp byte [bas_expr_result + BES_TYPE], BVAR_STRING
    je .berqi_bad
    BAS_RET_OK
.berqi_bad:
    BAS_RET_ERR BERR_TYPE


; --- or_expr ---------------------------------------------------------------
bas_expr_or:
    call bas_expr_depth_inc
    jc .be_or_complex
    call bas_expr_and
    jc .be_or_fail
.be_or_loop:
    mov al, [si]
    cmp al, TOK_OR
    jne .be_or_done
    call bas_expr_require_int
    jc .be_or_fail
    inc si
    push dx                         ; save LHS
    call bas_expr_and
    jc .be_or_fail_pop
    call bas_expr_require_int
    jc .be_or_fail_pop
    pop ax
    or  dx, ax                      ; DX = LHS | RHS (bitwise)
    jmp .be_or_loop
.be_or_done:
    call bas_expr_depth_dec
    clc
    ret
.be_or_fail_pop:
    pop ax
.be_or_fail:
    call bas_expr_depth_dec
    stc
    ret
.be_or_complex:
    mov al, BERR_TOOCOMPLEX
    stc
    ret


; --- and_expr --------------------------------------------------------------
bas_expr_and:
    call bas_expr_depth_inc
    jc .be_and_complex
    call bas_expr_not
    jc .be_and_fail
.be_and_loop:
    mov al, [si]
    cmp al, TOK_AND
    jne .be_and_done
    call bas_expr_require_int
    jc .be_and_fail
    inc si
    push dx
    call bas_expr_not
    jc .be_and_fail_pop
    call bas_expr_require_int
    jc .be_and_fail_pop
    pop ax
    and dx, ax
    jmp .be_and_loop
.be_and_done:
    call bas_expr_depth_dec
    clc
    ret
.be_and_fail_pop:
    pop ax
.be_and_fail:
    call bas_expr_depth_dec
    stc
    ret
.be_and_complex:
    mov al, BERR_TOOCOMPLEX
    stc
    ret


; --- not_expr --------------------------------------------------------------
bas_expr_not:
    call bas_expr_depth_inc
    jc .be_not_complex
    mov al, [si]
    cmp al, TOK_NOT
    jne .be_not_pass
    inc si
    call bas_expr_cmp
    jc .be_not_fail
    call bas_expr_require_int
    jc .be_not_fail
    not dx                          ; bitwise NOT (matches GW for ints)
    call bas_expr_depth_dec
    clc
    ret
.be_not_pass:
    call bas_expr_cmp
    jc .be_not_fail
    call bas_expr_depth_dec
    clc
    ret
.be_not_fail:
    call bas_expr_depth_dec
    stc
    ret
.be_not_complex:
    mov al, BERR_TOOCOMPLEX
    stc
    ret


; --- cmp_expr (single comparison; chains return -1/0) ---------------------
; Handles both numeric (=,<>,<,>,<=,>=) and string compare (same operators).
; Both operands must have the same type; mixed → BERR_TYPE.
bas_expr_cmp:
    call bas_expr_depth_inc
    jc .be_cmp_complex
    call bas_expr_add
    jc .be_cmp_fail
.be_cmp_loop:
    mov al, [si]
    cmp al, '='
    je .be_cmp_eq
    cmp al, TOK_NE
    je .be_cmp_ne
    cmp al, '<'
    je .be_cmp_lt
    cmp al, '>'
    je .be_cmp_gt
    cmp al, TOK_LE
    je .be_cmp_le
    cmp al, TOK_GE
    je .be_cmp_ge
    jmp .be_cmp_done
.be_cmp_eq:
    mov bl, 1
    jmp .be_cmp_apply
.be_cmp_ne:
    mov bl, 2
    jmp .be_cmp_apply
.be_cmp_lt:
    mov bl, 3
    jmp .be_cmp_apply
.be_cmp_gt:
    mov bl, 4
    jmp .be_cmp_apply
.be_cmp_le:
    mov bl, 5
    jmp .be_cmp_apply
.be_cmp_ge:
    mov bl, 6
.be_cmp_apply:
    inc si
    ; Branch by LHS type
    cmp byte [bas_expr_result + BES_TYPE], BVAR_STRING
    je .be_cmp_str
    ; Numeric LHS — existing fast path
    push dx
    push bx
    call bas_expr_add
    jc .be_cmp_fail_pop
    cmp byte [bas_expr_result + BES_TYPE], BVAR_STRING
    je .be_cmp_typemix_pop
    pop bx
    pop ax                          ; AX = LHS
    cmp ax, dx
    jmp .be_cmp_emit
.be_cmp_str:
    ; LHS is a string descriptor in bas_expr_result.  Save to bas_str_lhs,
    ; eval RHS, ensure RHS is also string, run bas_str_cmp_desc.
    push bx                         ; save op code
    call bas_result_save_lhs
    call bas_expr_add
    jc .be_cmp_fail_strpop
    cmp byte [bas_expr_result + BES_TYPE], BVAR_STRING
    jne .be_cmp_typemix_strpop
    push si
    push di
    mov si, bas_str_lhs
    mov di, bas_expr_result
    call bas_str_cmp_desc           ; sets DX = -1/0/+1; flags via CMP DX,0
    pop di
    pop si
    pop bx
.be_cmp_emit:
    ; AT ENTRY:  flags reflect the operand comparison:
    ;   numeric path → `cmp ax, dx` (LHS vs RHS)
    ;   string  path → `cmp dx, 0`  inside bas_str_cmp_desc (sign of -1/0/+1)
    ; BL = operator code (1=eq, 2=ne, 3=lt, 4=gt, 5=le, 6=ge).
    ; The original BL dispatch used `cmp bl, N` here, which CLOBBERED the
    ; very flags the test branches below rely on — making every comparison
    ; produce the wrong result.  We preserve them with pushf/popf.
    BAS_DISPATCH_BEGIN                  ; preserve comparison flags across dispatch
    mov dx, 0                           ; default = false (mov: no flag effect)
    cmp bl, 1
    je .be_cmp_eq_test
    cmp bl, 2
    je .be_cmp_ne_test
    cmp bl, 3
    je .be_cmp_lt_test
    cmp bl, 4
    je .be_cmp_gt_test
    cmp bl, 5
    je .be_cmp_le_test
    jmp .be_cmp_ge_test
.be_cmp_eq_test:
    BAS_DISPATCH_END
    jne .be_cmp_false
    jmp .be_cmp_true
.be_cmp_ne_test:
    BAS_DISPATCH_END
    je .be_cmp_false
    jmp .be_cmp_true
.be_cmp_lt_test:
    BAS_DISPATCH_END
    jge .be_cmp_false
    jmp .be_cmp_true
.be_cmp_gt_test:
    BAS_DISPATCH_END
    jle .be_cmp_false
    jmp .be_cmp_true
.be_cmp_le_test:
    BAS_DISPATCH_END
    jg  .be_cmp_false
    jmp .be_cmp_true
.be_cmp_ge_test:
    BAS_DISPATCH_END
    jl  .be_cmp_false
    jmp .be_cmp_true
.be_cmp_true:
    mov dx, -1
    jmp .be_cmp_result_int
.be_cmp_false:
    xor dx, dx
.be_cmp_result_int:
    ; Result is an int; reset type so subsequent ops/checks see numeric.
    mov byte [bas_expr_result + BES_TYPE], BVAR_NUMERIC
    mov [bas_expr_result + BES_VAL_LO], dx
    jmp .be_cmp_loop
.be_cmp_done:
    call bas_expr_depth_dec
    clc
    ret
.be_cmp_typemix_pop:
    pop bx
    pop ax
    mov al, BERR_TYPE
    jmp .be_cmp_fail_set
.be_cmp_typemix_strpop:
    pop bx
    mov al, BERR_TYPE
    jmp .be_cmp_fail_set
.be_cmp_fail_strpop:
    pop bx
.be_cmp_fail:
    jmp .be_cmp_fail_end
.be_cmp_fail_pop:
    pop bx
    pop ax
.be_cmp_fail_set:
    stc
.be_cmp_fail_end:
    call bas_expr_depth_dec
    stc
    ret
.be_cmp_complex:
    mov al, BERR_TOOCOMPLEX
    stc
    ret


; --- add_expr --------------------------------------------------------------
; `+` does numeric add OR string concat (typed by LHS).
; `-` is numeric only.
bas_expr_add:
    call bas_expr_depth_inc
    jc .be_add_complex
    call bas_expr_mul
    jc .be_add_fail
.be_add_loop:
    mov al, [si]
    cmp al, '+'
    je .be_add_plus
    cmp al, '-'
    je .be_add_minus
    jmp .be_add_done
.be_add_plus:
    cmp byte [bas_expr_result + BES_TYPE], BVAR_STRING
    je .be_add_strconcat
    inc si
    push dx
    call bas_expr_mul
    jc .be_add_fail_pop
    cmp byte [bas_expr_result + BES_TYPE], BVAR_STRING
    je .be_add_typemix_pop
    pop ax
    add dx, ax
    jmp .be_add_loop
.be_add_strconcat:
    inc si
    call bas_result_save_lhs
    call bas_expr_mul
    jc .be_add_fail
    cmp byte [bas_expr_result + BES_TYPE], BVAR_STRING
    jne .be_add_typemix
    push si
    push di
    mov si, bas_str_lhs
    mov di, bas_expr_result
    call bas_str_concat             ; → bas_expr_result = LHS+RHS (TEMP)
    pop di
    pop si
    jc .be_add_fail
    jmp .be_add_loop
.be_add_minus:
    call bas_expr_require_int
    jc .be_add_fail
    inc si
    push dx
    call bas_expr_mul
    jc .be_add_fail_pop
    call bas_expr_require_int
    jc .be_add_fail_pop
    pop ax
    sub ax, dx
    mov dx, ax
    jmp .be_add_loop
.be_add_done:
    call bas_expr_depth_dec
    clc
    ret
.be_add_typemix_pop:
    pop ax
.be_add_typemix:
    mov al, BERR_TYPE
    stc
    call bas_expr_depth_dec
    stc
    ret
.be_add_fail_pop:
    pop ax
.be_add_fail:
    call bas_expr_depth_dec
    stc
    ret
.be_add_complex:
    mov al, BERR_TOOCOMPLEX
    stc
    ret


; --- mul_expr --------------------------------------------------------------
bas_expr_mul:
    call bas_expr_depth_inc
    jc .be_mul_complex
    call bas_expr_pow
    jc .be_mul_fail
.be_mul_loop:
    mov al, [si]
    cmp al, '*'
    je .be_mul_star
    cmp al, '/'
    je .be_mul_div
    cmp al, TOK_MOD
    je .be_mul_mod
    jmp .be_mul_done
.be_mul_star:
    call bas_expr_require_int
    jc .be_mul_fail
    inc si
    push dx
    call bas_expr_pow
    jc .be_mul_fail_pop
    call bas_expr_require_int
    jc .be_mul_fail_pop
    pop ax
    imul dx                         ; DX:AX = AX * DX (sign-extended)
    mov dx, ax                      ; truncate to 16 bits
    jmp .be_mul_loop
.be_mul_div:
    call bas_expr_require_int
    jc .be_mul_fail
    inc si
    push dx
    call bas_expr_pow
    jc .be_mul_fail_pop
    call bas_expr_require_int
    jc .be_mul_fail_pop
    pop ax
    test dx, dx
    jz .be_mul_div0_pop
    mov bx, dx
    cwd
    idiv bx
    mov dx, ax
    jmp .be_mul_loop
.be_mul_div0_pop:
    mov al, BERR_DIVZERO
    jmp .be_mul_fail
.be_mul_mod:
    call bas_expr_require_int
    jc .be_mul_fail
    inc si
    push dx
    call bas_expr_pow
    jc .be_mul_fail_pop
    call bas_expr_require_int
    jc .be_mul_fail_pop
    pop ax
    test dx, dx
    jz .be_mul_div0_pop
    mov bx, dx
    cwd
    idiv bx
    mov dx, dx                      ; remainder already in DX from idiv
    jmp .be_mul_loop
.be_mul_done:
    call bas_expr_depth_dec
    clc
    ret
.be_mul_fail_pop:
    pop ax
.be_mul_fail:
    call bas_expr_depth_dec
    stc
    ret
.be_mul_complex:
    mov al, BERR_TOOCOMPLEX
    stc
    ret


; --- pow_expr (right-associative; integer exponent ≥ 0) ------------------
bas_expr_pow:
    call bas_expr_depth_inc
    jc .be_pow_complex
    call bas_expr_unary
    jc .be_pow_fail
    mov al, [si]
    cmp al, '^'
    jne .be_pow_done
    call bas_expr_require_int
    jc .be_pow_fail
    inc si
    push dx                         ; save LHS (base)
    call bas_expr_pow               ; right-associative recurse → DX = exp
    jc .be_pow_fail_pop
    call bas_expr_require_int
    jc .be_pow_fail_pop
    pop ax                          ; AX = base
    ; AX^DX → DX
    test dx, dx
    js .be_pow_negexp
    jz .be_pow_one
    mov [bas_scratch_a], ax         ; stash base
    mov cx, dx                      ; CX = counter
    mov bx, 1                       ; BX = product
.be_pow_iter:
    mov ax, bx
    mov dx, [bas_scratch_a]
    imul dx                         ; DX:AX = product * base → trunc to AX
    mov bx, ax
    loop .be_pow_iter
    mov dx, bx
    jmp .be_pow_done
.be_pow_one:
    mov dx, 1
    jmp .be_pow_done
.be_pow_negexp:
    xor dx, dx
.be_pow_done:
    call bas_expr_depth_dec
    clc
    ret
.be_pow_fail_pop:
    pop ax
.be_pow_fail:
    call bas_expr_depth_dec
    stc
    ret
.be_pow_complex:
    mov al, BERR_TOOCOMPLEX
    stc
    ret


; --- unary ----------------------------------------------------------------
bas_expr_unary:
    call bas_expr_depth_inc
    jc .be_un_complex
    mov al, [si]
    cmp al, '-'
    je .be_un_minus
    cmp al, '+'
    je .be_un_plus
    call bas_expr_primary
    jc .be_un_fail
    call bas_expr_depth_dec
    clc
    ret
.be_un_minus:
    inc si
    call bas_expr_primary
    jc .be_un_fail
    call bas_expr_require_int
    jc .be_un_fail
    neg dx
    call bas_expr_depth_dec
    clc
    ret
.be_un_plus:
    inc si
    call bas_expr_primary
    jc .be_un_fail
    call bas_expr_depth_dec
    clc
    ret
.be_un_fail:
    call bas_expr_depth_dec
    stc
    ret
.be_un_complex:
    mov al, BERR_TOOCOMPLEX
    stc
    ret


; --- primary --------------------------------------------------------------
bas_expr_primary:
    mov al, [si]
    cmp al, TOK_INT_LIT
    je .bep_int
    cmp al, TOK_VAR_NAME
    je .bep_var
    cmp al, TOK_STR_LIT
    je .bep_strlit
    cmp al, '('
    je .bep_paren
    cmp al, TOK_LINEREF
    je .bep_int_lineref             ; treat line ref as int in expressions
    cmp al, TOK_FN
    je .bep_userfn
    ; Function call?
    cmp al, TOK_FN_FIRST
    jb .bep_syn
    cmp al, TOK_FN_LAST
    ja .bep_syn
    jmp bas_expr_function           ; tail-call
.bep_userfn:
    jmp bas_userfn_invoke           ; tail-call
.bep_syn:
    mov al, BERR_SYNTAX
    stc
    ret
.bep_int:
    inc si
    mov dx, [si]
    add si, 2
    mov byte [bas_expr_result + BES_TYPE], BVAR_NUMERIC
    clc
    ret
.bep_int_lineref:
    inc si
    mov dx, [si]
    add si, 2
    mov byte [bas_expr_result + BES_TYPE], BVAR_NUMERIC
    clc
    ret
.bep_strlit:
    ; TOK_STR_LIT + len + bytes.  Build a LITERAL descriptor pointing into
    ; the token stream (seg = DS, off = first byte).
    inc si                          ; past TOK_STR_LIT
    mov al, [si]                    ; length byte
    inc si
    xor ah, ah
    push ax                         ; save len
    mov bx, ds
    mov [bas_expr_result + BES_VAL_LO], bx
    mov [bas_expr_result + BES_VAL_HI], si
    pop ax
    mov [bas_expr_result + BES_LEN], ax
    mov byte [bas_expr_result + BES_TYPE], BVAR_STRING
    mov byte [bas_expr_result + BES_FLAGS], BSTR_LITERAL
    add si, ax                      ; advance past literal bytes
    xor dx, dx                      ; legacy DX value (caller ignores for strings)
    clc
    ret
.bep_var:
    inc si
    mov al, [si]                    ; name0
    mov ah, [si+1]                  ; name1
    add si, 2
    test al, 0x80
    jnz .bep_strvar
    ; Check for array subscript on numeric var
    cmp byte [si], '('
    je .bep_num_array
    call bas_var_get_int
    jc .bep_propagate
    mov dx, bx
    mov byte [bas_expr_result + BES_TYPE], BVAR_NUMERIC
    clc
    ret
.bep_num_array:
    call bas_var_find
    jc .bep_undim
    cmp byte [bx + BVR_TYPE], BVAR_NUM_ARRAY
    jne .bep_undim
    push bx
    inc si                          ; past '('
    call bas_expr_eval_int          ; DX = index
    pop bx
    jc .bep_propagate
    cmp byte [si], ')'
    jne .bep_no_rparen
    inc si
    call bas_array_get_num
    jc .bep_propagate
    mov dx, [bas_expr_result + BES_VAL_LO]
    clc
    ret
.bep_strvar:
    ; Check for array subscript on string var
    cmp byte [si], '('
    je .bep_str_array
    call bas_var_get_str            ; fills bas_expr_result
    jc .bep_propagate
    xor dx, dx
    clc
    ret
.bep_str_array:
    call bas_var_find
    jc .bep_undim
    cmp byte [bx + BVR_TYPE], BVAR_STR_ARRAY
    jne .bep_undim
    push bx
    inc si                          ; past '('
    call bas_expr_eval_int          ; DX = index
    pop bx
    jc .bep_propagate
    cmp byte [si], ')'
    jne .bep_no_rparen
    inc si
    call bas_array_get_str
    jc .bep_propagate
    xor dx, dx
    clc
    ret
.bep_undim:
    mov al, BERR_UNDIM
    stc
    ret
.bep_propagate:
    stc
    ret
.bep_paren:
    inc si
    call bas_expr_or
    jc .bep_propagate
    mov al, [si]
    cmp al, ')'
    jne .bep_no_rparen
    inc si
    clc
    ret
.bep_no_rparen:
    mov al, BERR_SYNTAX
    stc
    ret


; --- Depth tracking -------------------------------------------------------
bas_expr_depth_inc:
    cmp word [bas_ex_top], BAS_EXPR_DEPTH
    jae .bedi_full
    inc word [bas_ex_top]
    clc
    ret
.bedi_full:
    mov al, BERR_TOOCOMPLEX
    stc
    ret

bas_expr_depth_dec:
    dec word [bas_ex_top]
    ret


; =============================================================================
; bas_expr_function — dispatch numeric- AND string-returning functions.
;
; In:  SI → function token byte (TOK_FN_FIRST..TOK_FN_LAST)
; Out: Result in bas_expr_result + DX (legacy int).  CF=1 → AL = error code.
;
; Function categories:
;   No-arg:                  TIME, INKEY$
;   Single int → int:        ABS, SGN, INT, RND, PEEK
;   Single int → string:     CHR$, STR$
;   Single string → int:     LEN, ASC, VAL
;   String + int → string:   LEFT$, RIGHT$
;   String + int + [int]:    MID$
;   EOF / INPUT$:            BERR_BADFUNC (Phase 4 file I/O)
; =============================================================================
bas_expr_function:
    push bx
    push cx
    mov al, [si]
    inc si
    mov bl, al                      ; save token

    ; No-arg functions (no '(' to consume)
    cmp bl, TOK_TIME
    je .bef_time
    cmp bl, TOK_INKEYS
    je .bef_inkeys_path

    ; All other functions need '('
    mov al, [si]
    cmp al, '('
    jne .bef_syn
    inc si

    ; Multi-arg functions parse their own arg list
    cmp bl, TOK_LEFTS
    je .bef_lefts_path
    cmp bl, TOK_RIGHTS
    je .bef_rights_path
    cmp bl, TOK_MIDS
    je .bef_mids_path
    cmp bl, TOK_INPUTS
    je .bef_badfn

    ; EOF(N) — channel function, takes int arg returning int
    cmp bl, TOK_EOF_FN
    je .bef_eof_path

    ; Single-arg function: parse it
    call bas_expr_or
    jc .bef_prop

    ; String-arg single → numeric (LEN, ASC, VAL)
    cmp bl, TOK_LEN
    je .bef_str_arg
    cmp bl, TOK_ASC
    je .bef_str_arg
    cmp bl, TOK_VAL
    je .bef_str_arg
    cmp bl, TOK_EOF_FN
    je .bef_badfn

    ; Otherwise: int-arg single (numeric → numeric OR int → string)
    call bas_expr_require_int
    jc .bef_prop
    mov al, [si]
    cmp al, ')'
    jne .bef_syn
    inc si

    ; Int-returning numeric functions
    cmp bl, TOK_ABS
    je .bef_abs
    cmp bl, TOK_SGN
    je .bef_sgn
    cmp bl, TOK_INT_FN
    je .bef_int
    cmp bl, TOK_RND
    je .bef_rnd
    cmp bl, TOK_PEEK
    je .bef_peek
    ; Int-returning string functions (CHR$, STR$)
    cmp bl, TOK_CHRS
    je .bef_chrs_call
    cmp bl, TOK_STRS
    je .bef_strs_call
    jmp .bef_badfn

; --- String-arg dispatch (LEN/ASC/VAL) ----------------------------------
.bef_str_arg:
    cmp byte [bas_expr_result + BES_TYPE], BVAR_STRING
    jne .bef_typearg
    mov al, [si]
    cmp al, ')'
    jne .bef_syn
    inc si
    cmp bl, TOK_LEN
    je .bef_lencall
    cmp bl, TOK_ASC
    je .bef_asccall
    cmp bl, TOK_VAL
    je .bef_valcall
    jmp .bef_badfn
.bef_lencall:
    call bas_strfn_len
    jc .bef_prop_clean
    jmp .bef_ok_keep_dx
.bef_asccall:
    call bas_strfn_asc
    jc .bef_prop_clean
    jmp .bef_ok_keep_dx
.bef_valcall:
    call bas_strfn_val
    jc .bef_prop_clean
    jmp .bef_ok_keep_dx

; --- Int → string (CHR$, STR$) ------------------------------------------
.bef_chrs_call:
    call bas_strfn_chrs
    jc .bef_prop_clean
    jmp .bef_ok_str
.bef_strs_call:
    call bas_strfn_strs
    jc .bef_prop_clean
    jmp .bef_ok_str

; --- LEFT$ / RIGHT$ ------------------------------------------------------
.bef_lefts_path:
    call bas_expr_or
    jc .bef_prop
    cmp byte [bas_expr_result + BES_TYPE], BVAR_STRING
    jne .bef_typearg
    call bas_result_save_lhs        ; bas_str_lhs = source string
    mov al, [si]
    cmp al, ','
    jne .bef_syn
    inc si
    call bas_expr_or
    jc .bef_prop
    call bas_expr_require_int
    jc .bef_prop
    mov al, [si]
    cmp al, ')'
    jne .bef_syn
    inc si
    call bas_strfn_lefts            ; DX = n already; bas_str_lhs = src
    jc .bef_prop_clean
    jmp .bef_ok_str
.bef_rights_path:
    call bas_expr_or
    jc .bef_prop
    cmp byte [bas_expr_result + BES_TYPE], BVAR_STRING
    jne .bef_typearg
    call bas_result_save_lhs
    mov al, [si]
    cmp al, ','
    jne .bef_syn
    inc si
    call bas_expr_or
    jc .bef_prop
    call bas_expr_require_int
    jc .bef_prop
    mov al, [si]
    cmp al, ')'
    jne .bef_syn
    inc si
    call bas_strfn_rights
    jc .bef_prop_clean
    jmp .bef_ok_str

; --- MID$(s, start[, len]) ----------------------------------------------
.bef_mids_path:
    call bas_expr_or
    jc .bef_prop
    cmp byte [bas_expr_result + BES_TYPE], BVAR_STRING
    jne .bef_typearg
    call bas_result_save_lhs
    mov al, [si]
    cmp al, ','
    jne .bef_syn
    inc si
    call bas_expr_or
    jc .bef_prop
    call bas_expr_require_int
    jc .bef_prop
    mov [bas_scratch_b], dx         ; start
    mov word [bas_scratch_c], -1    ; default: rest of string
    mov al, [si]
    cmp al, ','
    jne .bef_mids_nolen
    inc si
    call bas_expr_or
    jc .bef_prop
    call bas_expr_require_int
    jc .bef_prop
    mov [bas_scratch_c], dx
    mov al, [si]
.bef_mids_nolen:
    cmp al, ')'
    jne .bef_syn
    inc si
    call bas_strfn_mids
    jc .bef_prop_clean
    jmp .bef_ok_str

; --- INKEY$ -------------------------------------------------------------
.bef_inkeys_path:
    call bas_strfn_inkeys
    jc .bef_prop_clean
    jmp .bef_ok_str

; --- EOF(N) — int arg, int return -----------------------------------------
; NOTE: '(' was already consumed by the common-dispatch prelude (see ~line 822),
; just like the LEFT$/MID$/RIGHT$ paths. Do NOT look for it again here.
.bef_eof_path:
    call bas_expr_eval_int              ; DX = channel#
    jc .bef_prop
    mov al, [si]
    cmp al, ')'
    jne .bef_syn
    inc si
    mov ax, dx
    call bas_chan_eof                   ; AX=chan, DX=result
    jc .bef_prop
    jmp .bef_ok

; --- Int-returning numeric functions (DX = arg) -------------------------
.bef_abs:
    test dx, dx
    jns .bef_ok
    neg dx
    jmp .bef_ok
.bef_sgn:
    test dx, dx
    jz .bef_ok
    js .bef_sgn_neg
    mov dx, 1
    jmp .bef_ok
.bef_sgn_neg:
    mov dx, -1
    jmp .bef_ok
.bef_int:
    jmp .bef_ok
.bef_rnd:
    test dx, dx
    jle .bef_rnd_zero
    push dx
    mov ax, [bas_rnd_seed]
    mov cx, 25173
    imul cx
    add ax, 13849
    mov [bas_rnd_seed], ax
    and ax, 0x7FFF
    pop cx
    xor dx, dx
    div cx
    inc dx
    jmp .bef_ok
.bef_rnd_zero:
    xor dx, dx
    jmp .bef_ok
.bef_time:
    push es
    push bx
    mov ax, 0x0040
    mov es, ax
    mov dx, [es:0x006C]
    pop bx
    pop es
    jmp .bef_ok
.bef_peek:
    push es
    mov ax, [bas_def_seg]
    mov es, ax
    mov bx, dx
    mov dl, [es:bx]
    xor dh, dh
    pop es
    jmp .bef_ok

; --- Common exits -------------------------------------------------------
.bef_typearg:
    pop cx
    pop bx
    mov al, BERR_TYPE
    stc
    ret
.bef_badfn:
    pop cx
    pop bx
    mov al, BERR_BADFUNC
    stc
    ret
.bef_syn:
    pop cx
    pop bx
    mov al, BERR_SYNTAX
    stc
    ret
.bef_prop:
    pop cx
    pop bx
    stc
    ret
.bef_prop_clean:
    pop cx
    pop bx
    stc
    ret
.bef_ok:
    mov byte [bas_expr_result + BES_TYPE], BVAR_NUMERIC
    mov [bas_expr_result + BES_VAL_LO], dx
.bef_ok_keep_dx:
    pop cx
    pop bx
    clc
    ret
.bef_ok_str:
    pop cx
    pop bx
    clc
    ret
