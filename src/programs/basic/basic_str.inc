; =============================================================================
; basic_str.inc — BASIC v2 string subsystem
;
; All string bytes live in HMA-allocated buffers (segment 0xFFFF).  Three
; source categories distinguish lifetime + storage (BSTR_* flags):
;
;   BSTR_LITERAL — descriptor.seg = DS, descriptor.off = pointer into the
;                  tokenised program stream just after TOK_STR_LIT + len.
;                  Lifetime: stable for the current statement.  Read-only.
;                  Never stored verbatim into a variable.
;
;   BSTR_VAR     — descriptor.seg = bas_str_heap_seg, descriptor.off = an
;                  80-byte slot reserved on first assignment.  Lifetime:
;                  until NEW / CLEAR.
;
;   BSTR_TEMP    — descriptor.seg = HMA seg, descriptor.off = mn_alloc'd
;                  block of size == descriptor.len.  Recorded in
;                  bas_temp_pool; freed in bulk by bas_temp_pool_free_all
;                  at each statement boundary or on error.
;
; Descriptors fit the 8-byte BES_* layout — same shape lives in
; bas_expr_result, bas_ex_stack slots, and BVAR_STRING records (via the
; first 6 bytes of the var record: VAL_LO=seg, VAL_HI=off, LEN_DIM=length).
;
; All public helpers preserve DS=SS by convention.  ES is clobberable.
; Targets 286-class CPU (no FS/GS use).
; =============================================================================

; ----------------------------------------------------------------------------
; bas_str_heap_ensure — lazily allocate the string-variable heap on first use.
;   Out: CF=0 ok; CF=1 → AL = BERR_OOM
; ----------------------------------------------------------------------------
bas_str_heap_ensure:
    push bx
    push cx
    push dx
    cmp word [bas_str_heap_seg], 0
    jne .bshe_ok
    mov cx, BAS_STR_HEAP_SIZE
    mov dl, 0
    call mn_alloc
    jc .bshe_fail
    mov [bas_str_heap_seg], ax
    mov [bas_str_heap_off], bx
    mov word [bas_str_top], 0
.bshe_ok:
    pop dx
    pop cx
    pop bx
    clc
    ret
.bshe_fail:
    pop dx
    pop cx
    pop bx
    mov al, BERR_OOM
    stc
    ret

; ----------------------------------------------------------------------------
; bas_str_heap_release — free the string-variable heap (called from NEW).
; Zeroes every BVAR_STRING descriptor first so dangling pointers can't be
; followed.  Best-effort; ignores MEM_FREE errors.
; ----------------------------------------------------------------------------
bas_str_heap_release:
    push ax
    push bx
    push cx
    push si
    call bas_str_vars_zero
    mov ax, [bas_str_heap_seg]
    test ax, ax
    jz .bshr_done
    mov bx, [bas_str_heap_off]
    call mn_free
    mov word [bas_str_heap_seg], 0
    mov word [bas_str_heap_off], 0
.bshr_done:
    mov word [bas_str_top], 0
    pop si
    pop cx
    pop bx
    pop ax
    ret

; ----------------------------------------------------------------------------
; bas_str_heap_reset — CLEAR-mode reset.  Resets bas_str_top + zeroes
; per-variable descriptors but KEEPS the underlying HMA allocation.
; ----------------------------------------------------------------------------
bas_str_heap_reset:
    push ax
    push cx
    push si
    call bas_str_vars_zero
    mov word [bas_str_top], 0
    pop si
    pop cx
    pop ax
    ret

; ----------------------------------------------------------------------------
; bas_str_vars_zero — internal: walk var table, zero seg/off/len on every
; BVAR_STRING record.  Used by both NEW and CLEAR.
; ----------------------------------------------------------------------------
bas_str_vars_zero:
    push cx
    push si
    mov cx, [bas_var_count]
    test cx, cx
    jz .bsvz_done
    mov si, bas_var_table
