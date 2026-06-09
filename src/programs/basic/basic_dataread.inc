; =============================================================================
; basic_dataread.inc — DATA / READ / RESTORE statements + DATA cursor.
;
; Tokenization
;   DATA <payload>   →   TOK_DATA + TOK_DATA_RAW + len + raw payload bytes
;                        (See bas_lex_emit_data_payload in basic_lex.inc.)
;   READ a, b$, c    →   TOK_READ + var refs + commas
;   RESTORE [<n>]    →   TOK_RESTORE [+ TOK_LINEREF + word]
;
; Runtime
;   DATA   — no-op at execution time (.brd_eat_to_sep skips TOK_DATA_RAW).
;   READ   — pulls one comma-separated item per variable from the DATA stream.
;            String vars take raw bytes (with surrounding "" stripped if any).
;            Numeric vars require a parseable signed decimal.
;   RESTORE — without arg: cursor restarts at first DATA token in program.
;             with line#: cursor restarts at first DATA at/after that line.
;
; Cursor (BSS, defined in basic_data.inc):
;   bas_data_line_ptr   word  absolute address of TOK_DATA_RAW *length byte*
;                              0      = uninitialized — restart on next READ
;                              0xFFFF = exhausted — next READ → BERR_OUTOFDATA
;   bas_data_sub_off    word  byte offset within current DATA payload
;                              (0 ≤ sub_off ≤ payload_len)
;
; bas_cmd_clear zeroes the cursor; RESTORE explicitly does the same (or seeks).
;
; Calling convention: DS:SI = next token; CF=0 ok, CF=1 → AL = BERR_*.
; =============================================================================


; =============================================================================
; bas_stmt_data — runtime no-op.  TOK_DATA_RAW payload is skipped by
; .brd_eat_to_sep (treats it like TOK_STR_LIT: 1-byte length + bytes).
; =============================================================================
bas_stmt_data:
    clc
    ret


; =============================================================================
; bas_stmt_restore — DS:SI = first token after TOK_RESTORE.
; Optional line-number argument (TOK_LINEREF + word); no arg → reset cursor.
; =============================================================================
bas_stmt_restore:
    push ax
    push bx
    mov al, [si]
    cmp al, TOK_EOL
    je .bsre_no_arg
    cmp al, ':'
    je .bsre_no_arg
    cmp al, TOK_LINEREF
    jne .bsre_syntax
    inc si
    mov ax, [si]
    add si, 2
    call bas_data_find_first        ; In: AX = min line#; Out: BX = abs addr or 0xFFFF
    mov [bas_data_line_ptr], bx
    xor ax, ax
    mov [bas_data_sub_off], ax
    pop bx
    pop ax
    clc
    ret
.bsre_no_arg:
    xor ax, ax
    mov [bas_data_line_ptr], ax
    mov [bas_data_sub_off], ax
    pop bx
    pop ax
    clc
    ret
.bsre_syntax:
    pop bx
    pop ax
    mov al, BERR_SYNTAX
    stc
    ret


; =============================================================================
; bas_stmt_read — DS:SI = first token after TOK_READ.
; Parses comma-separated variable list; assigns each from next DATA item.
; =============================================================================
bas_stmt_read:
    push bx
    push cx
    push dx
    push di
    push es
.bsr_loop:
    mov al, [si]
    cmp al, TOK_VAR_NAME
    jne .bsr_syntax
    inc si
    mov al, [si]                    ; name0
    inc si
    mov ah, [si]                    ; name1
    inc si
    ; Stash name in bas_scratch_b (helpers freely clobber AX/DX).
    ; NOTE: must NOT use bas_scratch_d here — that slot is owned by
    ; bas_run_program_from_cur across the bas_run_direct → bas_stmt_read
    ; call chain (it stores the precomputed next-line offset there).
    ; Clobbering it would cause control to jump to a garbage line offset.
    mov [bas_scratch_b], ax
    test al, 0x80
    jnz .bsr_str_var

    ; --- Numeric variable: parse decimal from next DATA item -----------
    call bas_data_next_item         ; ES:DI = bytes, CX = len, AL = 'S'/'N'
    jc .bsr_propagate
    ; Copy bytes into bas_token_buf (DS-relative) with NUL terminator so
    ; bas_parse_int (which reads via DS:SI) can scan them.
    push si
    mov si, di                      ; SI = item start in prog area (DS = SS)
    ; Note: bas_data_next_item leaves ES = DS (we never change ES inside it).
    mov di, bas_token_buf
