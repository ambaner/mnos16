; =============================================================================
; serial.inc — COM1 serial port I/O for debug logging
;
; Provides low-level serial output at 115200 baud, 8N1 via COM1 (0x3F8).
; Pure port I/O — no BIOS calls needed.
;
; All code is conditional on %ifdef DEBUG.  In release builds, this file
; contributes exactly 0 bytes to the binary.
;
; Functions:
;   serial_init   — Initialize COM1 UART (call once during early boot)
;   serial_putc   — Send one character
;   serial_puts   — Send NUL-terminated string
;   serial_hex8   — Send AL as 2-digit hex
;   serial_hex16  — Send AX as 4-digit hex
;   serial_crlf   — Send CR+LF
;
; Hyper-V COM1 setup (run once, admin PowerShell):
;   Set-VMComPort -VMName "MNOS16" -Number 1 -Path "\\.\pipe\MNOS16-SERIAL"
;
; Read output with PuTTY (Serial, \\.\pipe\minios-serial) or PowerShell.
; =============================================================================

%ifdef DEBUG

; --- UART register addresses (COM1 = 0x3F8) ---------------------------------
COM1_BASE   equ 0x3F8
COM1_THR    equ COM1_BASE + 0       ; Transmit Holding Register (DLAB=0)
COM1_IER    equ COM1_BASE + 1       ; Interrupt Enable Register (DLAB=0)
COM1_FCR    equ COM1_BASE + 2       ; FIFO Control Register
COM1_LCR    equ COM1_BASE + 3       ; Line Control Register
COM1_MCR    equ COM1_BASE + 4       ; Modem Control Register
COM1_LSR    equ COM1_BASE + 5       ; Line Status Register

; =============================================================================
; serial_init — Initialize COM1 at 115200 baud, 8N1
;
; Must be called once during early boot (kernel init).
; Input:  none
; Output: none
; Clobbers: AX, DX
; =============================================================================
serial_init:
    ; Disable all UART interrupts (we poll, not IRQ)
    mov dx, COM1_IER
    xor al, al
    out dx, al

    ; Set DLAB to access baud rate divisor
    mov dx, COM1_LCR
    mov al, 0x80                    ; DLAB = 1
    out dx, al

    ; Baud rate 115200: divisor = 1843200 / (16 * 115200) = 1
    mov dx, COM1_BASE               ; DLL (DLAB=1)
    mov al, 1
    out dx, al
    mov dx, COM1_BASE + 1           ; DLM (DLAB=1)
    xor al, al
    out dx, al

    ; 8 data bits, no parity, 1 stop bit — clears DLAB
    mov dx, COM1_LCR
    mov al, 0x03                    ; 8N1, DLAB=0
    out dx, al

    ; Enable and clear FIFOs, 14-byte trigger level
    mov dx, COM1_FCR
    mov al, 0xC7
    out dx, al

    ; DTR + RTS + OUT2 (some virtual UARTs check these)
    mov dx, COM1_MCR
    mov al, 0x0B
    out dx, al

    ret

; =============================================================================
; serial_putc — Send one character to COM1
;
; Input:  AL = character to send
; Output: none
; Preserves: all registers
; =============================================================================
serial_putc:
    push dx
    push ax
    ; Wait for transmit buffer empty (LSR bit 5)
    mov dx, COM1_LSR
.wait_tx:
    in al, dx
    test al, 0x20
    jz .wait_tx
    ; Send the character
    pop ax
    mov dx, COM1_THR
    out dx, al
    pop dx
    ret

; =============================================================================
; serial_puts — Send NUL-terminated string to COM1
;
; Input:  SI = pointer to NUL-terminated string
; Output: none
; Preserves: AX, SI
; =============================================================================
serial_puts:
    push ax
    push si
.loop:
    lodsb
    test al, al
    jz .done
    call serial_putc
    jmp .loop
.done:
    pop si
    pop ax
    ret

; =============================================================================
; serial_hex16 — Send AX as 4-digit hex string to COM1
;
; Input:  AX = 16-bit value
; Output: none
; Preserves: AX, CX
; =============================================================================
serial_hex16:
    push cx
    push ax
    mov cx, 4
.hex_loop:
    rol ax, 4                       ; Rotate highest nibble to lowest
    push ax
    and al, 0x0F
    add al, '0'
    cmp al, '9'
    jbe .digit
    add al, 7                       ; 'A' - '9' - 1 = 7
.digit:
    call serial_putc
    pop ax
    dec cx
    jnz .hex_loop
    pop ax
    pop cx
    ret

; =============================================================================
; serial_hex8 — Send AL as 2-digit hex string to COM1
;
; Input:  AL = 8-bit value
; Output: none
; Preserves: AX, CX
; =============================================================================
serial_hex8:
    push ax
    push cx
    mov cx, 2
    rol al, 4                       ; High nibble first
.hex8_loop:
    push ax
    and al, 0x0F
    add al, '0'
    cmp al, '9'
    jbe .hex8_digit
    add al, 7
.hex8_digit:
    call serial_putc
    pop ax
    rol al, 4
    dec cx
    jnz .hex8_loop
    pop cx
    pop ax
    ret

; =============================================================================
; serial_crlf — Send CR+LF to COM1
;
; Input:  none
; Output: none
; Preserves: AX
; =============================================================================
serial_crlf:
    push ax
    mov al, 13
    call serial_putc
    mov al, 10
    call serial_putc
    pop ax
    ret

%endif ; DEBUG