.bsvz_walk:
    cmp byte [si + BVR_TYPE], BVAR_STRING
    jne .bsvz_skip
    mov word [si + BVR_VAL_LO], 0
    mov word [si + BVR_VAL_HI], 0
    mov word [si + BVR_LEN_DIM], 0
.bsvz_skip:
    add si, BAS_VAR_REC_SIZE
    loop .bsvz_walk
.bsvz_done:
    pop si
    pop cx
    ret

; ----------------------------------------------------------------------------
; bas_str_var_alloc_slot — reserve an 80-byte slot in the string-var heap.
;   Out: AX = bas_str_heap_seg, BX = absolute offset (heap base + slot off)
;        CF=0 ok; CF=1 → AL = BERR_OOM
; ----------------------------------------------------------------------------
bas_str_var_alloc_slot:
    push cx
    call bas_str_heap_ensure
    jc .bsvas_fail
    mov cx, [bas_str_top]
    mov bx, cx
    add bx, BAS_STR_VAR_LEN
    cmp bx, BAS_STR_HEAP_SIZE
    ja .bsvas_full
    mov [bas_str_top], bx
    mov bx, [bas_str_heap_off]
    add bx, cx                      ; BX = heap base + old top
    mov ax, [bas_str_heap_seg]
    pop cx
    clc
    ret
.bsvas_full:
    pop cx
    mov al, BERR_OOM
    stc
    ret
.bsvas_fail:
    pop cx
    stc
    ret

; ----------------------------------------------------------------------------
; bas_str_temp_alloc — HMA-alloc a temp string of CX bytes and record the
; descriptor in bas_temp_pool.
;   In:  CX = byte count (0..BAS_STR_MAX_TEMP)
;   Out: AX = seg (HMA), BX = off; pool extended.
;        CF=1 → AL = BERR_STRTOOLONG / BERR_TOOCOMPLEX / BERR_OOM
; ----------------------------------------------------------------------------
bas_str_temp_alloc:
    push cx
    push dx
    push di
    cmp cx, BAS_STR_MAX_TEMP
    ja .bsta_long
    cmp word [bas_temp_top], BAS_TEMP_STR_MAX
    jae .bsta_complex
    mov dx, cx
    test dx, dx
    jnz .bsta_doalloc
    mov dx, 1                       ; mn_alloc rejects 0 — fake an empty alloc
.bsta_doalloc:
    push cx
    mov cx, dx
    mov dl, 0
    call mn_alloc
    pop cx
    jc .bsta_oom
    mov di, [bas_temp_top]
    shl di, 2
    add di, bas_temp_pool
    mov [di], ax
    mov [di+2], bx
    inc word [bas_temp_top]
    pop di
    pop dx
    pop cx
    clc
    ret
.bsta_long:
    pop di
    pop dx
    pop cx
    mov al, BERR_STRTOOLONG
    stc
    ret
.bsta_complex:
    pop di
    pop dx
    pop cx
    mov al, BERR_TOOCOMPLEX
    stc
    ret
.bsta_oom:
    pop di
    pop dx
    pop cx
    mov al, BERR_OOM
    stc
    ret

; ----------------------------------------------------------------------------
; bas_str_copy — copy CX bytes from src_seg:src_off to dst_seg:dst_off.
;   In:  AX = src_seg, BX = src_off, DX = dst_seg, DI = dst_off, CX = len
;   Clobbers: DS, ES (restored), SI, DI, AX, CX
; ----------------------------------------------------------------------------
bas_str_copy:
    push ds
    push es
    push si
    push di
    push ax
    push bx
    push cx
    test cx, cx
    jz .bsc_done
    mov ds, ax
    mov si, bx
    mov es, dx
    cld
    rep movsb
.bsc_done:
    pop cx
    pop bx
    pop ax
    pop di
    pop si
    pop es
    pop ds
    ret

