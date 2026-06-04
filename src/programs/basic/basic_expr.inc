; =============================================================================
; basic_expr.inc — Recursive-descent integer expression evaluator (v1).
;
; v1 grammar (all integer):
;   expr     := or_expr
;   or_expr  := and_expr (OR and_expr)*
;   and_expr := not_expr (AND not_expr)*
;   not_expr := [NOT] cmp_expr
;   cmp_expr := add_expr ((= | <> | < | > | <= | >=) add_expr)*
;   add_expr := mul_expr ((+|-) mul_expr)*
;   mul_expr := pow_expr ((*|/|MOD) pow_expr)*
;   pow_expr := unary (^ pow_expr)?
;   unary    := [-|+] primary
;   primary  := int_literal | variable | '(' expr ')'
;
; v1 does NOT support strings, function calls, or arrays in expressions.
; Those raise BERR_SYNTAX / BERR_TYPE / BERR_BADFUNC accordingly.
;
; Public:
;   bas_expr_eval     In: DS:SI = token cursor (statement-local)
;                     Out: DX = signed 16-bit result
;                          SI advanced past expression
;                          CF=0 ok; CF=1 → AL = BERR_*
;
;   bas_expr_eval_int Wraps bas_expr_eval and forces integer result.
;                     (Same signature; provided for callers that want a
;                      single named entry.)
;
; All comparisons return -1 (true) / 0 (false), per GW-BASIC.
; Division by zero → BERR_DIVZERO.  Arithmetic uses signed 16-bit; overflow
; wraps silently (matches integer-mode GW-BASIC).
;
; Recursion depth is tracked in bas_ex_top (1-byte counter) and capped at
; BAS_EXPR_DEPTH (16); deeper → BERR_TOOCOMPLEX.
; =============================================================================

bas_expr_eval:
bas_expr_eval_int:
    mov word [bas_ex_top], 0
    call bas_expr_or
    ret


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
    inc si
    push dx                         ; save LHS
    call bas_expr_and
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
    inc si
    push dx
    call bas_expr_not
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
    push dx
    push bx
    call bas_expr_add
    jc .be_cmp_fail_pop
    pop bx
    pop ax                          ; AX = LHS
    cmp ax, dx
    ; Evaluate based on BL
    mov dx, 0
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
    jne .be_cmp_false
    jmp .be_cmp_true
.be_cmp_ne_test:
    je .be_cmp_false
    jmp .be_cmp_true
.be_cmp_lt_test:
    jge .be_cmp_false
    jmp .be_cmp_true
.be_cmp_gt_test:
    jle .be_cmp_false
    jmp .be_cmp_true
.be_cmp_le_test:
    jg  .be_cmp_false
    jmp .be_cmp_true
.be_cmp_ge_test:
    jl  .be_cmp_false
    jmp .be_cmp_true
.be_cmp_true:
    mov dx, -1
    jmp .be_cmp_loop
.be_cmp_false:
    xor dx, dx
    jmp .be_cmp_loop
.be_cmp_done:
    call bas_expr_depth_dec
    clc
    ret
.be_cmp_fail_pop:
    pop bx
    pop ax
.be_cmp_fail:
    call bas_expr_depth_dec
    stc
    ret
.be_cmp_complex:
    mov al, BERR_TOOCOMPLEX
    stc
    ret


; --- add_expr --------------------------------------------------------------
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
    inc si
    push dx
    call bas_expr_mul
    jc .be_add_fail_pop
    pop ax
    add dx, ax
    jmp .be_add_loop
.be_add_minus:
    inc si
    push dx
    call bas_expr_mul
    jc .be_add_fail_pop
    pop ax
    sub ax, dx
    mov dx, ax
    jmp .be_add_loop
.be_add_done:
    call bas_expr_depth_dec
    clc
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
    inc si
    push dx
    call bas_expr_pow
    jc .be_mul_fail_pop
    pop ax
    imul dx                         ; DX:AX = AX * DX (sign-extended)
    mov dx, ax                      ; truncate to 16 bits
    jmp .be_mul_loop
.be_mul_div:
    inc si
    push dx
    call bas_expr_pow
    jc .be_mul_fail_pop
    pop ax
    test dx, dx
    jz .be_mul_div0_pop
    ; AX / DX, signed
    mov bx, dx
    cwd
    idiv bx
    mov dx, ax
    jmp .be_mul_loop
