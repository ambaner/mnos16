; =============================================================================
; basic_var.inc — Variable table: lookup, create, get, set.
;
; The variable table is a packed array of BAS_VAR_REC_SIZE-byte records at
; bas_var_table, capacity BAS_MAX_VARS (= 128).  Records:
;
;   +0  byte  name0   (letter index 0..25 | (string_flag << 7))
;   +1  byte  name1   (digit 0..9, or 0xFF if absent)
;   +2  byte  type    BVAR_NUMERIC / BVAR_STRING / BVAR_NUM_ARRAY / BVAR_STR_ARRAY
;   +3  byte  flags   (reserved)
;   +4  word  value/seg
;   +6  word  string-offset / array-offset
;   +8  word  string-length / array-upper-bound
;   +10 byte  element-type (for arrays)
;   +11 .. 15 reserved
;
; bas_var_find:
;   In:  AL = name0  (low 5 bits = letter, bit 7 = $ flag)
;        AH = name1
;   Out: BX = pointer to record (DS-relative)
;        CF=0 if found, CF=1 if not found
;        DX = unchanged
;
; bas_var_create:
;   In:  AL = name0, AH = name1, DL = type tag
;   Out: BX = pointer to new record (zero-initialised type/value)
;        CF=0 ok, CF=1 → AL = BERR_TOOMANYVARS
;
; bas_var_get_int:
;   In:  AL = name0, AH = name1 (must be numeric)
;   Out: BX = signed integer value (auto-created if missing → 0)
;        CF=0 ok, CF=1 → AL = BERR_* (TYPE on mismatch)
;
; bas_var_set_int:
;   In:  AL = name0, AH = name1, DX = value (must not have $ flag)
;   Out: CF=0 ok, CF=1 → AL = BERR_*
;
; All routines preserve unused registers.  DS = SS by convention.
; =============================================================================

bas_var_find:
    push cx
    push si
    mov cx, [bas_var_count]
    test cx, cx
    jz .bvf_miss
    mov si, bas_var_table
.bvf_loop:
    mov bl, [si]
    cmp bl, al
    jne .bvf_skip
    mov bl, [si+1]
    cmp bl, ah
    jne .bvf_skip
    mov bx, si
    pop si
    pop cx
    clc
    ret
.bvf_skip:
    add si, BAS_VAR_REC_SIZE
    loop .bvf_loop
.bvf_miss:
    pop si
    pop cx
    stc
    ret


bas_var_create:
    push cx
    push si
    push di
    push es
    mov cx, [bas_var_count]
    cmp cx, BAS_MAX_VARS
    jae .bvc_full
    mov bx, BAS_VAR_REC_SIZE
    push ax
    push dx                         ; preserve type tag — mul clobbers DX
    mov ax, cx
    mul bx
    pop dx
    mov bx, ax
    pop ax
    add bx, bas_var_table
    ; Zero the record (16 bytes)
    mov si, ds
    mov es, si
    mov di, bx
    push ax
    push cx
    mov cx, BAS_VAR_REC_SIZE
    xor al, al
    cld
    rep stosb
    pop cx
    pop ax
    ; Fill identity
    mov [bx], al
    mov [bx+1], ah
    mov [bx+2], dl
    mov byte [bx+3], 0
    inc cx
    mov [bas_var_count], cx
    pop es
    pop di
    pop si
    pop cx
    clc
    ret
.bvc_full:
    pop es
    pop di
    pop si
    pop cx
    mov al, BERR_TOOMANYVARS
    stc
    ret


; ---- bas_var_get_int: get scalar int variable value -----------------------
bas_var_get_int:
    push cx
    push dx
    ; Reject string names
    test al, 0x80
    jnz .bvgi_type
    call bas_var_find
    jc .bvgi_create
    ; Found: type check
    cmp byte [bx + BVR_TYPE], BVAR_NUMERIC
    jne .bvgi_type
    mov bx, [bx + BVR_VAL_LO]
    pop dx
    pop cx
    clc
    ret
.bvgi_create:
    mov dl, BVAR_NUMERIC
    call bas_var_create
    jc .bvgi_fail_passthru
    ; Newly created: value = 0
    xor bx, bx
    pop dx
    pop cx
    clc
    ret
.bvgi_type:
    pop dx
    pop cx
    mov al, BERR_TYPE
    stc
    ret
.bvgi_fail_passthru:
    pop dx
    pop cx
    stc
    ret


; ---- bas_var_set_int: assign DX to scalar int variable --------------------
bas_var_set_int:
    push cx
    test al, 0x80
    jnz .bvsi_type
    call bas_var_find
    jc .bvsi_create
    cmp byte [bx + BVR_TYPE], BVAR_NUMERIC
    jne .bvsi_type
    mov [bx + BVR_VAL_LO], dx
    pop cx
    clc
    ret
.bvsi_create:
    push dx
    mov dl, BVAR_NUMERIC
    call bas_var_create
    pop dx
    jc .bvsi_fail_passthru
    mov [bx + BVR_VAL_LO], dx
    pop cx
    clc
    ret
.bvsi_type:
    pop cx
    mov al, BERR_TYPE
    stc
    ret
.bvsi_fail_passthru:
    pop cx
    stc
    ret