.bsr_num_copy:
    test cx, cx
    jz .bsr_num_done
    mov al, [si]
    inc si
    mov [di], al
    inc di
    dec cx
    jmp .bsr_num_copy
.bsr_num_done:
    mov byte [di], 0
    mov si, bas_token_buf
    call bas_parse_int              ; Out: DX = value, CF=1 if not numeric
    pop si
    jc .bsr_type_err
    mov ax, [bas_scratch_b]
    call bas_var_set_int
    jc .bsr_propagate
    jmp .bsr_after_item

.bsr_str_var:
    ; --- String variable: build BSTR_LITERAL descriptor, set var ------
    call bas_data_next_item         ; ES:DI = bytes, CX = len
    jc .bsr_propagate
    ; Build descriptor in bas_expr_result (literal pointing into prog area)
    mov ax, ds
    mov [bas_expr_result + BES_VAL_LO], ax
    mov [bas_expr_result + BES_VAL_HI], di
    mov [bas_expr_result + BES_LEN], cx
    mov byte [bas_expr_result + BES_TYPE], 'S'
    mov byte [bas_expr_result + BES_FLAGS], BSTR_LITERAL
    mov ax, [bas_scratch_b]
    call bas_var_set_str
    jc .bsr_propagate

.bsr_after_item:
    ; If next token is ',', consume and loop; else end of statement.
    mov al, [si]
    cmp al, ','
    jne .bsr_done
    inc si
    jmp .bsr_loop

.bsr_done:
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    clc
    ret
.bsr_propagate:
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    stc
    ret
.bsr_syntax:
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    mov al, BERR_SYNTAX
    stc
    ret
.bsr_type_err:
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    mov al, BERR_TYPE
    stc
    ret


; =============================================================================
; bas_data_next_item — fetch next DATA item, advancing cursor past it
;                      and the trailing comma (if any).
;
; Out: ES:DI = pointer to first byte of item (in bas_prog_buf; ES = DS)
;      CX    = item length in bytes (0..payload_max)
;      AL    = 'S' if item was quoted (descriptor type hint), 'N' otherwise
;      CF=0 ok; CF=1 → AL = BERR_OUTOFDATA
;
; Item parsing
;   - Skip leading WS (space/tab) within payload.
;   - If first byte is '"': item runs until matching '"'.  Bytes between
;     the quotes are the item (no quotes included).  After the closing '"',
;     skip WS; expect ',' or end of payload.
;   - Else: item runs until ',' or end of payload.  Trailing WS is stripped.
;   - Empty unquoted item (e.g. ",,") → 0-length 'N' item.
; =============================================================================
bas_data_next_item:
    push bx
    push dx
    push si
    mov ax, ds
    mov es, ax                      ; ES = DS (we work in our own segment)

.bdn_top:
    ; If cursor is exhausted → fail
    mov bx, [bas_data_line_ptr]
    cmp bx, 0xFFFF
    je .bdn_exhausted
    test bx, bx
    jnz .bdn_have_line
    ; Cursor uninitialized — find first DATA
    xor ax, ax                      ; line# = 0 means "first DATA in program"
    call bas_data_find_first        ; Out: BX
    mov [bas_data_line_ptr], bx
    xor ax, ax
    mov [bas_data_sub_off], ax
    cmp bx, 0xFFFF
    je .bdn_exhausted

.bdn_have_line:
    ; BX = address of TOK_DATA_RAW length byte.  Read payload metadata.
    mov dl, [bx]                    ; DL = payload length (1..255)
    xor dh, dh
    mov si, [bas_data_sub_off]      ; SI = offset into payload (0..len)
    ; Skip leading WS
.bdn_skip_ws:
    cmp si, dx
    jae .bdn_payload_done
    mov al, [bx+si+1]               ; [bx+si+disp8] is valid
    cmp al, ' '
    je .bdn_ws_inc
    cmp al, 9
    je .bdn_ws_inc
    jmp .bdn_at_item
.bdn_ws_inc:
    inc si
    jmp .bdn_skip_ws