.be_mul_div0_pop:
    mov al, BERR_DIVZERO
    jmp .be_mul_fail
.be_mul_mod:
    inc si
    push dx
    call bas_expr_pow
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
    inc si
    push dx                         ; save LHS (base)
    call bas_expr_pow               ; right-associative recurse → DX = exp
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
    cmp al, '('
    je .bep_paren
    cmp al, TOK_LINEREF
    je .bep_int_lineref             ; treat line ref as int in expressions
    ; Function call?
    cmp al, TOK_FN_FIRST
    jb .bep_syn
    cmp al, TOK_FN_LAST
    ja .bep_syn
    jmp bas_expr_function           ; tail-call
.bep_syn:
    mov al, BERR_SYNTAX
    stc
    ret
.bep_int:
    inc si
    mov dx, [si]
    add si, 2
    clc
    ret
.bep_int_lineref:
    inc si
    mov dx, [si]
    add si, 2
    clc
    ret
.bep_var:
    inc si
    mov al, [si]                    ; name0
    mov ah, [si+1]                  ; name1
    add si, 2
    ; String variable in expression → not supported in v1 expr ctx
    test al, 0x80
    jnz .bep_str_type
    call bas_var_get_int
    jc .bep_propagate
    mov dx, bx
    clc
    ret
.bep_str_type:
    mov al, BERR_TYPE
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
; bas_expr_function — dispatch numeric-returning functions.
;
; In:  SI → function token byte (TOK_FN_FIRST..TOK_FN_LAST)
; Out: DX = result, CF=1 → AL = error code
;
; v1 numeric-returning functions:
;   ABS(n), SGN(n), INT(n), RND(n), TIME, PEEK(off), LEN(s)*, ASC(s)*, VAL(s)*
;
; *LEN/ASC/VAL require basic_str.inc — emitted as BERR_BADFUNC stub for now.
; =============================================================================
bas_expr_function:
    push bx
    push cx
    mov al, [si]
    inc si
    mov bl, al                      ; save token
    ; Handle TIME first — no arg
    cmp bl, TOK_TIME
    je .bef_time
    ; Other functions take a parenthesised single arg
    mov al, [si]
    cmp al, '('
    jne .bef_syn
    inc si
    call bas_expr_or
    jc .bef_prop
    mov al, [si]
    cmp al, ')'
    jne .bef_syn
    inc si
    ; Now DX = argument. Dispatch.
    cmp bl, TOK_ABS
    je .bef_abs
    cmp bl, TOK_SGN
    je .bef_sgn
    cmp bl, TOK_INT_FN
    je .bef_int                     ; identity for v1 (integer-only)
    cmp bl, TOK_RND
    je .bef_rnd
    cmp bl, TOK_PEEK
    je .bef_peek
    cmp bl, TOK_LEN
    je .bef_badfn
    cmp bl, TOK_ASC
    je .bef_badfn
    cmp bl, TOK_VAL
    je .bef_badfn
    cmp bl, TOK_EOF_FN
    je .bef_badfn
    jmp .bef_badfn

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
    ; RND(n): returns 1..n. If n<=0, return 0.
    test dx, dx
    jle .bef_rnd_zero
    push dx
    ; Advance LCG: seed = seed * 25173 + 13849
    mov ax, [bas_rnd_seed]
    mov cx, 25173
    imul cx
    add ax, 13849
    mov [bas_rnd_seed], ax
    ; Force positive
    and ax, 0x7FFF
    pop cx                          ; CX = n
    xor dx, dx
    div cx                          ; DX = remainder = 0..n-1
    inc dx                          ; 1..n
    jmp .bef_ok
.bef_rnd_zero:
    xor dx, dx
    jmp .bef_ok
.bef_time:
    ; TIME — read BIOS tick counter low word (40:6C)
    push es
    push bx
    mov ax, 0x0040
    mov es, ax
    mov dx, [es:0x006C]
    pop bx
    pop es
    jmp .bef_ok
.bef_peek:
    ; PEEK(offset) — read byte at [DEF_SEG : offset]
    push es
    mov ax, [bas_def_seg]
    mov es, ax
    mov bx, dx
    mov dl, [es:bx]
    xor dh, dh
    pop es
    jmp .bef_ok
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
.bef_ok:
    pop cx
    pop bx
    clc
    ret
