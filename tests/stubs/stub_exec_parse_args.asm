; stub_exec_parse_args.asm — Test harness for kernel exec_parse_args
;
; Assembled as flat binary: nasm -f bin -I src/include/ -I src/kernel/ -o tests/bin/stub_exec_parse_args.bin
;
; The test harness writes the input string to exec_args_buf (at a known offset),
; then calls entry (0x1000).  After hlt, the test reads ARGV_ARGC and ARGV_PTRS.

[bits 16]
[org 0x1000]

%include "memory.inc"
%include "mnfs.inc"
%include "syscalls.inc"

entry:
    call exec_parse_args
    hlt

; Include the exec_parse_args code from the kernel syscall handler.
; We extract just the subroutine here since it's defined within the
; syscall_handler label scope.  For testability, we replicate it standalone.

exec_parse_args:
    push ax
    push bx
    push cx
    push si
    push di

    xor cl, cl
    mov byte [ARGV_ARGC], 0

    mov si, exec_args_buf
    cmp byte [si], 0
    je .epa_done

    mov di, ARGV_STORAGE

.epa_skip_spaces:
    lodsb
    cmp al, ' '
    je .epa_skip_spaces
    cmp al, 9
    je .epa_skip_spaces
    cmp al, 0
    je .epa_done
    dec si

    cmp cl, ARGV_MAX_ARGS
    jae .epa_done

    mov bx, cx
    and bx, 0x00FF
    shl bx, 1
    mov [ARGV_PTRS + bx], di

    cmp byte [si], '"'
    je .epa_quoted

.epa_unquoted:
    lodsb
    cmp al, ' '
    je .epa_end_arg
    cmp al, 9
    je .epa_end_arg
    cmp al, 0
    je .epa_end_arg_final
    cmp di, ARGV_STORAGE_END
    jae .epa_done
    stosb
    jmp .epa_unquoted

.epa_quoted:
    inc si
.epa_quoted_loop:
    lodsb
    cmp al, '"'
    je .epa_end_arg
    cmp al, 0
    je .epa_end_arg_final
    cmp di, ARGV_STORAGE_END
    jae .epa_done
    stosb
    jmp .epa_quoted_loop

.epa_end_arg:
    xor al, al
    stosb
    inc cl
    jmp .epa_skip_spaces

.epa_end_arg_final:
    xor al, al
    stosb
    inc cl

.epa_done:
    mov [ARGV_ARGC], cl

    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; --- Data area (at known offset for test harness) ---
exec_args_buf:  times 128 db 0
