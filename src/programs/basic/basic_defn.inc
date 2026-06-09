; =============================================================================
; basic_defn.inc — DEF FN registration + invocation.
;
;   DEF FN A(X) = X*X + 1   then later   FN A(5)  → 26.
;
; Recursion is forbidden (1-level scope only) — TOOCOMPLEX if attempted.
;
; Storage (8 B per entry, table at bas_userfn_table — see basic_data.inc):
;   +0  byte  name0       function name (no $)
;   +1  byte  name1
;   +2  byte  pname0      param name (no $)
;   +3  byte  pname1
;   +4  word  expr_addr   abs DS address of first body token
;   +6  byte  flags       bit 0 = active
;   +7  byte  pad
; =============================================================================

BAS_USERFN_REC_SIZE       equ 8
BAS_USERFN_MAX            equ 16

; Active-call frame (1-level scope).  Lives in the BSS "reserved future
; growth" pool defined in basic_data.inc (0xD060..0xD3FF), so it gets
; zeroed by bas_init's rep stosb on the BAS_BSS_SIZE region.
;
; History: pre-v0.9.19.0 these labels lived at 0xBC60+ — inside the old
; BSS region (0xB000..0xC000).  When v0.9.19.0 relocated BSS to 0xC400
; (above the code segment) the 0xBC60 addresses ended up INSIDE the
; executable code segment.  bas_load_file and bas_error both write
; `byte [bas_userfn_depth], 0` unconditionally, so every BASIC load or
; runtime error scribbled zero on whatever interpreter instruction
; happened to live at offset 0x3C60 — causing apparently-random
; mis-dispatch failures later on.  See CHANGELOG [0.9.19] / [0.10.1].
bas_userfn_depth          equ 0xD060   ; byte: 0 = idle, 1 = in call
                                       ; (0xD061 pad)
bas_userfn_saved_si       equ 0xD062   ; word: outer SI to restore
bas_userfn_saved_ex_top   equ 0xD064   ; word: outer bas_ex_top
bas_userfn_saved_pval     equ 0xD066   ; word: param's old value
bas_userfn_saved_pname    equ 0xD068   ; word: param name (al=name0, ah=name1)


; =============================================================================
; bas_stmt_def — dispatch DEF SEG vs DEF FN. Replaces the original entry.
; =============================================================================
bas_stmt_def:
    mov al, [si]
    cmp al, TOK_SEG
    je bas_stmt_def_seg
    cmp al, TOK_FN
    je bas_stmt_def_fn
    mov al, BERR_SYNTAX
    stc
    ret


; =============================================================================
; bas_stmt_def_fn — DS:SI = TOK_FN.
; Parse  FN fn-name '(' param-name ')' '=' body
; and register the entry.  Body is NOT evaluated now; .brd_eat_to_sep skips.
; =============================================================================
bas_stmt_def_fn:
    push bx
    push cx
    push dx
    inc si                          ; past TOK_FN
    cmp byte [si], TOK_VAR_NAME
    jne .bsdf_syn
    inc si
    mov al, [si]
    inc si
    mov ah, [si]
    inc si
    test al, 0x80                   ; numeric only
    jnz .bsdf_syn
    mov dx, ax                      ; DX = fn name
    cmp byte [si], '('
    jne .bsdf_syn
    inc si
    cmp byte [si], TOK_VAR_NAME
    jne .bsdf_syn
    inc si
    mov al, [si]
    inc si
    mov ah, [si]
    inc si
    test al, 0x80                   ; numeric param
    jnz .bsdf_syn
    mov cx, ax                      ; CX = param name
    cmp byte [si], ')'
    jne .bsdf_syn
    inc si
    cmp byte [si], '='
    jne .bsdf_syn
    inc si
    mov ax, dx
    call bas_userfn_register        ; AX=fn name, CX=param, SI=body addr
    jc .bsdf_prop
    pop dx
    pop cx
    pop bx
    clc
    ret
.bsdf_syn:
    pop dx
    pop cx
    pop bx
    mov al, BERR_SYNTAX
    stc
    ret
.bsdf_prop:
    pop dx
    pop cx
    pop bx
    stc
    ret


; =============================================================================
; bas_userfn_register — add or replace entry.
; In:  AX = fn name, CX = param name, SI = body abs address
; Out: CF=0 ok; CF=1 → AL = BERR_TOOMANYVARS
; Preserves: AX, CX, SI.
; =============================================================================
bas_userfn_register:
    push bx
    push dx
    call bas_userfn_find            ; CF=0 → BX = existing entry
    jnc .bur_write
    mov bx, [bas_userfn_count]
    cmp bx, BAS_USERFN_MAX
    jae .bur_full
    push ax
    mov ax, bx
    mov dx, BAS_USERFN_REC_SIZE
    mul dx
    mov bx, ax
    pop ax
    add bx, bas_userfn_table
    inc word [bas_userfn_count]
.bur_write:
    mov [bx], al                    ; name0
    mov [bx+1], ah                  ; name1
    mov [bx+2], cl                  ; pname0
    mov [bx+3], ch                  ; pname1
    mov [bx+4], si                  ; expr addr
    mov byte [bx+6], 1
    pop dx
    pop bx
    clc
    ret
.bur_full:
    pop dx
    pop bx
    mov al, BERR_TOOMANYVARS
    stc
    ret


