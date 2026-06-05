; =============================================================================
; mnoslib_io.inc — User-mode wrappers for INT 0x80 console/keyboard syscalls.
;
; These are pure 1:1 wrappers around the syscalls listed in syscalls.inc.
; Each wrapper is a single `mov ah, SYS_* / int 0x80 / ret`.  The preservation
; contract of each wrapper is the same as that of the syscall it wraps:
; registers used to return values are clobbered, everything else is preserved
; by the kernel's syscall epilogue.
;
; Dependencies: %include "syscalls.inc" before this file.
; =============================================================================

%ifndef MNOSLIB_IO_INC_INCLUDED
%define MNOSLIB_IO_INC_INCLUDED

; -----------------------------------------------------------------------------
; mn_print_string — Print a NUL-terminated string.
; Input:    DS:SI = NUL-terminated string
; Output:   none (CF undefined)
; -----------------------------------------------------------------------------
mn_print_string:
    mov ah, SYS_PRINT_STRING
    int 0x80
    ret

; -----------------------------------------------------------------------------
; mn_print_char — Print a single character.
; Input:    AL = character (printable; CR/LF treated literally)
; Output:   none
; -----------------------------------------------------------------------------
mn_print_char:
    mov ah, SYS_PRINT_CHAR
    int 0x80
    ret

; -----------------------------------------------------------------------------
; mn_print_hex8 — Print AL as two uppercase hex digits.
; Input:    AL = byte to print
; Output:   none (AX clobbered)
; -----------------------------------------------------------------------------
mn_print_hex8:
    mov ah, SYS_PRINT_HEX8
    int 0x80
    ret

; -----------------------------------------------------------------------------
; mn_print_hex16 — Print DX as four uppercase hex digits.
; Input:    DX = word to print
; Output:   none (AX clobbered)
; -----------------------------------------------------------------------------
mn_print_hex16:
    mov ah, SYS_PRINT_HEX16
    int 0x80
    ret

; -----------------------------------------------------------------------------
; mn_print_dec16 — Print DX as an unsigned decimal number (1–5 digits).
; Input:    DX = unsigned word to print
; Output:   none (AX clobbered)
; -----------------------------------------------------------------------------
mn_print_dec16:
    mov ah, SYS_PRINT_DEC16
    int 0x80
    ret

; -----------------------------------------------------------------------------
; mn_read_key — Block until a key is pressed; consume it.
; Input:    none
; Output:   AH = BIOS scancode, AL = ASCII (0 for non-ASCII keys)
; -----------------------------------------------------------------------------
mn_read_key:
    mov ah, SYS_READ_KEY
    int 0x80
    ret

; -----------------------------------------------------------------------------
; mn_peek_key — Non-blocking key check; does NOT consume the key.
; Input:    none
; Output:   ZF=1 if no key waiting, ZF=0 if a key is buffered
;           When ZF=0: AH = scancode, AL = ASCII
;           A subsequent mn_read_key is required to drain the key.
; -----------------------------------------------------------------------------
mn_peek_key:
    mov ah, SYS_PEEK_KEY
    int 0x80
    ret

; -----------------------------------------------------------------------------
; mn_wait_key — Print "Press any key…" prompt, wait, then clear screen.
; Input:    none
; Output:   none
; -----------------------------------------------------------------------------
mn_wait_key:
    mov ah, SYS_WAIT_KEY
    int 0x80
    ret

; -----------------------------------------------------------------------------
; mn_clear_screen — Clear the 80x25 text screen and home the cursor.
; Input:    none
; Output:   none
; -----------------------------------------------------------------------------
mn_clear_screen:
    mov ah, SYS_CLEAR_SCREEN
    int 0x80
    ret

; -----------------------------------------------------------------------------
; mn_set_cursor — Move the cursor to (row, col).
; Input:    DH = row (0–24), DL = column (0–79)
; Output:   none
; -----------------------------------------------------------------------------
mn_set_cursor:
    mov ah, SYS_SET_CURSOR
    int 0x80
    ret

; -----------------------------------------------------------------------------
; mn_get_cursor — Read the current cursor position.
; Input:    none
; Output:   DH = row, DL = column
; -----------------------------------------------------------------------------
mn_get_cursor:
    mov ah, SYS_GET_CURSOR
    int 0x80
    ret

%endif ; MNOSLIB_IO_INC_INCLUDED
