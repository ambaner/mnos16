; =============================================================================
; basic_load.inc — LOAD, SAVE, FILES, NEW, CLEAR.
;
; Public:
;   bas_load_file   In: DS:SI = source filename (NUL or space-terminated)
;                       CX = filename length
;                   Reads file via FS_READ_FILE, NEWs the program, then
;                   tokenizes each line and inserts.  Lines without leading
;                   line numbers cause BERR_SYNTAX.
;                   CF=0 ok, CF=1 → AL = BERR_*
;
;   bas_save_file   In: DS:SI = source filename, CX = length
;                   Builds text image in a scratch heap buffer (LIST format),
;                   deletes the existing file (ignoring NOT_FOUND), and
;                   writes the new payload.  On post-delete write failure
;                   the old file is lost — documented trade-off.
;                   CF=0 ok, CF=1 → AL = BERR_*
;
;   bas_cmd_files   Lists MNFS directory.  Direct-mode FILES command.
;
;   bas_cmd_new     Wipes program + variables + stacks + channels.
;
;   bas_cmd_clear   Wipes variables + stacks + temp pool; keeps program.
; =============================================================================

; ----------------------------------------------------------------------------
; Filename helpers.  MNFS uses 8.3 names in a 12-byte field: 8 chars padded
; with spaces + 3 chars padded + NUL.  If the user passes "FOO" we want
; "FOO.BAS".
;
; bas_fname_normalize:
;   In:  DS:SI = source name, CX = length (≤ 12), default ext if no dot
;   Out: bas_fname_buf populated as 12 bytes of 8.3 (NUL-pad after).
;        AX = 1 → ok, AX = 0 → invalid (too long)
;
; This is a permissive normalizer: copies up to 8 chars before dot, then
; up to 3 after; uppercases; pads with spaces.  Default extension "BAS"
; applied when no dot present.
; ----------------------------------------------------------------------------
bas_fname_normalize:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov ax, ds
    mov es, ax
    mov di, bas_fname_buf
    ; Zero-fill 16 bytes
    push cx
    mov cx, 16
    xor al, al
    rep stosb
    pop cx
    ; Pre-fill 11 bytes with spaces; byte 11 = NUL (already 0)
    mov di, bas_fname_buf
    mov word [di+0], 0x2020
    mov word [di+2], 0x2020
    mov word [di+4], 0x2020
    mov word [di+6], 0x2020
    mov word [di+8], 0x2020
    mov byte [di+10], 0x20
    mov byte [di+11], 0
    ; Walk source.
    ; BL = "seen dot" flag (0 = base, 1 = ext)
    ; BH = base char count (0..8)
    ; DH = ext char count (0..3)
    xor bx, bx
    xor dh, dh
.bfn_loop:
    test cx, cx
    jz .bfn_done
    mov dl, [si]
    inc si
    dec cx
    cmp dl, ' '
    je .bfn_done
    cmp dl, 0
    je .bfn_done
    cmp dl, '"'
    je .bfn_done
    cmp dl, 13
    je .bfn_done
    cmp dl, 10
    je .bfn_done
    cmp dl, '.'
    je .bfn_dot
    ; Uppercase
    cmp dl, 'a'
    jb .bfn_no_upper
    cmp dl, 'z'
    ja .bfn_no_upper
    sub dl, 0x20
.bfn_no_upper:
    test bl, bl
    jnz .bfn_ext_char
    ; --- Base char ---
    cmp bh, 8
    jae .bfn_loop                   ; truncate silently
    push bx
    mov al, bh
    xor ah, ah
    mov bx, ax
    mov [bas_fname_buf + bx], dl
    pop bx
    inc bh
    jmp .bfn_loop
.bfn_dot:
    test bl, bl
    jnz .bfn_loop                   ; ignore subsequent dots
    mov bl, 1
    jmp .bfn_loop
.bfn_ext_char:
    cmp dh, 3
    jae .bfn_loop
    push bx
    mov al, dh
    xor ah, ah
    add ax, 8
    mov bx, ax
    mov [bas_fname_buf + bx], dl
    pop bx
    inc dh
    jmp .bfn_loop
.bfn_done:
    test bh, bh
    jz .bfn_bad
    ; If no extension supplied, default to "BAS"
    test bl, bl
    jnz .bfn_ok
    mov byte [bas_fname_buf+8], 'B'
    mov byte [bas_fname_buf+9], 'A'
    mov byte [bas_fname_buf+10], 'S'
