; ─── shell_parse_args ─────────────────────────────────────────────────────────
; Parse the raw argument string (pointed to by SHELL_ARGS_PTR) into the
; structured argc/argv table at ARGV_TABLE (0x7F00).
;
; Called by run command before launching a program.
;
; Input:  [SHELL_ARGS_PTR] = pointer to NUL-terminated argument string
; Output: [ARGV_ARGC] = number of parsed arguments (0–15)
;         [ARGV_PTRS] = array of pointers to NUL-terminated strings in ARGV_STORAGE
;
; Parsing rules:
;   - Arguments separated by spaces/tabs
;   - Double-quoted strings treated as single argument (quotes stripped)
;   - Maximum 15 arguments; excess silently ignored
;   - Maximum ~217 bytes of total string storage
; ──────────────────────────────────────────────────────────────────────────────
shell_parse_args:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    ; Clear argc
    xor al, al
    mov [ARGV_ARGC], al
    xor cx, cx                      ; CX = argc counter (zero before early exits)

    ; SI = source (raw args), DI = destination (storage area)
    mov si, [SHELL_ARGS_PTR]
    test si, si
    jz .spa_done                    ; No args pointer → argc=0
    cmp byte [si], 0
    je .spa_done                    ; Empty string → argc=0

    mov di, ARGV_STORAGE            ; DI = next free byte in storage

.spa_skip_spaces:
    lodsb                           ; AL = next char from source
    cmp al, ' '
    je .spa_skip_spaces
    cmp al, 9                       ; TAB
    je .spa_skip_spaces
    cmp al, 0                       ; End of string?
    je .spa_done
    ; AL has first char of next argument
    dec si                          ; Back up — we consumed one too many

    ; Check if we've hit max args
    cmp cl, ARGV_MAX_ARGS
    jae .spa_done

    ; Record pointer to this argument
    mov bx, cx
    shl bx, 1
    mov [ARGV_PTRS + bx], di        ; argv[argc] = current DI

    ; Check for quoted argument
    cmp byte [si], '"'
    je .spa_quoted

.spa_unquoted:
    ; Copy until space, tab, or NUL
    lodsb
    cmp al, ' '
    je .spa_end_arg
    cmp al, 9
    je .spa_end_arg
    cmp al, 0
    je .spa_end_arg_final
    ; Check storage overflow
    cmp di, ARGV_STORAGE_END
    jae .spa_done
    stosb
    jmp .spa_unquoted

.spa_quoted:
    inc si                          ; Skip opening quote
.spa_quoted_loop:
    lodsb
    cmp al, '"'                     ; Closing quote?
    je .spa_end_arg_after_quote
    cmp al, 0                       ; Unterminated quote → end arg
    je .spa_end_arg_final
    ; Check storage overflow
    cmp di, ARGV_STORAGE_END
    jae .spa_done
    stosb
    jmp .spa_quoted_loop

.spa_end_arg_after_quote:
    ; After closing quote, skip any trailing spaces before next arg
.spa_end_arg:
    ; NUL-terminate this argument
    xor al, al
    stosb
    inc cl                          ; argc++
    jmp .spa_skip_spaces

.spa_end_arg_final:
    ; NUL-terminate the last argument (source string ended)
    xor al, al
    stosb
    inc cl                          ; argc++

.spa_done:
    mov [ARGV_ARGC], cl

    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