; ----------------------------------------------------------------------------
; bas_str_print_desc — print the string described by an 8-byte descriptor.
;   In:  DS:SI = descriptor (val_lo=seg, val_hi=off, len)
;   Clobbers: none visible (ES saved/restored)
; ----------------------------------------------------------------------------
bas_str_print_desc:
    push ax
    push bx
    push cx
    push es
    mov ax, [si + BES_VAL_LO]
    mov bx, [si + BES_VAL_HI]
    mov cx, [si + BES_LEN]
    test cx, cx
    jz .bspd_done
    mov es, ax
.bspd_loop:
    mov al, [es:bx]
    push bx
    push cx
    call bas_lex_putc
    pop cx
    pop bx
    inc bx
    loop .bspd_loop
.bspd_done:
    pop es
    pop cx
    pop bx
    pop ax
    ret

; ----------------------------------------------------------------------------
; bas_str_byteat — read one byte from a (seg, off) tuple.
;   In:  AX = seg, BX = off
;   Out: AL = byte
;   Clobbers: ES (saved/restored)
; ----------------------------------------------------------------------------
bas_str_byteat:
    push es
    mov es, ax
    mov al, [es:bx]
    pop es
    ret

; ----------------------------------------------------------------------------
; bas_str_cmp_desc — byte-wise lexicographic compare of two descriptors.
; Handles descriptors with different segments (literal in DS vs var in HMA).
;   In:  DS:SI = LHS descriptor (8 bytes), DS:DI = RHS descriptor
;   Out: DX = -1 / 0 / +1.  Flags set by CMP DX, 0.
;   Clobbers: AX, BX, CX, ES
; ----------------------------------------------------------------------------
bas_str_cmp_desc:
    push si
    push di
    push bp
    mov bp, sp
    sub sp, 8
    ; Local scratch (BP-relative):
    ;   [BP-2] = LHS seg     [BP-4] = LHS off
    ;   [BP-6] = RHS seg     [BP-8] = RHS off
    mov ax, [si + BES_VAL_LO]
    mov [bp-2], ax
    mov ax, [si + BES_VAL_HI]
    mov [bp-4], ax
    mov ax, [di + BES_VAL_LO]
    mov [bp-6], ax
    mov ax, [di + BES_VAL_HI]
    mov [bp-8], ax
    mov ax, [si + BES_LEN]          ; LLen
    mov cx, [di + BES_LEN]          ; RLen
    mov dx, ax
    cmp dx, cx
    jbe .bscd_min_ok
    mov dx, cx
.bscd_min_ok:
    test dx, dx
    jz .bscd_prefix_eq
.bscd_loop:
    ; LHS byte
    mov ax, [bp-2]
    mov bx, [bp-4]
    call bas_str_byteat
    mov ah, al                      ; stash LHS byte
    ; RHS byte
    push ax
    mov ax, [bp-6]
    mov bx, [bp-8]
    call bas_str_byteat
    mov bl, al                      ; BL = RHS byte
    pop ax                          ; AH = LHS byte
    cmp ah, bl
    jb .bscd_lt
    ja .bscd_gt
    inc word [bp-4]
    inc word [bp-8]
    dec dx
    jnz .bscd_loop
.bscd_prefix_eq:
    mov ax, [si + BES_LEN]
    mov cx, [di + BES_LEN]
    cmp ax, cx
    je .bscd_eq
    jb .bscd_lt
    jmp .bscd_gt
.bscd_lt:
    mov dx, -1
    jmp .bscd_done
.bscd_gt:
    mov dx, 1
    jmp .bscd_done
.bscd_eq:
    xor dx, dx
.bscd_done:
    mov sp, bp
    pop bp
    pop di
    pop si
    cmp dx, 0
    ret

