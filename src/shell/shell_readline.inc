; =============================================================================
; SUBROUTINES
;
; Only pure-logic routines remain here.  All I/O routines (puts, putc,
; puthex8, print_hex16, print_dec16, wait_key, check_a20) have been removed
; because the kernel provides equivalent syscalls.
; =============================================================================

; ---------------------------------------------------------------------------
; readline — Read a line of input into cmd_buf (up to 31 chars).
;
; Uses SYS_READ_KEY for keyboard input and SYS_PRINT_CHAR for echo.
; Handles Enter (submit), Backspace (delete), and printable ASCII.
; Converts uppercase to lowercase for case-insensitive command matching.
; ---------------------------------------------------------------------------
readline:
    xor cx, cx                      ; CX = character count

.read_char:
    call mn_read_key                ; Wait for keypress; AH=scancode, AL=ASCII
    ; Note: AH now has scancode, not the syscall number anymore

    cmp al, 0x0D                    ; Enter key?
    je .read_done

    cmp al, 0x08                    ; Backspace?
    je .read_bs

    cmp al, 0x20                    ; Below space = control char -> skip
    jb .read_char
    cmp al, 0x7E                    ; Above '~' -> skip
    ja .read_char

    cmp cx, 31                      ; Buffer full?
    jge .read_char

    ; Convert uppercase to lowercase for case-insensitive matching
    cmp al, 'A'
    jb .no_lower
    cmp al, 'Z'
    ja .no_lower
    add al, 32
.no_lower:

    ; Store character in buffer
    mov bx, cmd_buf
    add bx, cx
    mov [bx], al
    inc cx

    ; Echo the character to screen via mn_print_char
    call mn_print_char
    jmp .read_char

.read_bs:
    test cx, cx                     ; Nothing to delete?
    jz .read_char

    dec cx                          ; Remove last character

    ; Backspace echo: move cursor back, print space, move back again
    mov al, 0x08                    ; Backspace character
    call mn_print_char
    mov al, ' '                     ; Overwrite with space
    call mn_print_char
    mov al, 0x08                    ; Move cursor back again
    call mn_print_char
    jmp .read_char

.read_done:
    ; NUL-terminate the input buffer
    mov bx, cmd_buf
    add bx, cx
    mov byte [bx], 0
    mov [cmd_len], cl

    ; Print newline after the entered command
    mov si, msg_crlf
    call mn_print_string
    ret

; ---------------------------------------------------------------------------
; strcmp — Compare two NUL-terminated strings (case-sensitive).
;   Input:  DS:SI -> string 1, DS:DI -> string 2
;   Output: ZF set if strings are equal
;
; Pure logic — no hardware access, no syscalls needed.
; ---------------------------------------------------------------------------
strcmp:
    push si
    push di
.cmp_loop:
    lodsb
    mov ah, [di]
    inc di
    cmp al, ah
    jne .cmp_ne
    test al, al
    jnz .cmp_loop
    pop di
    pop si
    ret                             ; ZF is set (equal)

.cmp_ne:
    pop di
    pop si
    or al, 1                        ; Clear ZF (not equal)
    ret

; ---------------------------------------------------------------------------
; cmdmatch — Check if cmd_buf starts with command name DI and is followed by
;            a space or NUL.
;   Input:  DS:SI -> cmd_buf, DS:DI -> command string (NUL-terminated)
;   Output: ZF set if command matches (SI left past matched word + space)
; ---------------------------------------------------------------------------
cmdmatch:
    push si
    push di
.cm_loop:
    mov al, [di]
    test al, al
    jz .cm_end_cmd              ; Reached end of command string
    cmp al, [si]
    jne .cm_ne
    inc si
    inc di
    jmp .cm_loop

.cm_end_cmd:
    ; Command string ended — cmd_buf must have space or NUL here
    mov al, [si]
    cmp al, ' '
    je .cm_ok
    test al, al
    je .cm_ok
    ; cmd_buf has more non-space chars — not a match (e.g., "delete" vs "del")
    jmp .cm_ne

.cm_ok:
    pop di
    pop si
    xor al, al                      ; Set ZF (match)
    ret

.cm_ne:
    pop di
    pop si
    or al, 1                        ; Clear ZF (no match)
    ret

; ---------------------------------------------------------------------------
; rjust_dec16 — Print DX as unsigned decimal, right-justified in CL-wide field
;
; Input:  DX = value to print, CL = minimum field width
; Output: none
; Preserves: AX, BX, CX, DX, SI, DI
; ---------------------------------------------------------------------------
rjust_dec16:
    push ax
    push bx
    push cx
    push dx

    ; Count digits of DX
    mov ax, dx
    xor ch, ch                      ; CH = digit count
.rj_count:
    inc ch
    xor dx, dx
    mov bx, 10
    div bx
    test ax, ax
    jnz .rj_count

    ; Print (CL - CH) leading spaces
    sub cl, ch
    jbe .rj_print
.rj_pad:
    push cx
    mov al, ' '
    call mn_print_char
    pop cx
    dec cl
    jnz .rj_pad

.rj_print:
    pop dx                          ; Restore original value
    call mn_print_dec16

    pop cx
    pop bx
    pop ax
    ret

; ---------------------------------------------------------------------------
; print_boot_tag — Print [Release] or [Debug] based on BIB boot_mode.
;
; Reads BIB_BOOT_MODE via SYS_GET_BIB and prints the appropriate tag
; followed by CRLF.  Saves/restores ES and BX.
; ---------------------------------------------------------------------------
print_boot_tag:
    push es
    push bx
    call mn_get_bib                 ; ES:BX = BIB address (ES=0, BX=0x0600)
    cmp byte [es:bx+6], 1          ; BIB_BOOT_MODE at offset 6 from BIB base
    pop bx
    pop es
    je .pbt_debug
    mov si, msg_tag_release
    jmp .pbt_print
.pbt_debug:
    mov si, msg_tag_debug
.pbt_print:
    call mn_print_string
    ret

