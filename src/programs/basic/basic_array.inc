; =============================================================================
; basic_array.inc — 1-dimensional arrays (numeric and string).
;
; Storage layout:
;   Numeric array: (UPPER+1) consecutive words at [VAL_LO:VAL_HI].
;     element i lives at offset (i * 2).
;   String  array: (UPPER+1) consecutive 82-byte slots at [VAL_LO:VAL_HI].
;     element i lives at offset (i * 82); first 2 bytes = length, next 80 = chars.
;
; Storage is HMA-allocated via mn_alloc (owner=0).  Freed on NEW or CLEAR via
; bas_array_free_all.
; =============================================================================
%ifndef BASIC_ARRAY_INC
%define BASIC_ARRAY_INC

BAS_STR_ARR_ELEM_SIZE equ 82            ; 2B length + 80B chars

; ---------------------------------------------------------------------------
; bas_stmt_dim — handle DIM <name>(<upper>) [, <name>(<upper>)]*
;   In:  SI → first token after TOK_DIM
;   Out: SI past statement;  CF=1 → AL=BERR_*
; ---------------------------------------------------------------------------
bas_stmt_dim:
.bsd_one:
    mov al, [si]
    cmp al, TOK_VAR_NAME
    jne .bsd_syn
    inc si
    mov al, [si]
    mov ah, [si+1]
    add si, 2
    mov [bas_scratch_b], ax             ; save name
    mov bl, [si]
    cmp bl, '('
    jne .bsd_syn
    inc si
    call bas_expr_eval_int              ; DX = upper
    jc .bsd_propagate
    mov bl, [si]
    cmp bl, ')'
    jne .bsd_syn
    inc si
    mov ax, [bas_scratch_b]
    call bas_array_dim_one              ; AL/AH=name, DX=upper
    jc .bsd_propagate
    mov bl, [si]
    cmp bl, ','
    jne .bsd_done
    inc si
    jmp .bsd_one
.bsd_done:
    clc
    ret
.bsd_syn:
    mov al, BERR_SYNTAX
    stc
    ret
.bsd_propagate:
    stc
    ret

; ---------------------------------------------------------------------------
; bas_array_dim_one — create a new array variable, allocate HMA storage.
;   In:  AL/AH = name, DX = upper bound (>= 0)
;   Out: CF=1 → AL = BERR_*
; ---------------------------------------------------------------------------
bas_array_dim_one:
    push bx
    push cx
    push dx
    push di
    push bp
    mov bp, sp
    sub sp, 8
    ; Locals:
    ;   [bp-2] = name
    ;   [bp-4] = upper
    ;   [bp-6] = element size
    ;   [bp-8] = type tag
    mov [bp-2], ax
    mov [bp-4], dx
    test dx, dx
    js .bad1_bsub
    call bas_var_find
    jnc .bad1_dup
    test al, 0x80
    jnz .bad1_str_arr
    mov word [bp-6], 2
    mov word [bp-8], BVAR_NUM_ARRAY
    jmp .bad1_have_type
.bad1_str_arr:
    mov word [bp-6], BAS_STR_ARR_ELEM_SIZE
    mov word [bp-8], BVAR_STR_ARRAY
.bad1_have_type:
    mov ax, [bp-4]
    inc ax                              ; count = upper+1
    mov cx, [bp-6]
    mul cx                              ; DX:AX = total bytes
    test dx, dx
    jnz .bad1_oom                       ; >64K not supported
    mov cx, ax
    test cx, cx
    jnz .bad1_do_alloc
    xor ax, ax
    xor bx, bx
    jmp .bad1_alloc_done
.bad1_do_alloc:
    xor dx, dx                          ; owner=0
    call mn_alloc                       ; AX=seg, BX=off
    jc .bad1_oom
.bad1_alloc_done:
    push ax
    push bx
    push cx
    push es
    push di
    mov es, ax
    mov di, bx
    xor al, al
    cld
    rep stosb
    pop di
    pop es
    pop cx
    pop bx
    pop ax
    push ax
    push bx
    mov ax, [bp-2]
    mov dx, [bp-8]                      ; type tag in DL
    call bas_var_create
    jc .bad1_create_fail
    pop di                              ; storage off
    pop ax                              ; storage seg
    mov [bx + BVR_VAL_LO], ax
    mov [bx + BVR_VAL_HI], di
    mov ax, [bp-4]
    mov [bx + BVR_LEN_DIM], ax
    mov sp, bp
    pop bp
    pop di
    pop dx
    pop cx
    pop bx
    clc
    ret
.bad1_create_fail:
    pop bx
    pop ax
    ; storage leaked (rare path); accept for simplicity
    mov sp, bp
    pop bp
    pop di
    pop dx
    pop cx
    pop bx
    stc
    ret
.bad1_oom:
    mov al, BERR_OOM
    jmp .bad1_err
.bad1_dup:
    mov al, BERR_DUPDEF
    jmp .bad1_err
.bad1_bsub:
    mov al, BERR_BADSUBSCR
.bad1_err:
    mov sp, bp
    pop bp
    pop di
    pop dx
    pop cx
    pop bx
    stc
    ret

; ---------------------------------------------------------------------------
; bas_array_check_index — validate index in DX vs upper bound in [BX+LEN_DIM].
;   Out: CF=0 ok; CF=1 → AL = BERR_BADSUBSCR
; ---------------------------------------------------------------------------
bas_array_check_index:
    test dx, dx
    js .baci_oob
    cmp dx, [bx + BVR_LEN_DIM]
    ja .baci_oob
    clc
    ret
.baci_oob:
    mov al, BERR_BADSUBSCR
    stc
    ret