; ----------------------------------------------------------------------------
; bas_str_concat — concatenate two descriptors into bas_expr_result as a
; new BSTR_TEMP string.
;   In:  DS:SI = LHS descriptor, DS:DI = RHS descriptor
;   Out: bas_expr_result holds the concatenated string (TYPE='S' FLAGS=TEMP).
;        CF=0 ok; CF=1 → AL = BERR_*
; ----------------------------------------------------------------------------
bas_str_concat:
    push si                         ; saved SI ([bp+4] after push bp) = LHS desc ptr
    push di                         ; saved DI ([bp+2] after push bp) = RHS desc ptr
    push bp
    mov bp, sp
    sub sp, 8
    ; Frame layout (after `call ... push si / push di / push bp`):
    ;   [bp+6] = return IP
    ;   [bp+4] = saved SI = LHS desc ptr
    ;   [bp+2] = saved DI = RHS desc ptr
    ;   [bp+0] = saved BP
    ;   [bp-2] = dst seg
    ;   [bp-4] = dst off
    ;   [bp-6] = LLen
    ;   [bp-8] = total len (LLen + RLen)
    mov ax, [si + BES_LEN]
    mov [bp-6], ax
    mov cx, [di + BES_LEN]
    add ax, cx
    mov [bp-8], ax
    cmp ax, BAS_STR_MAX_TEMP
    ja .bscat_long
    mov cx, ax
    call bas_str_temp_alloc         ; AX=seg, BX=off
    jc .bscat_fail
    mov [bp-2], ax
    mov [bp-4], bx
    ; Copy LHS bytes into dst
    mov si, [bp+4]                  ; LHS desc ptr (saved SI)
    mov ax, [si + BES_VAL_LO]
    mov bx, [si + BES_VAL_HI]
    mov cx, [bp-6]                  ; LLen
    mov dx, [bp-2]                  ; dst seg
    mov di, [bp-4]                  ; dst off
    call bas_str_copy
    ; Copy RHS bytes after LHS bytes
    mov si, [bp+2]                  ; RHS desc ptr (saved DI)
    mov ax, [si + BES_VAL_LO]
    mov bx, [si + BES_VAL_HI]
    mov cx, [si + BES_LEN]
    mov dx, [bp-2]
    mov di, [bp-4]
    add di, [bp-6]
    call bas_str_copy
    ; Fill bas_expr_result with the new TEMP descriptor
    mov ax, [bp-2]
    mov [bas_expr_result + BES_VAL_LO], ax
    mov ax, [bp-4]
    mov [bas_expr_result + BES_VAL_HI], ax
    mov ax, [bp-8]
    mov [bas_expr_result + BES_LEN], ax
    mov byte [bas_expr_result + BES_TYPE], BVAR_STRING
    mov byte [bas_expr_result + BES_FLAGS], BSTR_TEMP
    mov sp, bp
    pop bp
    pop di
    pop si
    clc
    ret
.bscat_long:
    mov sp, bp
    pop bp
    pop di
    pop si
    mov al, BERR_STRTOOLONG
    stc
    ret
.bscat_fail:
    mov sp, bp
    pop bp
    pop di
    pop si
    stc
    ret

; ----------------------------------------------------------------------------
; bas_var_get_str — fetch string variable descriptor.
;   In:  AL = name0 (must have bit 7 set), AH = name1
;   Out: bas_expr_result filled (TYPE='S' FLAGS=BSTR_VAR; empty if undef).
;        CF=0 ok; CF=1 → AL = BERR_TYPE if name matches a non-string var.
; ----------------------------------------------------------------------------
bas_var_get_str:
    push bx
    push dx
    test al, 0x80
    jz .bvgs_type
    call bas_var_find
    jc .bvgs_undef
    cmp byte [bx + BVR_TYPE], BVAR_STRING
    jne .bvgs_type
    mov dx, [bx + BVR_VAL_LO]
    mov [bas_expr_result + BES_VAL_LO], dx
    mov dx, [bx + BVR_VAL_HI]
    mov [bas_expr_result + BES_VAL_HI], dx
    mov dx, [bx + BVR_LEN_DIM]
    mov [bas_expr_result + BES_LEN], dx
    mov byte [bas_expr_result + BES_TYPE], BVAR_STRING
    mov byte [bas_expr_result + BES_FLAGS], BSTR_VAR
    pop dx
    pop bx
    clc
    ret
