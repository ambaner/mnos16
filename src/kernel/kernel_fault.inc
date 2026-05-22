; =============================================================================
; CPU EXCEPTION FAULT HANDLERS (debug build only)
;
; In real mode, the CPU dispatches exceptions through the IVT just like any
; other interrupt.  Without custom handlers, exceptions either go to BIOS
; stubs (which do nothing useful) or triple-fault the CPU (instant reboot).
;
; These handlers catch the most common x86 exceptions, log the exception name
; and faulting CS:IP to both serial and screen, dump all registers, then halt.
; This turns invisible crashes into diagnosable events.
;
; See doc/DEBUGGING.md §6 for full specification.
; =============================================================================
; =============================================================================
; CPU EXCEPTION FAULT HANDLERS (both release and debug builds)
;
; On any trapped CPU exception, the handler:
;   - Prints exception name + faulting CS:IP to screen
;   - Dumps all registers + FLAGS to screen
;   - Dumps top 4 stack words to screen
;   - (Debug only) Also logs everything to serial
;   - Halts the CPU permanently
; =============================================================================

; =============================================================================
; install_fault_handlers — Install CPU exception handlers into IVT vectors
;
; Also remaps the master PIC (8259A) so hardware IRQ 0-7 fire INT 0x20-0x27
; instead of INT 0x08-0x0F.  This frees INT 0x08 for the #DF exception handler
; (which otherwise conflicts with the hardware timer at IRQ0).
;
; Must be called with interrupts safe to disable (early kernel init).
; =============================================================================
install_fault_handlers:
    cli
    push es
    push ds

    xor ax, ax
    mov es, ax                          ; ES = 0x0000 (IVT segment)
    mov ds, ax                          ; DS = 0x0000 (for reading IVT too)

    ; ─── Step 1: Relocate BIOS IRQ handlers from IVT[0x08-0x0F] to [0x20-0x27]
    ; Copy 8 IVT entries (4 bytes each = 32 bytes) from 0x08*4 to 0x20*4
    mov cx, 8
    mov si, 0x08 * 4                    ; Source: IVT[0x08] = offset 0x20
    mov di, 0x20 * 4                    ; Dest:   IVT[0x20] = offset 0x80