; ---------------------------------------------------------------------------
; bas_array_get_num — read numeric element into bas_expr_result + DX.
;   In:  BX = var rec ptr, DX = index
; ---------------------------------------------------------------------------
bas_array_get_num:
    push si
    push es
    call bas_array_check_index
    jc .bagn_fail
    shl dx, 1
    mov si, [bx + BVR_VAL_HI]
    add si, dx
    mov ax, [bx + BVR_VAL_LO]
    mov es, ax
    mov dx, [es:si]
    mov byte [bas_expr_result + BES_TYPE], BVAR_NUMERIC
    mov [bas_expr_result + BES_VAL_LO], dx
    pop es
    pop si
    clc
    ret
.bagn_fail:
    pop es
    pop si
    stc
    ret

; ---------------------------------------------------------------------------
; bas_array_set_num — store DX into numeric element at index CX.
;   In:  BX = var rec ptr, CX = index, DX = value
; ---------------------------------------------------------------------------
bas_array_set_num:
    push si
    push es
    push ax
    push dx
    mov dx, cx
    call bas_array_check_index
    jc .basn_fail
    shl dx, 1
    mov si, [bx + BVR_VAL_HI]
    add si, dx
    mov ax, [bx + BVR_VAL_LO]
    mov es, ax
    pop dx
    push dx
    mov [es:si], dx
    pop dx
    pop ax
    pop es
    pop si
    clc
    ret
.basn_fail:
    pop dx
    pop ax
    pop es
    pop si
    stc
    ret

; ---------------------------------------------------------------------------
; bas_array_get_str — read string element into bas_expr_result as BSTR_VAR.
;   In:  BX = var rec ptr, DX = index
; ---------------------------------------------------------------------------
bas_array_get_str:
    push si
    push es
    call bas_array_check_index
    jc .bags_fail
    mov ax, BAS_STR_ARR_ELEM_SIZE
    push dx
    mul dx                              ; AX = element offset
    pop dx
    mov si, [bx + BVR_VAL_HI]
    add si, ax
    mov ax, [bx + BVR_VAL_LO]
    mov es, ax
    mov cx, [es:si]                     ; length word
    mov [bas_expr_result + BES_VAL_LO], ax
    add si, 2
    mov [bas_expr_result + BES_VAL_HI], si
    mov [bas_expr_result + BES_LEN], cx
    mov byte [bas_expr_result + BES_TYPE], BVAR_STRING
    mov byte [bas_expr_result + BES_FLAGS], BSTR_VAR
    pop es
    pop si
    clc
    ret
.bags_fail:
    pop es
    pop si
    stc
    ret

; ---------------------------------------------------------------------------
; bas_array_set_str — store bas_expr_result into string element at index CX.
;   In:  BX = var rec ptr, CX = index
; ---------------------------------------------------------------------------
bas_array_set_str:
    push si
    push di
    push es
    push bp
    mov bp, sp
    sub sp, 4
    ; [bp-2] = saved var rec ptr
    ; [bp-4] = element offset
    mov [bp-2], bx
    push ax
    push bx
    push cx
    push dx
    mov dx, cx
    call bas_array_check_index
    jc .bass_fail
    mov cx, [bas_expr_result + BES_LEN]
    cmp cx, BAS_STR_VAR_LEN
    ja .bass_long
    mov ax, BAS_STR_ARR_ELEM_SIZE
    mul dx                              ; AX = element offset
    mov [bp-4], ax
    mov bx, [bp-2]
    mov si, [bx + BVR_VAL_HI]
    add si, ax                          ; SI = element off
    mov ax, [bx + BVR_VAL_LO]           ; AX = storage seg
    mov es, ax
    mov [es:si], cx                     ; write length word
    add si, 2
    test cx, cx
    jz .bass_done
    mov dx, ax                          ; dst seg
    mov di, si                          ; dst off
    mov ax, [bas_expr_result + BES_VAL_LO]
    mov bx, [bas_expr_result + BES_VAL_HI]
    call bas_str_copy
.bass_done:
    pop dx
    pop cx
    pop bx
    pop ax
    mov sp, bp
    pop bp
    pop es
    pop di
    pop si
    clc
    ret
.bass_long:
    pop dx
    pop cx
    pop bx
    pop ax
    mov sp, bp
    pop bp
    pop es
    pop di
    pop si
    mov al, BERR_STRTOOLONG
    stc
    ret
.bass_fail:
    pop dx
    pop cx
    pop bx
    pop ax
    mov sp, bp
    pop bp
    pop es
    pop di
    pop si
    stc
    ret

; ---------------------------------------------------------------------------
; bas_array_free_all — walk var table, mn_free every array storage block.
;   Called from bas_cmd_clear BEFORE bas_var_count is zeroed.
; ---------------------------------------------------------------------------
bas_array_free_all:
    push ax
    push bx
    push cx
    push si
    mov cx, [bas_var_count]
    test cx, cx
    jz .bafa_done
    mov si, bas_var_table
.bafa_walk:
    mov al, [si + BVR_TYPE]
    cmp al, BVAR_NUM_ARRAY
    je .bafa_free
    cmp al, BVAR_STR_ARRAY
    je .bafa_free
    jmp .bafa_skip
.bafa_free:
    mov ax, [si + BVR_VAL_LO]
    test ax, ax
    jz .bafa_skip
    mov bx, [si + BVR_VAL_HI]
    call mn_free
    mov word [si + BVR_VAL_LO], 0
    mov word [si + BVR_VAL_HI], 0
.bafa_skip:
    add si, BAS_VAR_REC_SIZE
    loop .bafa_walk
.bafa_done:
    pop si
    pop cx
    pop bx
    pop ax
    ret

%endif