.bvgs_undef:
    mov word [bas_expr_result + BES_VAL_LO], 0
    mov word [bas_expr_result + BES_VAL_HI], 0
    mov word [bas_expr_result + BES_LEN], 0
    mov byte [bas_expr_result + BES_TYPE], BVAR_STRING
    mov byte [bas_expr_result + BES_FLAGS], BSTR_VAR
    pop dx
    pop bx
    clc
    ret
.bvgs_type:
    pop dx
    pop bx
    mov al, BERR_TYPE
    stc
    ret

; ----------------------------------------------------------------------------
; bas_var_set_str — assign bas_expr_result (a string descriptor) to a
; string variable.  Creates the var + reserves 80B slot on first use.
; Always copies bytes into the var's own slot (never aliases).
;   In:  AL = name0 (with $ flag), AH = name1
;        bas_expr_result holds the source string descriptor
;   Out: CF=0 ok; CF=1 → AL = BERR_*
; ----------------------------------------------------------------------------
bas_var_set_str:
    push bx
    push cx
    push dx
    push di
    push bp
    mov bp, sp
    sub sp, 4
    ; Locals:
    ;   [bp-2] = saved name (AL,AH)
    ;   [bp-4] = var record ptr
    test al, 0x80
    jz .bvss_type
    mov cx, [bas_expr_result + BES_LEN]
    cmp cx, BAS_STR_VAR_LEN
    ja .bvss_long
    mov [bp-2], ax
    ; Find or create the var record
    call bas_var_find               ; BX = rec ptr; CF=1 if missing
    jnc .bvss_have_rec
    mov ax, [bp-2]
    mov dl, BVAR_STRING
    call bas_var_create             ; BX = new rec ptr
    jc .bvss_propagate
.bvss_have_rec:
    mov [bp-4], bx
    ; If no slot reserved yet (seg == 0), allocate one
    cmp word [bx + BVR_VAL_LO], 0
    jne .bvss_slot_ok
    call bas_str_var_alloc_slot     ; AX=seg, BX=off
    jc .bvss_propagate
    mov dx, ax                      ; DX = slot seg
    mov di, bx                      ; DI = slot off
    mov bx, [bp-4]
    mov [bx + BVR_VAL_LO], dx
    mov [bx + BVR_VAL_HI], di
.bvss_slot_ok:
    mov bx, [bp-4]
    mov cx, [bas_expr_result + BES_LEN]
    mov [bx + BVR_LEN_DIM], cx
    test cx, cx
    jz .bvss_done
    mov dx, [bx + BVR_VAL_LO]       ; dst seg
    mov di, [bx + BVR_VAL_HI]       ; dst off
    mov ax, [bas_expr_result + BES_VAL_LO]
    mov bx, [bas_expr_result + BES_VAL_HI]
    call bas_str_copy
.bvss_done:
    mov sp, bp
    pop bp
    pop di
    pop dx
    pop cx
    pop bx
    clc
    ret
.bvss_type:
    mov sp, bp
    pop bp
    pop di
    pop dx
    pop cx
    pop bx
    mov al, BERR_TYPE
    stc
    ret
.bvss_long:
    mov sp, bp
    pop bp
    pop di
    pop dx
    pop cx
    pop bx
    mov al, BERR_STRTOOLONG
    stc
    ret
.bvss_propagate:
    mov sp, bp
    pop bp
    pop di
    pop dx
    pop cx
    pop bx
    stc
    ret

