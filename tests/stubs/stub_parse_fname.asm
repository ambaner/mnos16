; stub_parse_fname.asm — Test harness for run_parse_filename
;
; The test sets SI to point to a command string (e.g., "hello.mnx foo bar"),
; then calls entry.  After hlt, the test reads:
;   - run_fname_buf (11 bytes): the parsed 8.3 filename
;   - run_ext_provided (1 byte): whether a dot+extension was given
;   - run_args_ptr (2 bytes): pointer to the argument portion

[bits 16]
[org 0x1000]

entry:
    call run_parse_filename
    hlt

; ─── run_parse_filename (from shell_cmd_run.inc) ─────────────────────────────
; We inline it here to avoid pulling in the full shell_cmd_run.inc which has
; many dependencies (syscalls, FS calls, etc.).

run_parse_filename:
    push di
    push cx

    ; Default: no extension provided
    mov byte [run_ext_provided], 0

    ; Initialize fname buffer with spaces
    mov di, run_fname_buf
    mov cx, 11
    mov al, ' '
    rep stosb

    ; Start filling filename (up to 8 chars before dot)
    mov di, run_fname_buf
    xor cx, cx                      ; CX = char count in name part

.rpf_name_loop:
    mov al, [si]
    cmp al, 0                       ; End of string?
    je .rpf_done_no_ext
    cmp al, ' '                     ; Space = end of filename, args follow
    je .rpf_done_space
    cmp al, '.'                     ; Dot = start extension
    je .rpf_dot

    ; Uppercase conversion
    cmp al, 'a'
    jb .rpf_store_name
    cmp al, 'z'
    ja .rpf_store_name
    sub al, 32                      ; Convert to uppercase

.rpf_store_name:
    cmp cx, 8                       ; Max 8 chars in name
    jge .rpf_skip_name_char
    mov [di], al
    inc di
    inc cx
.rpf_skip_name_char:
    inc si
    jmp .rpf_name_loop

.rpf_dot:
    mov byte [run_ext_provided], 1
    inc si                          ; Skip the dot
    ; Fill extension part (bytes 8-10 of fname buffer)
    mov di, run_fname_buf + 8
    xor cx, cx

.rpf_ext_loop:
    mov al, [si]
    cmp al, 0
    je .rpf_done_no_args
    cmp al, ' '
    je .rpf_done_space

    ; Uppercase conversion
    cmp al, 'a'
    jb .rpf_store_ext
    cmp al, 'z'
    ja .rpf_store_ext
    sub al, 32

.rpf_store_ext:
    cmp cx, 3                       ; Max 3 chars in extension
    jge .rpf_skip_ext_char
    mov [di], al
    inc di
    inc cx
.rpf_skip_ext_char:
    inc si
    jmp .rpf_ext_loop

.rpf_done_space:
    ; SI points at the space; skip spaces to find args
    inc si
.rpf_skip_arg_spaces:
    cmp byte [si], ' '
    jne .rpf_set_args
    inc si
    jmp .rpf_skip_arg_spaces
.rpf_set_args:
    ; SI → first arg char (or NUL if just trailing spaces)
    mov [run_args_ptr], si
    clc
    pop cx
    pop di
    ret

.rpf_done_no_ext:
    ; No extension provided — leave extension as spaces
    mov byte [run_ext_provided], 0
.rpf_done_no_args:
    ; No args — point args_ptr at an empty string (NUL)
    mov word [run_args_ptr], run_empty_args
    clc
    pop cx
    pop di
    ret

; ─── Data ────────────────────────────────────────────────────────────────────
run_fname_buf   times 11 db 0       ; 8.3 filename buffer
run_ext_provided db 0               ; 1 if user typed a dot+extension
run_args_ptr    dw 0                ; Pointer to argument string
run_empty_args  db 0                ; Empty NUL string for no-args case