.bfn_ok:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    mov ax, 1
    ret
.bfn_bad:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    xor ax, ax
    ret


; =============================================================================
; bas_cmd_new — wipe program + everything
; =============================================================================
bas_cmd_new:
    call bas_cmd_clear
    call bas_prog_clear
    ret


; =============================================================================
; bas_cmd_clear — wipe variables, stacks, channels, temp pool (keep program)
; =============================================================================
bas_cmd_clear:
    push ax
    xor ax, ax
    mov [bas_var_count], ax
    mov [bas_gs_top], ax
    mov [bas_for_top], ax
    mov [bas_wh_top], ax
    mov [bas_ex_top], ax
    mov [bas_op_top], ax
    ; Free temp pool and channels (defined in basic_err.inc)
    call bas_temp_pool_free_all
    call bas_channels_close_all_discard
    ; Reset DEF SEG to our own segment
    mov ax, ds
    mov [bas_def_seg], ax
    pop ax
    ret


; =============================================================================
; bas_cmd_files — direct LIST of MNFS directory.
; =============================================================================
bas_cmd_files:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es

    ; FS_LIST_FILES expects ES:BX = buffer for 15 × 16-byte directory entries
    ; (240 bytes).  Reuse bas_token_buf scratch (256 bytes).
    mov ax, ds
    mov es, ax
    mov bx, bas_token_buf
    call mn_list_files
    jc .bcf_io
    ; CL = file count
    xor ch, ch
    test cl, cl
    jz .bcf_none
    mov si, bx                      ; SI = first entry
    add si, MNFS_HDR_SIZE           ; skip 32-byte MNFS header
    xor dx, dx                      ; DX = count of BAS files shown
.bcf_loop:
    push cx
    ; Filter: only show *.BAS entries (name=8, ext at +8..+10)
    cmp byte [si+8], 'B'
    jne .bcf_skip_entry
    cmp byte [si+9], 'A'
    jne .bcf_skip_entry
    cmp byte [si+10], 'S'
    jne .bcf_skip_entry
    ; bas_print_field advances SI by CX; print name(8) + '.' + ext(3) → SI+=11
    mov cx, 8
    call bas_print_field
    mov al, '.'
    call bas_lex_putc
    mov cx, 3
    call bas_print_field
    call bas_crlf
    add si, MNFS_ENTRY_SIZE - 11    ; skip rest of entry (32 - 11 = 21)
    inc dx
    pop cx
    loop .bcf_loop
    jmp .bcf_check
.bcf_skip_entry:
    add si, MNFS_ENTRY_SIZE         ; full 32-byte entry
    pop cx
    loop .bcf_loop
.bcf_check:
    test dx, dx
    jnz .bcf_done
.bcf_none:
    mov si, bas_msg_no_files
    call bas_puts_nul
    call bas_crlf
.bcf_done:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
.bcf_io:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    mov al, BERR_DISKIO
    stc
    ret

; Helper: print CX raw chars from DS:SI, then advance SI by CX.
bas_print_field:
    push ax
    push cx
.bpfd_loop:
    mov al, [si]
    inc si
    call bas_lex_putc
    loop .bpfd_loop
    pop cx
    pop ax
    ret

bas_msg_no_files: db 'No BAS files',0


; =============================================================================
; bas_load_file
; In: DS:SI = filename text, CX = length
; =============================================================================
bas_load_file:
    push bx
    push cx
    push dx
    push si
    push di
    push es

    ; --- DEBUG: marker so we know we entered bas_load_file --------------
    push si
    push bx
    mov si, blf_dbg_enter
    xor bx, bx
    call mn_dbg_print
    pop bx
    pop si

    ; Normalize filename
    call bas_fname_normalize
    test ax, ax
    jz .blf_badfn

    ; FS_FIND_FILE to learn the file's sector count.  Cap to fit in our
    ; conventional load buffer (BAS_LOAD_BUF_LEN = 4096 → 8 sectors).
    mov si, bas_fname_buf
    call mn_find_file
    jc .blf_notfound
    ; CX = sector count.
    cmp cx, (BAS_LOAD_BUF_LEN / 512)
    ja .blf_toobig

    ; Read file directly into bas_load_buf (ES:BX = DS:bas_load_buf).
    ; Conventional memory only — no MEM_ALLOC, no HMA, no IVT corruption.
    push ds
    pop es                          ; ES = our DS
    mov bx, bas_load_buf
    mov si, bas_fname_buf
    call mn_read_file
    jc .blf_readerr
    ; AX = bytes read; stash for after the DEBUG print + bas_cmd_new
    mov cx, ax                      ; CX = actual byte count

    ; --- DEBUG: file read OK --------------------------------------------
    push cx
    push dx
    mov si, blf_dbg_read
    xor bx, bx
    call mn_dbg_print
    pop dx
    pop cx

    ; --- NEW the program (wipe) ----------------------------------------
    push cx                         ; preserve byte count across NEW
    call bas_cmd_new
    pop cx

    ; --- DEBUG: NEW done; about to enter line loop ----------------------
    push cx
    push dx
    mov si, blf_dbg_newdone
    xor bx, bx
    call mn_dbg_print
    pop dx
    pop cx

    ; --- Parse text into lines -----------------------------------------
    ; Source lives in our own DS at bas_load_buf.  ES = DS, BX = source ptr,
    ; DX = remaining bytes.  No segment-switching needed (much simpler than
    ; the prior HMA-based path).
    push ds
    pop es                          ; ES = DS
    mov bx, bas_load_buf
    mov dx, cx                      ; DX = remaining