; ----------------------------------------------------------------------------
; bas_result_set_int — fill bas_expr_result as a numeric.
;   In: DX = integer value
;   Clobbers: nothing visible
; ----------------------------------------------------------------------------
bas_result_set_int:
    mov [bas_expr_result + BES_VAL_LO], dx
    mov word [bas_expr_result + BES_VAL_HI], 0
    mov word [bas_expr_result + BES_LEN], 0
    mov byte [bas_expr_result + BES_TYPE], BVAR_NUMERIC
    mov byte [bas_expr_result + BES_FLAGS], 0
    ret

; ----------------------------------------------------------------------------
; bas_result_save_lhs — snapshot bas_expr_result to bas_str_lhs (8 B).
; Pair with bas_result_load_lhs to recover.  Used by binary operators that
; need to evaluate RHS without losing LHS.  Safe across bas_expr_pow (which
; uses bas_scratch_a).
; ----------------------------------------------------------------------------
bas_result_save_lhs:
    push ax
    mov ax, [bas_expr_result + 0]
    mov [bas_str_lhs + 0], ax
    mov ax, [bas_expr_result + 2]
    mov [bas_str_lhs + 2], ax
    mov ax, [bas_expr_result + 4]
    mov [bas_str_lhs + 4], ax
    mov ax, [bas_expr_result + 6]
    mov [bas_str_lhs + 6], ax
    pop ax
    ret

; ----------------------------------------------------------------------------
; bas_result_load_lhs — inverse of save: copies bas_str_lhs back into
; bas_expr_result.
; ----------------------------------------------------------------------------
bas_result_load_lhs:
    push ax
    mov ax, [bas_str_lhs + 0]
    mov [bas_expr_result + 0], ax
    mov ax, [bas_str_lhs + 2]
    mov [bas_expr_result + 2], ax
    mov ax, [bas_str_lhs + 4]
    mov [bas_expr_result + 4], ax
    mov ax, [bas_str_lhs + 6]
    mov [bas_expr_result + 6], ax
    pop ax
    ret


; ====== Phase 2 string functions appended below ======
; ============================================================================
; bas_str_put_temp_result — fill bas_expr_result as a TEMP string descriptor.
;   In:  AX=seg, BX=off, CX=len
; ============================================================================
bas_str_put_temp_result:
    mov [bas_expr_result + BES_VAL_LO], ax
    mov [bas_expr_result + BES_VAL_HI], bx
    mov [bas_expr_result + BES_LEN], cx
    mov byte [bas_expr_result + BES_TYPE], BVAR_STRING
    mov byte [bas_expr_result + BES_FLAGS], BSTR_TEMP
    ret

; ============================================================================
; bas_strfn_len — LEN(s$): returns length of string at bas_expr_result.
;   In:  bas_expr_result holds string descriptor
;   Out: DX = length; bas_expr_result rewritten as numeric.
; ============================================================================
bas_strfn_len:
    mov dx, [bas_expr_result + BES_LEN]
    mov byte [bas_expr_result + BES_TYPE], BVAR_NUMERIC
    mov [bas_expr_result + BES_VAL_LO], dx
    clc
    ret

; ============================================================================
; bas_strfn_asc — ASC(s$): returns ASCII of first byte (BERR_ILLEGAL if empty).
; ============================================================================
bas_strfn_asc:
    mov cx, [bas_expr_result + BES_LEN]
    test cx, cx
    jz .bsfa_empty
    push es
    mov ax, [bas_expr_result + BES_VAL_LO]
    mov bx, [bas_expr_result + BES_VAL_HI]
    mov es, ax
    mov al, [es:bx]
    xor ah, ah
    mov dx, ax
    pop es
    mov byte [bas_expr_result + BES_TYPE], BVAR_NUMERIC
    mov [bas_expr_result + BES_VAL_LO], dx
    clc
    ret
.bsfa_empty:
    mov al, BERR_BADFUNC
    stc
    ret