.bdn_at_item:
    ; SI = item-start offset within payload
    cmp al, '"'
    jne .bdn_unquoted

    ; --- Quoted item ---------------------------------------------------
    inc si                          ; skip opening '"'
    mov di, si                      ; DI = first byte of item (offset)
.bdn_q_scan:
    cmp si, dx
    jae .bdn_q_unterm
    mov al, [bx+si+1]
    cmp al, '"'
    je .bdn_q_end
    inc si
    jmp .bdn_q_scan
.bdn_q_end:
    ; Item = [bx+1+DI .. bx+1+SI-1], length = SI - DI
    mov cx, si
    sub cx, di
    inc si                          ; consume closing '"'
    ; Convert DI from offset → abs address (bx+1+di)
    add di, bx
    inc di
    mov al, 'S'
    jmp .bdn_commit

.bdn_q_unterm:
    ; Unterminated: treat tail as the item.
    mov cx, si
    sub cx, di
    add di, bx
    inc di
    mov al, 'S'
    jmp .bdn_commit

.bdn_unquoted:
    ; --- Unquoted item -------------------------------------------------
    mov di, si                      ; DI = item-start offset
.bdn_u_scan:
    cmp si, dx
    jae .bdn_u_end
    mov al, [bx+si+1]
    cmp al, ','
    je .bdn_u_end
    inc si
    jmp .bdn_u_scan
.bdn_u_end:
    ; SI = first non-item byte (',' or payload-end).  Save it; trim trailing
    ; WS by decrementing SI (we restore from stack afterwards).
    push si
.bdn_u_trim:
    cmp si, di
    je .bdn_u_trim_done
    mov al, [bx+si]                 ; byte at offset (si-1) within payload
    cmp al, ' '
    je .bdn_u_trim_dec
    cmp al, 9
    je .bdn_u_trim_dec
    jmp .bdn_u_trim_done
.bdn_u_trim_dec:
    dec si
    jmp .bdn_u_trim
.bdn_u_trim_done:
    mov cx, si
    sub cx, di                      ; CX = item length
    pop si                          ; SI = post-item position (',' or end)
    ; Convert DI from offset → abs address
    add di, bx
    inc di
    mov al, 'N'
    ; Fall through to commit

.bdn_commit:
    ; Save type + ptr + len; skip post-item WS; consume optional ',';
    ; record sub_off.
    push ax                         ; preserve type
    push cx                         ; preserve length
    push di                         ; preserve abs item ptr
.bdn_c_skip:
    cmp si, dx
    jae .bdn_c_check
    mov al, [bx+si+1]
    cmp al, ' '
    je .bdn_c_skip_inc
    cmp al, 9
    je .bdn_c_skip_inc
    jmp .bdn_c_check
.bdn_c_skip_inc:
    inc si
    jmp .bdn_c_skip
.bdn_c_check:
    cmp si, dx
    jae .bdn_c_save
    mov al, [bx+si+1]
    cmp al, ','
    jne .bdn_c_save
    inc si                          ; consume comma
.bdn_c_save:
    mov [bas_data_sub_off], si
    pop di
    pop cx
    pop ax
    pop si
    pop dx
    pop bx
    clc
    ret

.bdn_payload_done:
    ; End of payload reached without an item.  Advance to next DATA line.
    push ax
    mov al, dl                      ; payload length
    xor ah, ah
    add ax, bx                      ; AX = BX + payload_len
    inc ax                          ; +1 past length byte itself
    mov bx, ax
    pop ax
    call bas_data_find_next         ; In: BX, Out: BX
    mov [bas_data_line_ptr], bx
    xor ax, ax
    mov [bas_data_sub_off], ax
    cmp bx, 0xFFFF
    je .bdn_exhausted
    jmp .bdn_top

.bdn_exhausted:
    mov word [bas_data_line_ptr], 0xFFFF
    pop si
    pop dx
    pop bx
    mov al, BERR_OUTOFDATA
    stc
    ret


; =============================================================================
; bas_data_find_first — walk program for first TOK_DATA + TOK_DATA_RAW.
;
; In:  AX = min line# (0 = any line)
; Out: BX = absolute addr of TOK_DATA_RAW length byte, or 0xFFFF if none.
; =============================================================================
bas_data_find_first:
    push si
    push cx
    push dx
    mov dx, ax                      ; DX = min line
    mov si, BAS_PROG_BASE
    call bas_data_walk_from
    mov bx, ax                      ; AX returned in result
    pop dx
    pop cx
    pop si
    ret


