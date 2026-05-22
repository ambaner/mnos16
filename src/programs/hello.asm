; =============================================================================
; HELLO.MNX — "Hello, world!" demo program for MNOS16
;
; This is the simplest possible user program.  It demonstrates:
;   - Standard MNEX header format (4-byte magic + 2-byte sector count)
;   - Using INT 0x80 SYS_PRINT_STRING to output text
;   - Clean return to shell via `ret`
;
; Loaded by the shell's `run` command into the Transient Program Area (TPA)
; at 0x9000.  Returns control to the shell by executing `ret`.
;
; Build:   nasm -f bin -I src/include/ -o build/boot/hello.mnx src/programs/hello.asm
; Run:     mnos:\> run HELLO.MNX
;
; See doc/PROGRAM-LOADER.md for the full program loader specification.
; =============================================================================

%include "syscalls.inc"
%include "memory.inc"

[BITS 16]
[ORG USER_PROG_BASE]                ; Loaded at 0x9000 by shell

; =============================================================================
; MNEX HEADER (6 bytes)
; =============================================================================
hello_magic     db 'MNEX'           ; Magic identifier — user-mode executable
hello_sectors   dw 1                ; Program size in sectors (1 = 512 bytes)

; =============================================================================
; ENTRY POINT (offset 6 — immediately after header)
;
; The shell calls here via: call USER_PROG_BASE + MNEX_HDR_SIZE
; =============================================================================
entry:
    ; Print greeting message
    mov si, msg_hello
    mov ah, SYS_PRINT_STRING
    int 0x80

    ; Return to shell (the `call` pushed a return address)
    ret

; =============================================================================
; DATA
; =============================================================================
msg_hello       db 'Hello, world!', 13, 10, 0

; =============================================================================
; PADDING — fill to sector boundary (1 sector = 512 bytes)
; =============================================================================
times 512 - ($ - $$) db 0