; =============================================================================
; bas_userfn_find — linear search by name.
; In:  AX = name
; Out: BX = entry addr; CF=0 found, CF=1 not found
; Preserves: AX, CX, DX, SI, DI.
; =============================================================================
bas_userfn_find:
    push cx
    push si
    mov cx, [bas_userfn_count]
    test cx, cx
    jz .buf_none
    mov bx, bas_userfn_table
.buf_loop:
    mov si, [bx]                    ; reads name0..name1 as word
    cmp si, ax
    je .buf_hit
    add bx, BAS_USERFN_REC_SIZE
    dec cx
    jnz .buf_loop
.buf_none:
    pop si
    pop cx
    stc
    ret
.buf_hit:
    pop si
    pop cx
    clc
    ret


; =============================================================================
; bas_userfn_invoke — called from bas_expr_primary on TOK_FN.
;
; In:  DS:SI = TOK_FN
; Out: DX = numeric result, bas_expr_result.BES_TYPE = 'N'
;      SI past closing ')'
;      CF=0 ok; CF=1 → AL = BERR_*
;
; Entry/exit register policy: same as bas_expr_primary leaves — DX/AX
; clobbered freely; SI advanced.
; =============================================================================
bas_userfn_invoke:
    push bx
    push cx
    push di
    inc si                          ; past TOK_FN
    cmp byte [si], TOK_VAR_NAME
    jne .bui_syn
    inc si
    mov al, [si]
    inc si
    mov ah, [si]
    inc si
    test al, 0x80
    jnz .bui_syn
    call bas_userfn_find            ; Out: BX = entry; CF=1 if missing
    jc .bui_undef

    ; Stash body addr and param name in BSS now (eval can clobber regs).
    mov ax, [bx+2]
    mov [bas_userfn_saved_pname], ax
    mov ax, [bx+4]                  ; body abs address
    push ax                         ; [SP] = body addr

    cmp byte [si], '('
    jne .bui_syn_pop
    inc si

    ; Evaluate argument expression.  Inner bas_expr_eval resets bas_ex_top
    ; so save/restore around it.
    push word [bas_ex_top]
    call bas_expr_eval              ; DX = arg, bas_expr_result set
    pop word [bas_ex_top]
    jc .bui_prop_pop
    cmp byte [bas_expr_result + BES_TYPE], 'S'
    je .bui_type_pop
    cmp byte [si], ')'
    jne .bui_syn_pop
    inc si                          ; past ')'

    ; Recursion check
    cmp byte [bas_userfn_depth], 0
    jne .bui_complex_pop
    mov byte [bas_userfn_depth], 1

    ; Save outer SI + ex_top
    mov [bas_userfn_saved_si], si
    mov ax, [bas_ex_top]
    mov [bas_userfn_saved_ex_top], ax

    ; Save param's current value (auto-create with 0 if missing).
    push dx                         ; preserve arg
    mov ax, [bas_userfn_saved_pname]
    call bas_var_get_int            ; BX = old value; CF=1 only on type clash
    pop dx                          ; restore arg
    jc .bui_clear_depth_prop_pop
    mov [bas_userfn_saved_pval], bx

    ; Set param := arg
    mov ax, [bas_userfn_saved_pname]
    call bas_var_set_int            ; AX=name, DX=arg
    jc .bui_clear_depth_prop_pop

    ; SI := body addr (pop from stack), reset ex_top, eval body.
    pop si                          ; SI = body addr (consumes the saved push)
    mov word [bas_ex_top], 0
    call bas_expr_eval              ; DX = result, bas_expr_result = result
    pushf
    push dx

    ; Restore param's old value (best effort — ignore CF).
    mov ax, [bas_userfn_saved_pname]
    mov dx, [bas_userfn_saved_pval]
    call bas_var_set_int

    ; Restore outer state.
    mov si, [bas_userfn_saved_si]
    mov ax, [bas_userfn_saved_ex_top]
    mov [bas_ex_top], ax
    mov byte [bas_userfn_depth], 0

    pop dx
    popf
    jc .bui_final_fail
    pop di
    pop cx
    pop bx
    clc
    ret

.bui_final_fail:
    pop di
    pop cx
    pop bx
    stc
    ret

.bui_clear_depth_prop_pop:
    mov byte [bas_userfn_depth], 0
    add sp, 2                       ; discard pushed body addr
    pop di
    pop cx
    pop bx
    stc
    ret

.bui_prop_pop:
    add sp, 2                       ; discard body addr
    pop di
    pop cx
    pop bx
    stc
    ret

.bui_syn_pop:
    add sp, 2
    mov al, BERR_SYNTAX
    pop di
    pop cx
    pop bx
    stc
    ret
.bui_type_pop:
    add sp, 2
    mov al, BERR_TYPE
    pop di
    pop cx
    pop bx
    stc
    ret
.bui_complex_pop:
    add sp, 2
    mov al, BERR_TOOCOMPLEX
    pop di
    pop cx
    pop bx
    stc
    ret
.bui_undef:
    mov al, BERR_BADFUNC
    pop di
    pop cx
    pop bx
    stc
    ret
.bui_syn:
    mov al, BERR_SYNTAX
    pop di
    pop cx
    pop bx
    stc
    ret