; =============================================================================
; bas_data_find_next — find next TOK_DATA + TOK_DATA_RAW starting from an
; absolute address (used to skip the current/just-consumed DATA payload).
;
; In:  BX = address to start scanning from (typically just past prev payload)
; Out: BX = absolute addr of next TOK_DATA_RAW length byte, or 0xFFFF.
; =============================================================================
bas_data_find_next:
    push si
    push cx
    push dx
    ; Walk line headers until we find the line that contains BX, then start
    ; scanning from the *following* line (since any DATA before BX in the
    ; current line was already consumed).
    xor dx, dx                      ; no min line
    mov si, BAS_PROG_BASE
.bdfn_locate:
    mov ax, [si + BLINE_OFF_LINENO]
    test ax, ax
    jz .bdfn_none
    mov al, [si + BLINE_OFF_LEN]
    xor ah, ah
    add ax, BLINE_HDR_SIZE          ; AX = full record size
    mov cx, si
    add cx, ax                      ; CX = next record address
    cmp si, bx
    jae .bdfn_scan_si               ; SI starts at/after BX → start here
    cmp cx, bx
    ja  .bdfn_scan_next             ; BX is inside this line → skip to next
    mov si, cx
    cmp si, BAS_PROG_END
    jae .bdfn_none
    jmp .bdfn_locate
.bdfn_scan_next:
    mov si, cx
    cmp si, BAS_PROG_END
    jae .bdfn_none
.bdfn_scan_si:
    call bas_data_walk_from
    mov bx, ax
    pop dx
    pop cx
    pop si
    ret
.bdfn_none:
    mov bx, 0xFFFF
    pop dx
    pop cx
    pop si
    ret


; =============================================================================
; bas_data_walk_from — internal helper.
;
; In:  SI = first line-record address to scan
;      DX = min line# (0 = none)
; Out: AX = absolute addr of TOK_DATA_RAW length byte, or 0xFFFF.
; Clobbers: AX, CX (caller saves what it needs).  Preserves SI, BX, DX, DI, ES.
; =============================================================================
bas_data_walk_from:
    push si
    push bx
    push di
.bdwf_line:
    mov ax, [si + BLINE_OFF_LINENO]
    test ax, ax
    jz .bdwf_none
    cmp ax, dx
    jb .bdwf_skip
    ; Walk tokens within this line
    mov bx, si                      ; BX = saved line-record start
    mov di, si
    add di, BLINE_HDR_SIZE
.bdwf_tok:
    mov al, [di]
    cmp al, TOK_EOL
    je .bdwf_line_end
    cmp al, TOK_DATA
    je .bdwf_hit
    inc di
    cmp al, TOK_INT_LIT
    je .bdwf_t2
    cmp al, TOK_LINEREF
    je .bdwf_t2
    cmp al, TOK_STR_LIT
    je .bdwf_tstr
    cmp al, TOK_DATA_RAW
    je .bdwf_tstr
    cmp al, TOK_VAR_NAME
    je .bdwf_t2
    jmp .bdwf_tok
.bdwf_t2:
    add di, 2
    jmp .bdwf_tok
.bdwf_tstr:
    mov al, [di]
    inc di
    movzx ax, al
    add di, ax
    jmp .bdwf_tok
.bdwf_hit:
    ; Past TOK_DATA expect TOK_DATA_RAW immediately
    inc di
    mov al, [di]
    cmp al, TOK_DATA_RAW
    jne .bdwf_tok                   ; defensive — keep scanning
    inc di
    mov ax, di                      ; AX = address of length byte
    pop di
    pop bx
    pop si
    ret
.bdwf_line_end:
    mov si, bx                      ; restore line-record start
.bdwf_skip:
    mov al, [si + BLINE_OFF_LEN]
    xor ah, ah
    add si, BLINE_HDR_SIZE
    add si, ax
    cmp si, BAS_PROG_END
    jae .bdwf_none
    jmp .bdwf_line
.bdwf_none:
    mov ax, 0xFFFF
    pop di
    pop bx
    pop si
    ret