.blf_line_loop:
    test dx, dx
    jz .blf_load_done

    ; --- DEBUG: line loop iteration -------------------------------------
    push bx
    push cx
    push dx
    push si
    mov si, blf_dbg_iter
    xor bx, bx
    call mn_dbg_print
    pop si
    pop dx
    pop cx
    pop bx

    ; Find end of current line in load buffer (search for CR/LF/NUL).
    ; Source span: DS:BX .. DS:(BX + line_len)
    mov si, bx                      ; SI = scan ptr (within DS)
    mov cx, dx                      ; CX = remaining to scan
.blf_find_eol:
    mov al, [si]
    cmp al, 13
    je .blf_got_eol
    cmp al, 10
    je .blf_got_eol
    cmp al, 0
    je .blf_got_eol
    inc si
    dec cx
    jnz .blf_find_eol
.blf_got_eol:
    ; Line span = [bx .. si); line_len = si - bx
    ; Copy line into bas_line_buf (NUL-terminated).  Source & dest are both
    ; in DS, so no segment dance required.
    push bx
    push dx
    push si

    mov cx, si
    sub cx, bx                      ; CX = line length
    cmp cx, BAS_LINE_BUF_LEN - 1
    jbe .blf_len_ok
    mov cx, BAS_LINE_BUF_LEN - 1
.blf_len_ok:
    mov si, bx
    mov di, bas_line_buf
    cld
    rep movsb
    mov byte [di], 0                ; NUL terminate

    pop si
    pop dx
    pop bx

    ; --- DEBUG: copied line, about to call silent parser ----------------
    push bx
    push cx
    push dx
    push si
    mov si, blf_dbg_copied
    xor bx, bx
    call mn_dbg_print
    pop si
    pop dx
    pop cx
    pop bx

    ; Hand to the line-parser/inserter.  Preserve BX/DX across the call.
    push bx
    push dx
    mov si, bas_line_buf
    call bas_handle_input_line_silent
    pop dx
    pop bx
    jc .blf_parse_err

    ; --- DEBUG: silent returned OK --------------------------------------
    push bx
    push cx
    push dx
    push si
    mov si, blf_dbg_silentok
    xor bx, bx
    call mn_dbg_print
    pop si
    pop dx
    pop cx
    pop bx

    ; Advance BX past the line text in the source buffer.  We re-walk
    ; (rather than trust SI from the find_eol pass) so the cap-to-256
    ; truncation doesn't desync the source pointer.
    push cx
    mov cx, dx                      ; CX = remaining
.blf_real_eol:
    test cx, cx
    jz .blf_after_line
    mov al, [bx]
    cmp al, 13
    je .blf_after_line
    cmp al, 10
    je .blf_after_line
    cmp al, 0
    je .blf_after_line
    inc bx
    dec dx
    dec cx
    jmp .blf_real_eol
.blf_after_line:
    pop cx

    ; Skip terminator bytes (CR, LF, CRLF, NUL)
.blf_skip_term:
    test dx, dx
    jz .blf_load_done
    mov al, [bx]
    cmp al, 13
    je .blf_term_eat
    cmp al, 10
    je .blf_term_eat
    cmp al, 0
    je .blf_term_eat
    jmp .blf_line_loop
.blf_term_eat:
    inc bx
    dec dx
    jmp .blf_skip_term