; ============================================================================
; bas_strfn_val — VAL(s$): parses leading signed decimal; 0 if no digits.
; Copies string into bas_line_buf NUL-terminated (per-call cap = 255 bytes)
; then calls bas_parse_int.  Empty string or no digits → DX=0.
; ============================================================================
bas_strfn_val:
    push si
    push di
    push es
    mov cx, [bas_expr_result + BES_LEN]
    cmp cx, 255
    jbe .bsfv_lenok
    mov cx, 255
.bsfv_lenok:
    mov ax, [bas_expr_result + BES_VAL_LO]
    mov bx, [bas_expr_result + BES_VAL_HI]
    mov es, ax
    mov di, bas_line_buf
.bsfv_copy:
    test cx, cx
    jz .bsfv_copied
    mov al, [es:bx]
    mov [di], al
    inc bx
    inc di
    dec cx
    jmp .bsfv_copy
.bsfv_copied:
    mov byte [di], 0
    mov si, bas_line_buf
    call bas_parse_int
    jnc .bsfv_ok
    xor dx, dx
.bsfv_ok:
    pop es
    pop di
    pop si
    mov byte [bas_expr_result + BES_TYPE], BVAR_NUMERIC
    mov [bas_expr_result + BES_VAL_LO], dx
    clc
    ret

; ============================================================================
; bas_strfn_chrs — CHR$(n): single-byte temp string.
;   In:  DX = byte value (low 8 bits used)
; ============================================================================
bas_strfn_chrs:
    push dx
    mov cx, 1
    call bas_str_temp_alloc
    pop dx
    jc .bsfc_fail
    push es
    push bx
    mov es, ax
    mov [es:bx], dl
    pop bx
    pop es
    mov cx, 1
    call bas_str_put_temp_result
    clc
    ret
.bsfc_fail:
    ret

; ============================================================================
; bas_strfn_strs — STR$(n): format DX as decimal into a TEMP string.
; GW-BASIC convention: leading space for non-negative, '-' for negative.
; ============================================================================
bas_strfn_strs:
    push di
    push si
    mov di, bas_token_buf + 16
    mov ax, dx
    mov si, 0
    test ax, ax
    jns .bsfs_pos
    neg ax
    mov si, 1
.bsfs_pos:
    mov cx, 10
.bsfs_loop:
    xor dx, dx
    div cx
    add dl, '0'
    dec di
    mov [di], dl
    test ax, ax
    jnz .bsfs_loop
    dec di
    test si, si
    jnz .bsfs_neg
    mov byte [di], ' '
    jmp .bsfs_done
.bsfs_neg:
    mov byte [di], '-'
.bsfs_done:
    mov ax, bas_token_buf + 16
    sub ax, di
    mov cx, ax
    push di
    push cx
    call bas_str_temp_alloc
    pop cx
    pop si
    jc .bsfs_fail
    push ax
    push bx
    push cx
    mov dx, ax
    push di
    mov di, bx
    mov ax, ds
    mov bx, si
    call bas_str_copy
    pop di
    pop cx
    pop bx
    pop ax
    call bas_str_put_temp_result
    pop si
    pop di
    clc
    ret
.bsfs_fail:
    pop si
    pop di
    ret

; ============================================================================
; bas_strfn_lefts — LEFT$(s$, n): first n bytes (n clamped to len).
;   In:  bas_str_lhs = string descriptor, DX = n
; ============================================================================
bas_strfn_lefts:
    push si
    push di
    test dx, dx
    jns .bsfl_npos
    xor dx, dx
.bsfl_npos:
    mov cx, [bas_str_lhs + BES_LEN]
    cmp dx, cx
    jbe .bsfl_lenok
    mov dx, cx
