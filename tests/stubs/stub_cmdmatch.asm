; stub_cmdmatch.asm — Test harness for cmdmatch
;
; The test sets DS:SI to the command buffer and DS:DI to the command name,
; calls entry.  After hlt, the test checks ZF in FLAGS to determine match.

[bits 16]
[org 0x1000]

entry:
    call cmdmatch
    hlt

; --- cmdmatch (inlined from shell_readline.inc) ------------------------------
cmdmatch:
    push si
    push di
.cm_loop:
    mov al, [di]
    test al, al
    jz .cm_end_cmd
    cmp al, [si]
    jne .cm_ne
    inc si
    inc di
    jmp .cm_loop

.cm_end_cmd:
    mov al, [si]
    cmp al, ' '
    je .cm_ok
    test al, al
    je .cm_ok
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