.blf_load_done:
    ; --- DEBUG: load done -----------------------------------------------
    push bx
    push cx
    push dx
    mov si, blf_dbg_done
    xor bx, bx
    call mn_dbg_print
    pop dx
    pop cx
    pop bx
    ; No heap to free — load buffer is static BSS.
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    clc
    ret

.blf_parse_err:
    ; AL = BERR_*; entry-pushed registers are still on stack (6 items)
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    stc
    ret

.blf_notfound:
    mov al, BERR_FILENOTFOUND
    jmp .blf_bail
.blf_badfn:
    mov al, BERR_SYNTAX
    jmp .blf_bail
.blf_toobig:
    mov al, BERR_PROGTOOBIG
    jmp .blf_bail
.blf_readerr:
    mov al, BERR_DISKIO
    jmp .blf_bail

.blf_bail:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    stc
    ret

blf_dbg_enter:    db 'BLF: enter',0
blf_dbg_read:     db 'BLF: read OK',0
blf_dbg_newdone:  db 'BLF: new done',0
blf_dbg_iter:     db 'BLF: iter',0
blf_dbg_copied:   db 'BLF: copied',0
blf_dbg_silentok: db 'BLF: silent OK',0
blf_dbg_done:     db 'BLF: done',0


; ---------------------------------------------------------------------------
; bas_handle_input_line_silent — like bas_handle_input_line, but returns
; CF/AL on error instead of longjmping.  Used by LOAD where we want to
; surface errors cleanly without losing the file buffer.
; ---------------------------------------------------------------------------
bas_handle_input_line_silent:
    push bx
    push cx
    push si
    push di
    push es
.bhils_skipws:
    mov al, [si]
    cmp al, ' '
    je .bhils_eat
    cmp al, 9
    je .bhils_eat
    jmp .bhils_after
.bhils_eat:
    inc si
    jmp .bhils_skipws
.bhils_after:
    test al, al
    jz .bhils_ok
    cmp al, 13
    je .bhils_ok
    cmp al, 10
    je .bhils_ok
    cmp al, '0'
    jb .bhils_syntax
    cmp al, '9'
    ja .bhils_syntax
    xor bx, bx
.bhils_ln:
    mov al, [si]
    cmp al, '0'
    jb .bhils_ln_done
    cmp al, '9'
    ja .bhils_ln_done
    inc si
    sub al, '0'
    push ax
    mov ax, bx
    mov cx, 10
    mul cx
    test dx, dx
    jnz .bhils_badln_pop
    cmp ax, BAS_MAX_LINENO
    ja .bhils_badln_pop
    mov bx, ax
    pop ax
    xor ah, ah
    add bx, ax
    cmp bx, BAS_MAX_LINENO
    ja .bhils_badln
    jmp .bhils_ln
.bhils_badln_pop:
    pop ax
.bhils_badln:
    mov al, BERR_BADLINENUM
    jmp .bhils_fail
.bhils_ln_done:
    test bx, bx
    jz .bhils_badln
    ; Skip whitespace
.bhils_skipws2:
    mov al, [si]
    cmp al, ' '
    je .bhils_eat2
    cmp al, 9
    je .bhils_eat2
    jmp .bhils_after2
.bhils_eat2:
    inc si
    jmp .bhils_skipws2
.bhils_after2:
    test al, al
    jz .bhils_delete
    cmp al, 13
    je .bhils_delete
    cmp al, 10
    je .bhils_delete
    ; Tokenize
    mov ax, ds
    mov es, ax
    mov di, bas_token_buf
    push bx
    call bas_tokenize_line
    pop bx
    jc .bhils_fail
    mov ax, bx
    mov si, bas_token_buf
    call bas_prog_insert
    jc .bhils_fail
    jmp .bhils_ok
.bhils_delete:
    mov ax, bx
    mov si, bas_token_buf
    mov byte [si], TOK_EOL
    mov cx, 1
    call bas_prog_insert
    jc .bhils_fail
.bhils_ok:
    pop es
    pop di
    pop si
    pop cx
    pop bx
    clc
    ret
.bhils_syntax:
    mov al, BERR_SYNTAX
.bhils_fail:
    pop es
    pop di
    pop si
    pop cx
    pop bx
    stc
    ret


