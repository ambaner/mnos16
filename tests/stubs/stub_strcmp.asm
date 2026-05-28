; stub_strcmp.asm — Test harness for strcmp
;
; The test sets DS:SI and DS:DI to two strings, calls entry.
; After hlt, the test checks ZF in FLAGS to determine equality.

[bits 16]
[org 0x1000]

; strcmp uses no external constants, but it does reference SYS_PRINT_CHAR
; in nearby code.  We only include the routine itself here.

entry:
    call strcmp
    hlt

; --- strcmp (inlined from shell_readline.inc) ---------------------------------
; We inline it rather than %include the full file because shell_readline.inc
; contains routines that reference syscall constants we don't want to pull in.

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