.bsfl_lenok:
    mov cx, dx
    call bas_str_temp_alloc
    jc .bsfl_fail
    push ax
    push bx
    push cx
    mov dx, ax
    mov di, bx
    mov ax, [bas_str_lhs + BES_VAL_LO]
    mov bx, [bas_str_lhs + BES_VAL_HI]
    call bas_str_copy
    pop cx
    pop bx
    pop ax
    call bas_str_put_temp_result
    pop di
    pop si
    clc
    ret
.bsfl_fail:
    pop di
    pop si
    ret

; ============================================================================
; bas_strfn_rights — RIGHT$(s$, n): last n bytes.
;   In:  bas_str_lhs = source string, DX = n
; ============================================================================
bas_strfn_rights:
    push si
    push di
    test dx, dx
    jns .bsfr_npos
    xor dx, dx
.bsfr_npos:
    mov cx, [bas_str_lhs + BES_LEN]
    cmp dx, cx
    jbe .bsfr_lenok
    mov dx, cx
.bsfr_lenok:
    mov cx, dx                      ; CX = bytes to copy
    call bas_str_temp_alloc         ; AX=seg BX=off
    jc .bsfr_fail
    push ax
    push bx
    push cx
    mov dx, ax                      ; dst seg
    mov di, bx                      ; dst off
    mov ax, [bas_str_lhs + BES_VAL_LO]
    mov bx, [bas_str_lhs + BES_VAL_HI]
    ; Source offset = src.off + (src.len - n)
    mov si, [bas_str_lhs + BES_LEN]
    sub si, cx                      ; SI = offset adjustment
    add bx, si
    call bas_str_copy
    pop cx
    pop bx
    pop ax
    call bas_str_put_temp_result
    pop di
    pop si
    clc
    ret
.bsfr_fail:
    pop di
    pop si
    ret

; ============================================================================
; bas_strfn_mids — MID$(s$, start, [len]).
;   In:  bas_str_lhs = source string
;        bas_scratch_b = start (1-based), bas_scratch_c = length (-1 = rest)
; ============================================================================
bas_strfn_mids:
    push si
    push di
    mov ax, [bas_scratch_b]
    cmp ax, 1
    jge .bsfm_startok
    mov ax, 1
.bsfm_startok:
    dec ax
    mov cx, [bas_str_lhs + BES_LEN]
    cmp ax, cx
    jb .bsfm_have_start
    xor cx, cx
    jmp .bsfm_alloc
.bsfm_have_start:
    sub cx, ax
    mov dx, [bas_scratch_c]
    test dx, dx
    js .bsfm_alloc
    cmp dx, cx
    jbe .bsfm_dxok
    mov dx, cx
.bsfm_dxok:
    mov cx, dx
.bsfm_alloc:
    push ax
    call bas_str_temp_alloc
    pop si
    jc .bsfm_fail
    push ax
    push bx
    push cx
    mov dx, ax
    mov di, bx
    mov ax, [bas_str_lhs + BES_VAL_LO]
    mov bx, [bas_str_lhs + BES_VAL_HI]
    add bx, si
    call bas_str_copy
    pop cx
    pop bx
    pop ax
    call bas_str_put_temp_result
    pop di
    pop si
    clc
    ret
.bsfm_fail:
    pop di
    pop si
    ret

; ============================================================================
; bas_strfn_inkeys — INKEY$: returns string of 0 or 1 character.
; ============================================================================
bas_strfn_inkeys:
    call mn_peek_key
    jz .bsfi_empty
    call mn_read_key
    push ax
    mov cx, 1
    call bas_str_temp_alloc
    pop dx
    jc .bsfi_fail
    push es
    mov es, ax
    mov [es:bx], dl
    pop es
    mov cx, 1
    call bas_str_put_temp_result
    clc
    ret
.bsfi_empty:
    mov cx, 0
    call bas_str_temp_alloc
    jc .bsfi_fail
    xor cx, cx
    call bas_str_put_temp_result
    clc
    ret
.bsfi_fail:
    ret