.copy_irq_vectors:
    mov ax, [ds:si]                     ; Offset of BIOS ISR
    mov [es:di], ax
    mov ax, [ds:si + 2]                 ; Segment of BIOS ISR
    mov [es:di + 2], ax
    add si, 4
    add di, 4
    dec cx
    jnz .copy_irq_vectors

    ; ─── Step 2: Reprogram master PIC (8259A) to use base vector 0x20 ────────
    ; ICW1: Initialize + ICW4 needed
    mov al, 0x11
    out 0x20, al
    ; Small I/O delay (let PIC process)
    jmp short $+2
    jmp short $+2

    ; ICW2: New base interrupt vector = 0x20
    mov al, 0x20
    out 0x21, al
    jmp short $+2
    jmp short $+2

    ; ICW3: Slave PIC on IRQ2 (bit 2)
    mov al, 0x04
    out 0x21, al
    jmp short $+2
    jmp short $+2

    ; ICW4: 8086 mode, normal EOI
    mov al, 0x01
    out 0x21, al
    jmp short $+2
    jmp short $+2

    ; ─── Step 3: Install CPU exception handlers ───────────────────────────────

    ; INT 0x00 — Divide Error (#DE)
    mov word [es:0x00*4],   fault_de
    mov word [es:0x00*4+2], cs

    ; INT 0x01 — Debug / Single Step (#DB)
    mov word [es:0x01*4],   fault_db
    mov word [es:0x01*4+2], cs

    ; INT 0x04 — Overflow (#OF)
    mov word [es:0x04*4],   fault_of
    mov word [es:0x04*4+2], cs

    ; INT 0x05 — Bound Range Exceeded (#BR)
    mov word [es:0x05*4],   fault_br
    mov word [es:0x05*4+2], cs

    ; INT 0x06 — Invalid Opcode (#UD)
    mov word [es:0x06*4],   fault_ud
    mov word [es:0x06*4+2], cs

    ; INT 0x07 — Device Not Available (#NM)
    mov word [es:0x07*4],   fault_nm
    mov word [es:0x07*4+2], cs

    ; INT 0x08 — Double Fault (#DF) — safe now that IRQ0 is at INT 0x20
    mov word [es:0x08*4],   fault_df
    mov word [es:0x08*4+2], cs

    pop ds
    pop es
    sti
    ret

; =============================================================================
; fault_common — Shared exception handler core
;
; Each specific handler pushes the address of its name string, then jumps here.
;
; Stack frame on entry:
;   SP+0  = name pointer (pushed by stub)
;   SP+2  = faulting IP  (pushed by CPU)
;   SP+4  = faulting CS  (pushed by CPU)
;   SP+6  = faulting FLAGS (pushed by CPU)
;
; After saving 7 registers (14 bytes):
;   SP+0  = BP   SP+2  = DI   SP+4  = SI   SP+6  = DX
;   SP+8  = CX   SP+10 = BX   SP+12 = AX   SP+14 = name ptr
;   SP+16 = IP   SP+18 = CS   SP+20 = FLAGS
;   SP+22 = original stack top (pre-fault)
; =============================================================================
fault_common:
    ; Save all registers at the moment of fault
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp

%ifdef DEBUG
    ; --- Serial output (debug builds) ----------------------------------------
    ; Matches on-screen dump: all regs from saved stack frame + stack dump
    mov si, .fault_banner
    call serial_puts

    ; Print exception name to serial
    mov bx, sp
    mov si, [ss:bx + 14]               ; Name pointer
    call serial_puts

    ; Print " at XXXX:XXXX" to serial
    mov si, .fault_at
    call serial_puts
    mov ax, [ss:bx + 18]               ; Faulting CS
    call serial_hex16
    mov al, ':'
    call serial_putc
    mov bx, sp
    mov ax, [ss:bx + 16]               ; Faulting IP
    call serial_hex16
    call serial_crlf

    ; Line 2: AX BX CX DX
    mov si, .reg_ax
    call serial_puts
    mov bx, sp
    mov ax, [ss:bx + 12]
    call serial_hex16
    mov si, .reg_bx
    call serial_puts
    mov bx, sp
    mov ax, [ss:bx + 10]
    call serial_hex16
    mov si, .reg_cx
    call serial_puts
    mov bx, sp
    mov ax, [ss:bx + 8]
    call serial_hex16
    mov si, .reg_dx
    call serial_puts
    mov bx, sp
    mov ax, [ss:bx + 6]
    call serial_hex16
    call serial_crlf

    ; Line 3: SI DI BP SP
    mov si, .reg_si
    call serial_puts
    mov bx, sp
    mov ax, [ss:bx + 4]
    call serial_hex16
    mov si, .reg_di
    call serial_puts
    mov bx, sp
    mov ax, [ss:bx + 2]
    call serial_hex16
    mov si, .reg_bp
    call serial_puts
    mov bx, sp
    mov ax, [ss:bx + 0]
    call serial_hex16
    mov si, .reg_sp
    call serial_puts
    mov ax, sp
    add ax, 22                          ; Original SP before fault frame
    call serial_hex16
    call serial_crlf

    ; Line 4: DS ES SS FL
    mov si, .reg_ds
    call serial_puts
    mov ax, ds
    call serial_hex16
    mov si, .reg_es
    call serial_puts
    mov ax, es
    call serial_hex16
    mov si, .reg_ss
    call serial_puts
    mov ax, ss
    call serial_hex16
    mov si, .reg_fl
    call serial_puts
    mov bx, sp
    mov ax, [ss:bx + 20]
    call serial_hex16
    call serial_crlf

    ; Line 5: Stack dump (top 4 words from original stack)
    mov si, .stack_lbl
    call serial_puts
    mov bx, sp
    mov ax, [ss:bx + 22]
    call serial_hex16
    mov al, ' '
    call serial_putc
    mov bx, sp
    mov ax, [ss:bx + 24]
    call serial_hex16
    mov al, ' '
    call serial_putc
    mov bx, sp
    mov ax, [ss:bx + 26]
    call serial_hex16
    mov al, ' '
    call serial_putc
    mov bx, sp
    mov ax, [ss:bx + 28]
    call serial_hex16
    call serial_crlf
%endif

    ; --- Screen output (both builds) -----------------------------------------
    ; Line 1: "*** FAULT: <name>"
    mov si, .fault_banner
    call puts

    mov bx, sp
    mov si, [ss:bx + 14]               ; Name pointer
    call puts

    ; Line 2: "at XXXX:XXXX"
    mov si, .fault_at
    call puts
    mov bx, sp
    mov ax, [ss:bx + 18]               ; Faulting CS
    call .screen_hex16
    mov al, ':'
    mov ah, 0x0E
    xor bh, bh
    int 0x10
    mov bx, sp
    mov ax, [ss:bx + 16]               ; Faulting IP
    call .screen_hex16

    mov si, .fault_crlf
    call puts

    ; Line 3+: Register dump on screen
    ; AX
    mov si, .reg_ax
    call puts
    mov bx, sp
    mov ax, [ss:bx + 12]
    call .screen_hex16

    ; BX
    mov si, .reg_bx
    call puts
    mov bx, sp
    mov ax, [ss:bx + 10]
    call .screen_hex16

    ; CX
    mov si, .reg_cx
    call puts
    mov bx, sp
    mov ax, [ss:bx + 8]
    call .screen_hex16

    ; DX
    mov si, .reg_dx
    call puts
    mov bx, sp
    mov ax, [ss:bx + 6]
    call .screen_hex16

    mov si, .fault_crlf
    call puts

    ; SI
    mov si, .reg_si
    call puts
    mov bx, sp
    mov ax, [ss:bx + 4]
    call .screen_hex16

    ; DI
    mov si, .reg_di
    call puts
    mov bx, sp
    mov ax, [ss:bx + 2]
    call .screen_hex16

    ; BP
    mov si, .reg_bp
    call puts
    mov bx, sp
    mov ax, [ss:bx + 0]
    call .screen_hex16

    ; SP (original = current SP + 22)
    mov si, .reg_sp
    call puts
    mov ax, sp
    add ax, 22
    call .screen_hex16

    mov si, .fault_crlf
    call puts

    ; DS
    mov si, .reg_ds
    call puts
    mov ax, ds
    call .screen_hex16

    ; ES
    mov si, .reg_es
    call puts
    mov ax, es
    call .screen_hex16

    ; SS
    mov si, .reg_ss
    call puts
    mov ax, ss
    call .screen_hex16

    ; FLAGS
    mov si, .reg_fl
    call puts
    mov bx, sp
    mov ax, [ss:bx + 20]
    call .screen_hex16

    mov si, .fault_crlf
    call puts

    ; Line 4: Stack dump (top 4 words from original stack)
    mov si, .stack_lbl
    call puts

    mov bx, sp
    mov ax, [ss:bx + 22]               ; Stack word 0
    call .screen_hex16
    mov al, ' '
    mov ah, 0x0E
    xor bh, bh
    int 0x10
    mov bx, sp
    mov ax, [ss:bx + 24]               ; Stack word 1
    call .screen_hex16
    mov al, ' '
    mov ah, 0x0E
    xor bh, bh
    int 0x10
    mov bx, sp
    mov ax, [ss:bx + 26]               ; Stack word 2
    call .screen_hex16
    mov al, ' '
    mov ah, 0x0E
    xor bh, bh
    int 0x10
    mov bx, sp
    mov ax, [ss:bx + 28]               ; Stack word 3
    call .screen_hex16

    mov si, .fault_crlf
    call puts

    ; Final message
    mov si, .fault_halted
    call puts

    ; Halt permanently
    cli
.fault_halt:
    hlt
    jmp .fault_halt

; --- Screen hex16 helper (print AX as 4-digit hex via BIOS teletype) ----------
.screen_hex16:
    push cx
    push ax
    mov cx, 4
.sh16_loop:
    rol ax, 4
    push ax
    and al, 0x0F
    add al, '0'
    cmp al, '9'
    jbe .sh16_digit
    add al, 7
.sh16_digit:
    mov ah, 0x0E
    push bx
    xor bx, bx
    int 0x10
    pop bx
    pop ax
    dec cx
    jnz .sh16_loop
    pop ax
    pop cx
    ret

; --- Fault handler data strings -----------------------------------------------
.fault_banner:  db 13, 10, '*** FAULT: ', 0
.fault_at:      db ' at ', 0
.fault_crlf:    db 13, 10, 0
.fault_halted:  db 'System halted.', 13, 10, 0
.reg_ax:        db 'AX=', 0
.reg_bx:        db ' BX=', 0
.reg_cx:        db ' CX=', 0
.reg_dx:        db ' DX=', 0
.reg_si:        db 'SI=', 0
.reg_di:        db ' DI=', 0
.reg_bp:        db ' BP=', 0
.reg_sp:        db ' SP=', 0
.reg_ds:        db 'DS=', 0
.reg_es:        db ' ES=', 0
.reg_ss:        db ' SS=', 0
.reg_fl:        db ' FL=', 0
.stack_lbl:     db 'Stack: ', 0

; =============================================================================
; Specific fault handler stubs
; Each pushes its name string pointer, then jumps to fault_common.
; =============================================================================

fault_de:                               ; INT 0x00 — Divide Error
    push word .de_name
    jmp fault_common
.de_name: db '#DE Divide Error', 0

fault_db:                               ; INT 0x01 — Debug / Single Step
    push word .db_name
    jmp fault_common
.db_name: db '#DB Debug', 0

fault_of:                               ; INT 0x04 — Overflow
    push word .of_name
    jmp fault_common
.of_name: db '#OF Overflow', 0

fault_br:                               ; INT 0x05 — Bound Range Exceeded
    push word .br_name
    jmp fault_common
.br_name: db '#BR Bound Range', 0

fault_ud:                               ; INT 0x06 — Invalid Opcode
    push word .ud_name
    jmp fault_common
.ud_name: db '#UD Invalid Opcode', 0

fault_nm:                               ; INT 0x07 — Device Not Available
    push word .nm_name
    jmp fault_common
.nm_name: db '#NM No Device', 0

fault_df:                               ; INT 0x08 — Double Fault
    push word .df_name
    jmp fault_common
.df_name: db '#DF Double Fault', 0