; =============================================================================
; bas_save_file
; In: DS:SI = filename, CX = length
; Builds a text buffer in bas_token_buf (256B) per line and accumulates
; into a heap buffer.  Simpler path: write directly chunk-by-chunk after
; sizing in one pass.
; =============================================================================
bas_save_file:
    push bx
    push cx
    push dx
    push si
    push di
    push es

    call bas_fname_normalize
    test ax, ax
    jz .bsf_badfn

    cmp word [bas_prog_len], 0
    je .bsf_noprog

    ; --- Emit LIST output into the conventional load/save scratch buffer ----
    ; bas_load_buf at 0xC000 (BAS_LOAD_BUF_LEN = 4096) is conventional memory,
    ; safe for BIOS INT 13h writes (no A20 wrap).  Caps SAVE at 4KB.
    mov word [bas_pi_old_size], 0                  ; segment = DS (0)
    mov word [bas_pi_new_size], bas_load_buf       ; current write pos
    mov word [bas_pi_end_addr], bas_load_buf       ; start pos for length calc
    mov word [bas_pi_paylen], BAS_LOAD_BUF_LEN     ; remaining capacity

    ; Walk program lines and detokenize each into the buffer
    call bas_prog_first
    jc .bsf_pass_done
.bsf_pass_loop:
    push bx
    call bas_save_emit_line         ; emits line to buffer; trashes regs
    pop bx
    jc .bsf_buf_full
    call bas_prog_next
    jnc .bsf_pass_loop
.bsf_pass_done:

    ; Compute byte length emitted.
    mov ax, [bas_pi_new_size]
    sub ax, [bas_pi_end_addr]
    movzx ecx, ax                   ; ECX = byte count

    ; Atomic create-or-replace via mnoslib helper.
    ; ES:BX = our DS:bas_load_buf, DS:SI = filename, DL = attr.
    push ds
    pop  es
    mov  bx, bas_load_buf
    xor  dl, dl
    mov  si, bas_fname_buf
    call mn_save_file
    jc .bsf_write_err

    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    clc
    ret

.bsf_write_err:
    mov al, BERR_DISKFULL
    jmp .bsf_fail
.bsf_buf_full:
    mov al, BERR_PROGTOOBIG
    jmp .bsf_fail
.bsf_noprog:
    mov al, BERR_NOPROGRAM
    jmp .bsf_fail
.bsf_badfn:
    mov al, BERR_SYNTAX
.bsf_fail:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    stc
    ret


; ---------------------------------------------------------------------------
; bas_save_emit_line — for a line at DS:BX, emit "<lineno> <text>\r\n" to
; the heap buffer described by bas_pi_old_size/new_size/paylen.
; Returns CF=1 if heap buffer too small.
; ---------------------------------------------------------------------------
bas_save_emit_line:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es

    ; Print line number first
    mov dx, [bx + BLINE_OFF_LINENO]
    mov ax, dx
    call bas_save_emit_unsigned
    jc .bsel_full
    mov al, ' '
    call bas_save_emit_byte
    jc .bsel_full

    ; Detokenize payload to heap (NOT to console).
    mov si, bx
    add si, BLINE_OFF_PAYLOAD
    call bas_save_emit_detok
    jc .bsel_full

    ; CRLF
    mov al, 13
    call bas_save_emit_byte
    jc .bsel_full
    mov al, 10
    call bas_save_emit_byte
    jc .bsel_full

    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret
.bsel_full:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret

; Emit AL to heap; CF=1 if full.
bas_save_emit_byte:
    push bx
    push es
    cmp word [bas_pi_paylen], 0
    je .bseb_full
    mov es, [bas_pi_old_size]
    mov bx, [bas_pi_new_size]
    mov [es:bx], al
    inc word [bas_pi_new_size]
    dec word [bas_pi_paylen]
    pop es
    pop bx
    clc
    ret
.bseb_full:
    pop es
    pop bx
    stc
    ret

; Emit AX as unsigned decimal (1..65535) to heap.
bas_save_emit_unsigned:
    push ax
    push bx
    push cx
    push dx
    xor cx, cx
    mov bx, 10
.bseu_div:
    xor dx, dx
    div bx
    push dx
    inc cx
    test ax, ax
    jnz .bseu_div
.bseu_emit:
    pop ax
    add al, '0'
    call bas_save_emit_byte
    jc .bseu_full
    loop .bseu_emit
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret
.bseu_full:
    ; pop remaining digit stack
.bseu_drain:
    test cx, cx
    jz .bseu_drained
    pop ax
    dec cx
    jmp .bseu_drain
.bseu_drained:
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret

; Detokenize DS:SI payload to heap.  Mirrors bas_detokenize_line but
; emits via bas_save_emit_byte.  Returns CF=1 if heap full.
bas_save_emit_detok:
    push ax
    push bx
    push cx
    push dx
    push si
    xor cx, cx                      ; need-space flag
.dse_loop:
    mov al, [si]
    inc si
    cmp al, TOK_EOL
    je .dse_done

    cmp al, TOK_INT_LIT
    je .dse_int
    cmp al, TOK_LINEREF
    je .dse_lr
    cmp al, TOK_STR_LIT
    je .dse_str
    cmp al, TOK_VAR_NAME
    je .dse_var
    cmp al, 0x80
    jae .dse_kw

    ; punctuation
    cmp al, ','
    je .dse_punct_no_sp
    cmp al, ';'
    je .dse_punct_no_sp
    cmp al, ':'
    je .dse_punct_no_sp
    cmp al, '('
    je .dse_punct_no_sp
    cmp al, ')'
    je .dse_punct_no_sp
    cmp al, '#'
    je .dse_punct_no_sp
    cmp al, ' '
    je .dse_punct_sp
    call bas_save_emit_byte
    jc .dse_fail
    or  cl, 1
    jmp .dse_loop
.dse_punct_sp:
    mov al, ' '
    call bas_save_emit_byte
    jc .dse_fail
    and cl, 0xFE
    jmp .dse_loop
.dse_punct_no_sp:
    call bas_save_emit_byte
    jc .dse_fail
    and cl, 0xFE
    jmp .dse_loop

.dse_int:
    mov ax, [si]
    add si, 2
    test cl, 1
    jz .dse_int_emit
    push ax
    mov al, ' '
    call bas_save_emit_byte
    pop ax
    jc .dse_fail
.dse_int_emit:
    test ax, ax
    jns .dse_int_pos
    push ax
    mov al, '-'
    call bas_save_emit_byte
    pop ax
    jc .dse_fail
    neg ax
.dse_int_pos:
    call bas_save_emit_unsigned
    jc .dse_fail
    or  cl, 1
    jmp .dse_loop

.dse_lr:
    mov ax, [si]
    add si, 2
    test cl, 1
    jz .dse_lr_emit
    push ax
    mov al, ' '
    call bas_save_emit_byte
    pop ax
    jc .dse_fail
.dse_lr_emit:
    call bas_save_emit_unsigned
    jc .dse_fail
    or  cl, 1
    jmp .dse_loop

.dse_str:
    mov al, '"'
    call bas_save_emit_byte
    jc .dse_fail
    mov al, [si]
    inc si
    mov dl, al                      ; length
.dse_str_lp:
    test dl, dl
    jz .dse_str_end
    mov al, [si]
    inc si
    call bas_save_emit_byte
    jc .dse_fail
    dec dl
    jmp .dse_str_lp
.dse_str_end:
    mov al, '"'
    call bas_save_emit_byte
    jc .dse_fail
    or  cl, 1
    jmp .dse_loop

.dse_var:
    test cl, 1
    jz .dse_var_emit
    push ax
    mov al, ' '
    call bas_save_emit_byte
    pop ax
    jc .dse_fail
.dse_var_emit:
    mov al, [si]
    inc si
    mov dh, al
    and al, 0x1F
    add al, 'A'
    call bas_save_emit_byte
    jc .dse_fail
    mov al, [si]
    inc si
    cmp al, 10
    jae .dse_var_no_digit
    add al, '0'
    call bas_save_emit_byte
    jc .dse_fail
.dse_var_no_digit:
    test dh, 0x80
    jz .dse_var_done
    mov al, '$'
    call bas_save_emit_byte
    jc .dse_fail
.dse_var_done:
    or  cl, 1
    jmp .dse_loop

.dse_kw:
    push si
    call bas_tok_kw_name
    jc .dse_kw_miss
    mov bx, si
    pop si
    test cl, 1
    jz .dse_kw_print
    push bx
    mov al, ' '
    call bas_save_emit_byte
    pop bx
    jc .dse_fail
.dse_kw_print:
.dse_kw_lp:
    mov al, [bx]
    test al, al
    jz .dse_kw_done
    call bas_save_emit_byte
    jc .dse_fail
    inc bx
    jmp .dse_kw_lp
.dse_kw_done:
    or  cl, 1
    jmp .dse_loop
.dse_kw_miss:
    pop si
    mov al, '?'
    call bas_save_emit_byte
    jc .dse_fail
    or  cl, 1
    jmp .dse_loop

.dse_done:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret
.dse_fail:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret
