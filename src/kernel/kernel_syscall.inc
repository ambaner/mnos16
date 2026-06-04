syscall_handler:
    ; --- Stack canary check (debug only) -------------------------------------
    ; Verify the canary at 0x6C00 is intact before dispatching.  This catches
    ; stack overflows that occurred during the previous syscall handler or in
    ; user-mode code between syscalls.  CANARY_CHECK preserves all registers
    ; and FLAGS, so AH (function number) and all arguments are untouched.
    ; In release builds, CANARY_CHECK expands to nothing (0 bytes).
    CANARY_CHECK

    mov [cs:.sc_temp], bx           ; Save BX in kernel data area

%ifdef DEBUG
    ; --- Increment nesting depth ---
    inc byte [cs:BIB_INT_DEPTH]

    ; --- Syscall trace: log function name + AX + original BX to serial ---
    push si
    push ax

    ; Look up syscall name from table
    mov si, .sc_trace_pfx           ; "[SYS] "
    call serial_puts

    movzx bx, ah
    cmp bx, SYSCALL_MAX
    ja .sc_trace_noname
    shl bx, 1                       ; word offset into name pointer table
    mov si, [cs:.sc_name_table + bx]
    test si, si
    jz .sc_trace_noname
    call serial_puts
    jmp .sc_trace_ax_part

.sc_trace_noname:
    mov si, .sc_trace_ah            ; "AH="
    call serial_puts
    mov al, ah
    call serial_hex8

.sc_trace_ax_part:
    mov si, .sc_trace_ax            ; " AX="
    call serial_puts
    pop ax                          ; restore AX for printing
    push ax                         ; re-save
    call serial_hex16

    mov si, .sc_trace_bx            ; " BX="
    call serial_puts
    mov ax, [cs:.sc_temp]           ; original BX
    call serial_hex16

    ; Print CF state from caller's FLAGS (on interrupt stack frame)
    ; Stack: [SP+0]=AX, [SP+2]=SI, [SP+4]=IP, [SP+6]=CS, [SP+8]=FLAGS
    mov si, .sc_trace_cf            ; " CF="
    call serial_puts
    push bp
    mov bp, sp
    test byte [bp+10], 0x01         ; FLAGS at BP+10 (BP,AX,SI,IP,CS,FLAGS)
    pop bp
    jz .sc_cf_zero
    mov al, '1'
    jmp .sc_cf_print
.sc_cf_zero:
    mov al, '0'
.sc_cf_print:
    call serial_putc

    ; Print IF state from caller's FLAGS
    mov si, .sc_trace_if            ; " IF="
    call serial_puts
    push bp
    mov bp, sp
    test byte [bp+11], 0x02         ; IF is bit 9 = bit 1 of high byte at BP+11
    pop bp
    jz .sc_if_zero
    mov al, '1'
    jmp .sc_if_print
.sc_if_zero:
    mov al, '0'
.sc_if_print:
    call serial_putc

    ; Print nesting depth
    mov si, .sc_trace_depth         ; " D="
    call serial_puts
    mov al, [cs:BIB_INT_DEPTH]
    call serial_hex8

    call serial_crlf
    pop ax
    mov bx, [cs:.sc_temp]           ; restore BX (was clobbered by name lookup)
    pop si
%endif

    movzx bx, ah                    ; BX = function number (zero-extended)
    cmp bx, SYSCALL_MAX
    ja .sc_unknown

    add bx, bx                      ; BX = function number * 2 (word offset)
    mov bx, [cs:.sc_table + bx]     ; BX = handler address from jump table
    xchg bx, [cs:.sc_temp]          ; BX = original value, .sc_temp = handler
    jmp [cs:.sc_temp]               ; Jump to handler with all regs intact

.sc_unknown:
    mov bx, [cs:.sc_temp]           ; Restore BX
    stc
    syscall_ret_cf                  ; Must propagate CF to caller

; Temporary storage for syscall dispatch (single-threaded real mode,
; so no reentrancy concerns — interrupts are masked during INT handlers).
.sc_temp: dw 0

%ifdef DEBUG
.sc_trace_pfx: db '[SYS] ', 0
.sc_trace_ah:  db 'AH=', 0
.sc_trace_ax:  db ' AX=', 0
.sc_trace_bx:  db ' BX=', 0
.sc_trace_cf:  db ' CF=', 0
.sc_trace_if:  db ' IF=', 0

; EXIT trace strings for READ_SECTOR
.sc_exit_pfx:  db '[SYS] READ_SECTOR -> ', 0
.sc_exit_cf:   db 'CF=', 0
.sc_exit_if:   db ' IF=', 0
.sc_exit_err:  db ' AH=', 0
.sc_trace_depth: db ' D=', 0

; Spawn reload debug
.spawn_ivt_pfx: db '[SPAWN] IVT81=', 0
.spawn_diag_pfx: db '[EXIT] spawn_depth=', 0

; DAP dump prefix
.sc_dap_pfx:   db '[SYS] DAP: ', 0

; Syscall name strings (short, human-readable)
.sn_01: db 'PRINT_STRING', 0
.sn_02: db 'PRINT_CHAR', 0
.sn_03: db 'READ_KEY', 0
.sn_04: db 'READ_SECTOR', 0
.sn_05: db 'GET_VERSION', 0
.sn_06: db 'CLEAR_SCREEN', 0
.sn_07: db 'SET_CURSOR', 0
.sn_08: db 'GET_CURSOR', 0
.sn_09: db 'CHECK_A20', 0
.sn_0a: db 'GET_CONV_MEM', 0
.sn_0b: db 'GET_EXT_MEM', 0
.sn_0c: db 'GET_E820', 0
.sn_0d: db 'REBOOT', 0
.sn_0e: db 'GET_DRIVE_INFO', 0
.sn_0f: db 'GET_BIB', 0
.sn_10: db 'PRINT_HEX8', 0
.sn_11: db 'PRINT_HEX16', 0
.sn_12: db 'PRINT_DEC16', 0
.sn_13: db 'WAIT_KEY', 0
.sn_14: db 'GET_EQUIP', 0
.sn_15: db 'GET_VIDEO', 0
.sn_16: db 'GET_BDA_BYTE', 0
.sn_17: db 'GET_BDA_WORD', 0
.sn_18: db 'CPUID', 0
.sn_19: db 'CHECK_CPUID', 0
.sn_1a: db 'GET_EDD', 0
.sn_1b: db 'GET_IVT', 0
.sn_20: db 'DBG_PRINT', 0
.sn_21: db 'DBG_HEX16', 0
.sn_22: db 'DBG_REGS', 0
.sn_23: db 'EXIT', 0
.sn_24: db 'GET_ARGS', 0
.sn_25: db 'GET_ARGC', 0
.sn_26: db 'GET_ARGV', 0
.sn_27: db 'EXEC', 0
.sn_28: db 'SPAWN', 0
.sn_29: db 'PEEK_KEY', 0

; Name pointer table (indexed by AH, 0x00–0x1B)
.sc_name_table:
    dw 0            ; 0x00 — unused
    dw .sn_01       ; 0x01
    dw .sn_02       ; 0x02
    dw .sn_03       ; 0x03
    dw .sn_04       ; 0x04
    dw .sn_05       ; 0x05
    dw .sn_06       ; 0x06
    dw .sn_07       ; 0x07
    dw .sn_08       ; 0x08
    dw .sn_09       ; 0x09
    dw .sn_0a       ; 0x0A
    dw .sn_0b       ; 0x0B
    dw .sn_0c       ; 0x0C
    dw .sn_0d       ; 0x0D
    dw .sn_0e       ; 0x0E
    dw .sn_0f       ; 0x0F
    dw .sn_10       ; 0x10
    dw .sn_11       ; 0x11
    dw .sn_12       ; 0x12
    dw .sn_13       ; 0x13
    dw .sn_14       ; 0x14
    dw .sn_15       ; 0x15
    dw .sn_16       ; 0x16
    dw .sn_17       ; 0x17
    dw .sn_18       ; 0x18
    dw .sn_19       ; 0x19
    dw .sn_1a       ; 0x1A
    dw .sn_1b       ; 0x1B
    dw 0            ; 0x1C — reserved
    dw 0            ; 0x1D — reserved
    dw 0            ; 0x1E — reserved
    dw 0            ; 0x1F — reserved
    dw .sn_20       ; 0x20
    dw .sn_21       ; 0x21
    dw .sn_22       ; 0x22
    dw .sn_23       ; 0x23
    dw .sn_24       ; 0x24
    dw .sn_25       ; 0x25
    dw .sn_26       ; 0x26
    dw .sn_27       ; 0x27
    dw .sn_28       ; 0x28
    dw .sn_29       ; 0x29
%endif

; --- Syscall jump table (39 entries: 0x00–0x26, gap 0x1C–0x1F reserved) ------
.sc_table:
    dw .sc_unknown          ; 0x00 — unused
    dw .fn_print_string     ; 0x01 — SYS_PRINT_STRING
    dw .fn_print_char       ; 0x02 — SYS_PRINT_CHAR
    dw .fn_read_key         ; 0x03 — SYS_READ_KEY
    dw .fn_read_sector      ; 0x04 — SYS_READ_SECTOR
    dw .fn_get_version      ; 0x05 — SYS_GET_VERSION
    dw .fn_clear_screen     ; 0x06 — SYS_CLEAR_SCREEN
    dw .fn_set_cursor       ; 0x07 — SYS_SET_CURSOR
    dw .fn_get_cursor       ; 0x08 — SYS_GET_CURSOR
    dw .fn_check_a20        ; 0x09 — SYS_CHECK_A20
    dw .fn_get_conv_mem     ; 0x0A — SYS_GET_CONV_MEM
    dw .fn_get_ext_mem      ; 0x0B — SYS_GET_EXT_MEM
    dw .fn_get_e820         ; 0x0C — SYS_GET_E820
    dw .fn_reboot           ; 0x0D — SYS_REBOOT
    dw .fn_get_drive_info   ; 0x0E — SYS_GET_DRIVE_INFO
    dw .fn_get_bib          ; 0x0F — SYS_GET_BIB
    dw .fn_print_hex8       ; 0x10 — SYS_PRINT_HEX8
    dw .fn_print_hex16      ; 0x11 — SYS_PRINT_HEX16
    dw .fn_print_dec16      ; 0x12 — SYS_PRINT_DEC16
    dw .fn_wait_key         ; 0x13 — SYS_WAIT_KEY
    dw .fn_get_equip        ; 0x14 — SYS_GET_EQUIP
    dw .fn_get_video        ; 0x15 — SYS_GET_VIDEO
    dw .fn_get_bda_byte     ; 0x16 — SYS_GET_BDA_BYTE
    dw .fn_get_bda_word     ; 0x17 — SYS_GET_BDA_WORD
    dw .fn_cpuid            ; 0x18 — SYS_CPUID
    dw .fn_check_cpuid      ; 0x19 — SYS_CHECK_CPUID
    dw .fn_get_edd          ; 0x1A — SYS_GET_EDD
    dw .fn_get_ivt          ; 0x1B — SYS_GET_IVT
    dw .sc_unknown          ; 0x1C — reserved
    dw .sc_unknown          ; 0x1D — reserved
    dw .sc_unknown          ; 0x1E — reserved
    dw .sc_unknown          ; 0x1F — reserved
    dw .fn_dbg_print        ; 0x20 — SYS_DBG_PRINT
    dw .fn_dbg_hex16        ; 0x21 — SYS_DBG_HEX16
    dw .fn_dbg_regs         ; 0x22 — SYS_DBG_REGS
    dw .fn_exit             ; 0x23 — SYS_EXIT
    dw .fn_get_args         ; 0x24 — SYS_GET_ARGS
    dw .fn_get_argc         ; 0x25 — SYS_GET_ARGC
    dw .fn_get_argv         ; 0x26 — SYS_GET_ARGV
    dw .fn_exec             ; 0x27 — SYS_EXEC
    dw .fn_spawn            ; 0x28 — SYS_SPAWN
    dw .fn_peek_key         ; 0x29 — SYS_PEEK_KEY

; ─── SYS_PRINT_STRING (AH=0x01)──────────────────────────────────────────────
; Input:  DS:SI = pointer to null-terminated string
; Output: none
; ──────────────────────────────────────────────────────────────────────────────
.fn_print_string:
    push ax
    push si
    push bx
.ps_loop:
    lodsb
    test al, al
    jz .ps_done
    mov ah, 0x0E
    xor bh, bh
    int 0x10
    jmp .ps_loop
.ps_done:
    pop bx
    pop si
    pop ax
    syscall_iret

; ─── SYS_PRINT_CHAR (AH=0x02) ────────────────────────────────────────────────
; Input:  AL = character to print
; Output: none
; ──────────────────────────────────────────────────────────────────────────────
.fn_print_char:
    push ax
    push bx
    mov ah, 0x0E
    xor bh, bh
    int 0x10
    pop bx
    pop ax
    syscall_iret

; ─── SYS_READ_KEY (AH=0x03) ──────────────────────────────────────────────────
; Input:  none
; Output: AH = scan code, AL = ASCII character
; ──────────────────────────────────────────────────────────────────────────────
.fn_read_key:
    xor ah, ah
    int 0x16                        ; BIOS keyboard: wait for keypress
    syscall_iret

; ─── SYS_PEEK_KEY (AH=0x29) ──────────────────────────────────────────────────
; Non-blocking key check via BIOS INT 0x16 AH=01h.  Does NOT consume the key.
;
; Input:  none
; Output: AL = ASCII (0 if no key), AH = scancode (0 if no key),
;         ZF = 1 if no key, ZF = 0 if a key is buffered.
;
; This handler must preserve ZF across the syscall return path.  The standard
; syscall_iret would let IRET restore the caller's saved FLAGS (clobbering
; our ZF), and syscall_ret_cf's `dec [BIB_INT_DEPTH]` in debug builds also
; clobbers ZF.  We use a custom return that captures FLAGS before any
; cleanup and restores them just before retf 2.
; ──────────────────────────────────────────────────────────────────────────────
.fn_peek_key:
    mov ah, 0x01
    int 0x16                        ; ZF=1 if no key, AX = scancode:ASCII otherwise
    jnz .pk_have_key
    xor ax, ax                      ; No key: clear AX, sets ZF=1
.pk_have_key:
    ; AX and ZF now correctly reflect the result.  Save FLAGS so debug
    ; cleanup (which uses `dec`, clobbering ZF) cannot disturb them.
    pushf
%ifdef DEBUG
    dec byte [cs:BIB_INT_DEPTH]
%endif
    popf                            ; restore our ZF/CF
    sti                             ; sti modifies only IF, leaves ZF/CF intact
    retf 2                          ; preserve current FLAGS (per syscall_ret_cf pattern)

; ─── SYS_READ_SECTOR (AH=0x04) ───────────────────────────────────────────────
; Input:  EDI = absolute LBA sector number
;         ES:BX = buffer to read into
;         CL = number of sectors to read
; Output: CF clear = success, CF set = error
;
; NOTE: LBA is passed in EDI (not EAX) because AH carries the syscall
; function number, and AH is bits 8-15 of EAX — they would collide.
; ──────────────────────────────────────────────────────────────────────────────
.fn_read_sector:
    push si
    push dx

    ; Set up the kernel's DAP
    mov [dap_lba], edi
    xor ch, ch
    mov [dap_sectors], cx
    mov [dap_buffer], bx
    mov [dap_buffer+2], es

%ifdef DEBUG
    ; --- DAP dump: show full 16-byte DAP contents before INT 0x13 ---
    push ax
    push cx
    push si
    mov si, .sc_dap_pfx             ; "[SYS] DAP: "
    call serial_puts
    ; Print 16 bytes as hex: size(2) sectors(2) buf_off(2) buf_seg(2) lba(8)
    mov si, dap
    mov cx, 16
.sc_dap_loop:
    lodsb
    call serial_hex8
    mov al, ' '
    call serial_putc
    loop .sc_dap_loop
    call serial_crlf
    pop si
    pop cx
    pop ax
%endif

    mov dl, [BIB_DRIVE]
    mov si, dap
    mov ah, 0x42
    sti                             ; BIOS INT 0x13 needs interrupts for DMA
    int 0x13

%ifdef DEBUG
    ; --- EXIT trace: show return CF, IF, and BIOS error code ------------------
    pushf                           ; Save FLAGS (CF from INT 0x13)
    push ax                         ; Save AX (AH = BIOS error code if CF=1)
    push si

    mov si, .sc_exit_pfx            ; "[SYS] READ_SECTOR -> "
    call serial_puts

    ; Print CF state
    mov si, .sc_exit_cf
    call serial_puts
    push bp
    mov bp, sp
    test byte [bp+6], 0x01          ; FLAGS at BP+6 (BP, SI, AX, FLAGS)
    pop bp
    jz .sc_exit_cf0
    mov al, '1'
    jmp .sc_exit_cf_print
.sc_exit_cf0:
    mov al, '0'
.sc_exit_cf_print:
    call serial_putc

    ; Print IF state
    mov si, .sc_exit_if
    call serial_puts
    push bp
    mov bp, sp
    test byte [bp+7], 0x02          ; IF is bit 9 of FLAGS = bit 1 of high byte
    pop bp
    jz .sc_exit_if0
    mov al, '1'
    jmp .sc_exit_if_print
.sc_exit_if0:
    mov al, '0'
.sc_exit_if_print:
    call serial_putc

    ; Print BIOS error code (AH after INT 0x13)
    mov si, .sc_exit_err
    call serial_puts
    push bp
    mov bp, sp
    mov ax, [bp+4]                  ; AX saved on stack: BP, SI, AX, FLAGS
    pop bp
    mov al, ah                      ; AH = BIOS error code
    call serial_hex8

    ; Print nesting depth
    mov si, .sc_trace_depth
    call serial_puts
    mov al, [cs:BIB_INT_DEPTH]
    call serial_hex8

    call serial_crlf
    pop si
    pop ax
    popf                            ; Restore FLAGS (CF intact)
%endif

    pop dx
    pop si
    syscall_ret_cf                  ; Propagate CF from INT 0x13 to caller

; ─── SYS_GET_VERSION (AH=0x05) ───────────────────────────────────────────────
; Input:  none
; Output: AH = major version, AL = minor version
; ──────────────────────────────────────────────────────────────────────────────
.fn_get_version:
    mov ax, OS_VERSION                  ; AH=major, AL=minor
    syscall_iret

; ─── SYS_CLEAR_SCREEN (AH=0x06) ──────────────────────────────────────────────
; Input:  none
; Output: none (clears screen, sets mode 3)
; ──────────────────────────────────────────────────────────────────────────────
.fn_clear_screen:
    push ax
    mov ax, 0x0003
    int 0x10
    pop ax
    syscall_iret

; ─── SYS_SET_CURSOR (AH=0x07) ────────────────────────────────────────────────
; Input:  DH = row, DL = column
; Output: none
; ──────────────────────────────────────────────────────────────────────────────
.fn_set_cursor:
    push ax
    push bx
    mov ah, 0x02
    xor bh, bh
    int 0x10
    pop bx
    pop ax
    syscall_iret

; ─── SYS_GET_CURSOR (AH=0x08) ────────────────────────────────────────────────
; Input:  none
; Output: DH = row, DL = column
; ──────────────────────────────────────────────────────────────────────────────
.fn_get_cursor:
    push ax
    push bx
    mov ah, 0x03
    xor bh, bh
    int 0x10                        ; Returns DH=row, DL=col
    pop bx
    pop ax
    syscall_iret

; ─── SYS_CHECK_A20 (AH=0x09) ─────────────────────────────────────────────────
; Input:  none
; Output: AL = 1 if A20 enabled, 0 if disabled
; ──────────────────────────────────────────────────────────────────────────────
.fn_check_a20:
    push ds
    push es
    push cx

    xor ax, ax
    mov ds, ax
    mov ax, 0xFFFF
    mov es, ax

    mov al, [ds:0x0500]
    push ax
    mov al, [es:0x0510]
    push ax

    mov byte [es:0x0510], 0x13
    mov byte [ds:0x0500], 0x37

    cmp byte [es:0x0510], 0x37
    je .a20_off
    mov cl, 1
    jmp .a20_restore
.a20_off:
    xor cl, cl

.a20_restore:
    pop ax
    mov [es:0x0510], al
    pop ax
    mov [ds:0x0500], al

    pop cx
    pop es
    pop ds
    mov al, cl
    syscall_iret

; ─── SYS_GET_CONV_MEM (AH=0x0A) ──────────────────────────────────────────────
; Input:  none
; Output: AX = conventional memory in KB
; ──────────────────────────────────────────────────────────────────────────────
.fn_get_conv_mem:
    int 0x12                        ; AX = KB of conventional memory
    syscall_iret

; ─── SYS_GET_EXT_MEM (AH=0x0B) ───────────────────────────────────────────────
; Input:  none
; Output: AX = extended memory in KB, CF set on error
; ──────────────────────────────────────────────────────────────────────────────
.fn_get_ext_mem:
    mov ah, 0x88
    int 0x15                        ; AX = KB, CF on error
    syscall_ret_cf

; ─── SYS_GET_E820 (AH=0x0C) ──────────────────────────────────────────────────
; Input:  EBX = continuation value (0 to start), ES:DI = 20-byte buffer
; Output: EBX = next continuation (0 = done), CF set on error
;         Buffer filled with one E820 entry
; ──────────────────────────────────────────────────────────────────────────────
.fn_get_e820:
    push edx
    mov eax, 0x0000E820
    mov ecx, 20
    mov edx, 0x534D4150             ; 'SMAP'
    int 0x15
    ; Check signature — if EAX != 'SMAP', set CF
    cmp eax, 0x534D4150
    je .e820_ok
    stc
.e820_ok:
    pop edx
    syscall_ret_cf

; ─── SYS_REBOOT (AH=0x0D) ────────────────────────────────────────────────────
; Input:  none
; Output: does not return (warm reboot)
; ──────────────────────────────────────────────────────────────────────────────
.fn_reboot:
    mov word [0x0472], 0x1234       ; Warm-reboot flag
    jmp 0xFFFF:0x0000               ; BIOS reset vector

; ─── SYS_GET_DRIVE_INFO (AH=0x0E) ────────────────────────────────────────────
; Input:  none (uses boot drive from BIB)
; Output: CH=cyl_low, CL=sec|cyl_hi, DH=heads, DL=drives, CF on error
; ──────────────────────────────────────────────────────────────────────────────
.fn_get_drive_info:
    mov dl, [BIB_DRIVE]
    mov ah, 0x08
    int 0x13
    syscall_ret_cf

; ─── SYS_GET_BIB (AH=0x0F) ───────────────────────────────────────────────────
; Input:  none
; Output: BX = BIB offset (0x0600), ES = 0x0000
; ──────────────────────────────────────────────────────────────────────────────
.fn_get_bib:
    xor ax, ax
    mov es, ax
    mov bx, 0x0600
    syscall_iret

; ─── SYS_PRINT_HEX8 (AH=0x10) ───────────────────────────────────────────────
; Input:  AL = byte to print as two hex digits
; Output: none
; ──────────────────────────────────────────────────────────────────────────────
.fn_print_hex8:
    push ax
    push bx
    ; High nibble
    push ax
    shr al, 4
    call .hex_nibble
    pop ax
    ; Low nibble
    and al, 0x0F
    call .hex_nibble
    pop bx
    pop ax
    syscall_iret

.hex_nibble:
    add al, '0'
    cmp al, '9'
    jbe .hex_print
    add al, 7
.hex_print:
    mov ah, 0x0E
    xor bh, bh
    int 0x10
    ret

; ─── SYS_PRINT_HEX16 (AH=0x11) ──────────────────────────────────────────────
; Input:  DX = word to print as four hex digits
; Output: none
;
; NOTE: Value passed in DX (not AX) because AH carries the syscall number.
; ──────────────────────────────────────────────────────────────────────────────
.fn_print_hex16:
    push ax
    push bx
    push dx
    mov al, dh
    ; High byte, high nibble
    push ax
    shr al, 4
    call .hex_nibble
    pop ax
    and al, 0x0F
    call .hex_nibble
    ; Low byte
    mov al, dl
    push ax
    shr al, 4
    call .hex_nibble
    pop ax
    and al, 0x0F
    call .hex_nibble
    pop dx
    pop bx
    pop ax
    syscall_iret

; ─── SYS_PRINT_DEC16 (AH=0x12) ──────────────────────────────────────────────
; Input:  DX = word to print as unsigned decimal
; Output: none
;
; NOTE: Value passed in DX (not AX) because AH carries the syscall number.
; ──────────────────────────────────────────────────────────────────────────────
.fn_print_dec16:
    push ax
    push bx
    push cx
    push dx
    mov ax, dx                      ; Work in AX for division
    xor cx, cx

.dec_div:
    xor dx, dx
    mov bx, 10
    div bx
    push dx
    inc cx
    test ax, ax
    jnz .dec_div

.dec_print:
    pop ax
    add al, '0'
    mov ah, 0x0E
    xor bh, bh
    int 0x10
    dec cx
    jnz .dec_print

    pop dx
    pop cx
    pop bx
    pop ax
    syscall_iret

; ─── SYS_WAIT_KEY (AH=0x13) ──────────────────────────────────────────────────
; Input:  none
; Output: none (prints prompt, waits, clears screen)
; ──────────────────────────────────────────────────────────────────────────────
.fn_wait_key:
    push ax
    push si
    push bx
    mov si, msg_anykey
    call puts
    xor ah, ah
    int 0x16                        ; Wait for keypress
    mov ax, 0x0003                  ; Clear screen
    int 0x10
    pop bx
    pop si
    pop ax
    syscall_iret

; ─── SYS_GET_EQUIP (AH=0x14) ─────────────────────────────────────────────────
; Input:  none
; Output: AX = equipment word
; ──────────────────────────────────────────────────────────────────────────────
.fn_get_equip:
    int 0x11
    syscall_iret

; ─── SYS_GET_VIDEO (AH=0x15) ─────────────────────────────────────────────────
; Input:  none
; Output: AL = current video mode, AH = columns, BH = active page
; ──────────────────────────────────────────────────────────────────────────────
.fn_get_video:
    mov ah, 0x0F
    int 0x10
    syscall_iret

; ─── SYS_GET_BDA_BYTE (AH=0x16) ──────────────────────────────────────────────
; Input:  BX = offset within BDA (0x0400–0x04FF)
; Output: AL = byte at that BDA offset
; ──────────────────────────────────────────────────────────────────────────────
.fn_get_bda_byte:
    push ds
    push ax
    xor ax, ax
    mov ds, ax
    pop ax
    mov al, [ds:bx]
    pop ds
    syscall_iret

; ─── SYS_GET_BDA_WORD (AH=0x17) ──────────────────────────────────────────────
; Input:  BX = offset within BDA (0x0400–0x04FF)
; Output: AX = word at that BDA offset
; ──────────────────────────────────────────────────────────────────────────────
.fn_get_bda_word:
    push ds
    xor ax, ax
    mov ds, ax
    mov ax, [ds:bx]
    pop ds
    syscall_iret

; ─── SYS_CPUID (AH=0x18) ─────────────────────────────────────────────────────
; Input:  EDI = CPUID leaf number (EAX is used by the dispatcher for AH,
;         so the caller passes the leaf in EDI to avoid the conflict)
; Output: EAX, EBX, ECX, EDX = CPUID result
; ──────────────────────────────────────────────────────────────────────────────
.fn_cpuid:
    mov eax, edi                    ; Move leaf from EDI into EAX
    cpuid
    syscall_iret

; ─── SYS_CHECK_CPUID (AH=0x19) ───────────────────────────────────────────────
; Input:  none
; Output: AL = 1 if CPUID supported, 0 if not
; ──────────────────────────────────────────────────────────────────────────────
.fn_check_cpuid:
    pushfd
    pop eax
    mov ecx, eax
    xor eax, 0x00200000             ; Flip ID bit (bit 21)
    push eax
    popfd
    pushfd
    pop eax
    push ecx
    popfd                           ; Restore original EFLAGS
    xor eax, ecx
    test eax, 0x00200000
    jz .no_cpuid_support
    mov al, 1
    syscall_iret
.no_cpuid_support:
    xor al, al
    syscall_iret

; ─── SYS_GET_EDD (AH=0x1A) ───────────────────────────────────────────────────
; Input:  DL = drive number
; Output: BX = 0xAA55 if EDD supported, AH=version, CX=interface support
;         CF set if not supported
; ──────────────────────────────────────────────────────────────────────────────
.fn_get_edd:
    mov bx, 0x55AA
    mov ah, 0x41
    int 0x13
    syscall_ret_cf

; ─── SYS_GET_IVT (AH=0x1B) ───────────────────────────────────────────────────
; Input:  CL = vector number (0x00–0xFF)
; Output: AX = offset, DX = segment of the IVT entry
; ──────────────────────────────────────────────────────────────────────────────
.fn_get_ivt:
    push ds
    push bx
    xor ax, ax
    mov ds, ax
    xor bh, bh
    mov bl, cl
    shl bx, 2                      ; BX = vector * 4
    mov ax, [ds:bx]                 ; Offset
    mov dx, [ds:bx+2]              ; Segment
    pop bx
    pop ds
    syscall_iret

; ─── SYS_DBG_PRINT (AH=0x20) ──────────────────────────────────────────────────
; Input:  DS:SI = NUL-terminated message string
;         DS:BX = NUL-terminated tag string (e.g., "SHL"); BX=0 → default "USR"
; Output: none
; Note:   No-op in release builds.  In debug builds, outputs:
;         [TAG] message\r\n
; ──────────────────────────────────────────────────────────────────────────────
.fn_dbg_print:
%ifdef DEBUG
    push si
    push ax
    call .dbg_emit_tag              ; print "[TAG] "
    pop ax
    pop si
    push si
    push ax
    call serial_puts                ; print the message
    call serial_crlf
    pop ax
    pop si
%endif
    syscall_iret

; ─── SYS_DBG_HEX16 (AH=0x21) ─────────────────────────────────────────────────
; Input:  DX = 16-bit value to display
;         DS:BX = NUL-terminated tag string; BX=0 → default "USR"
; Output: none
; Note:   No-op in release builds.  In debug builds, outputs:
;         [TAG] 0xNNNN\r\n
; ──────────────────────────────────────────────────────────────────────────────
.fn_dbg_hex16:
%ifdef DEBUG
    push ax
    push si
    call .dbg_emit_tag              ; print "[TAG] "
    mov ax, dx
    call serial_hex16               ; print DX as hex
    call serial_crlf
    pop si
    pop ax
%endif
    syscall_iret

; ─── SYS_DBG_REGS (AH=0x22) ──────────────────────────────────────────────────
; Input:  DS:BX = NUL-terminated tag string; BX=0 → default "USR"
;         All other registers are the values to dump.
; Output: none
; Note:   No-op in release builds.  In debug builds, outputs:
;         [TAG] AX=xxxx BX=xxxx CX=xxxx DX=xxxx SI=xxxx DI=xxxx\r\n
;         AX will show AH=0x22 (the syscall number is part of AX).
;         BX shows the original value (saved by dispatcher in .sc_temp).
; ──────────────────────────────────────────────────────────────────────────────
.fn_dbg_regs:
%ifdef DEBUG
    push ax
    push si
    ; Save values we want to print before we clobber them
    mov [cs:.dbg_save_cx], cx
    mov [cs:.dbg_save_dx], dx
    mov [cs:.dbg_save_si], si
    mov [cs:.dbg_save_di], di

    call .dbg_emit_tag              ; print "[TAG] "

    mov si, .dbg_lbl_ax
    call serial_puts
    pop si                          ; pop saved SI
    pop ax                          ; pop saved AX (caller's AX with AH=0x22)
    push ax
    push si
    call serial_hex16

    mov si, .dbg_lbl_bx
    call serial_puts
    mov ax, [cs:.sc_temp]           ; original BX (saved by dispatcher)
    call serial_hex16

    mov si, .dbg_lbl_cx
    call serial_puts
    mov ax, [cs:.dbg_save_cx]
    call serial_hex16

    mov si, .dbg_lbl_dx
    call serial_puts
    mov ax, [cs:.dbg_save_dx]
    call serial_hex16

    mov si, .dbg_lbl_si
    call serial_puts
    mov ax, [cs:.dbg_save_si]
    call serial_hex16

    mov si, .dbg_lbl_di
    call serial_puts
    mov ax, [cs:.dbg_save_di]
    call serial_hex16

    call serial_crlf
    pop si
    pop ax
%endif
    syscall_iret

%ifdef DEBUG
; ─── .dbg_emit_tag — print "[TAG] " to serial ────────────────────────────────
; Input:  DS:BX = tag string (NUL-terminated); BX=0 → use "USR"
; Clobbers: SI (caller must save)
; ──────────────────────────────────────────────────────────────────────────────
.dbg_emit_tag:
    push ax
    mov al, '['
    call serial_putc
    test bx, bx
    jz .dbg_default_tag
    mov si, bx
    call serial_puts
    jmp .dbg_tag_close
.dbg_default_tag:
    mov si, .dbg_tag_usr
    call serial_puts
.dbg_tag_close:
    mov al, ']'
    call serial_putc
    mov al, ' '
    call serial_putc
    pop ax
    ret

.dbg_tag_usr:  db 'USR', 0
.dbg_lbl_ax:   db 'AX=', 0
.dbg_lbl_bx:   db ' BX=', 0
.dbg_lbl_cx:   db ' CX=', 0
.dbg_lbl_dx:   db ' DX=', 0
.dbg_lbl_si:   db ' SI=', 0
.dbg_lbl_di:   db ' DI=', 0
.dbg_save_cx:  dw 0
.dbg_save_dx:  dw 0
.dbg_save_si:  dw 0
.dbg_save_di:  dw 0
%endif

; ─── SYS_EXIT (AH=0x23) ──────────────────────────────────────────────────────
; Terminate the running user program and return to the shell.
;
; This syscall does NOT return to the caller.  It abandons the current INT
; stack frame, restores the shell's stack pointer (which points at the return
; address from the shell's `call` instruction), and does a near `ret` back
; into the shell.
;
; Input:  AH = 0x23, AL = exit code (reserved for future use, ignored)
; Output: Does not return — control transfers to shell
;
; Implementation notes:
;   - Restores DS=ES=SS=0 for segment safety (a misbehaving program may
;     have changed them)
;   - Reads SHELL_SAVED_SP from the fixed shared address (0x7FFE)
;   - The shell stores SP-2 there before calling the program, so the
;     value points directly at the return address on the stack
;   - After `ret`, the shell resumes at the instruction after `call`
; ──────────────────────────────────────────────────────────────────────────────
.fn_exit:
    cli                             ; Disable interrupts during stack switch
    xor ax, ax
    mov ds, ax                      ; Restore DS = 0 (flat model)
    mov es, ax                      ; Restore ES = 0
    mov ss, ax                      ; Restore SS = 0

    ; Check if there's a spawn parent to reload
%ifdef DEBUG
    ; Diagnostic: print spawn_depth to confirm state
    push ax
    push si
    mov si, .spawn_diag_pfx
    call serial_puts
    mov al, [spawn_depth]
    call serial_hex8
    call serial_crlf
    pop si
    pop ax
%endif
    cmp byte [spawn_depth], 0
    jne .exit_reload_parent

    DBG "EXIT: spawn_depth=0, ret to shell"
    mov sp, [SHELL_SAVED_SP]        ; SP → return address from shell's call
    sti                             ; Re-enable interrupts
    ret                             ; Near return to shell

.exit_reload_parent:
    ; A parent program was saved by SYS_SPAWN — reload it from disk.
    ; Decrement depth first, then load parent at [depth-1] (current index).
    dec byte [spawn_depth]

    DBG "SPAWN: reload_parent entry"
    mov sp, [SHELL_SAVED_SP]        ; Use shell's stack for safety
    sti

    DBG "SPAWN: SP restored, calling FS_FIND"

    ; Verify INT 0x81 vector is sane (debug: print IVT entry)
%ifdef DEBUG
    push ax
    push si
    mov si, .spawn_ivt_pfx
    call serial_puts
    mov ax, [0x0204]               ; INT 0x81 IP
    call serial_hex16
    mov al, ':'
    call serial_putc
    mov ax, [0x0206]               ; INT 0x81 CS
    call serial_hex16
    call serial_crlf
    pop si
    pop ax
%endif

    ; Compute address of parent filename: spawn_parent_stack + depth*11
    ; (spawn_depth was already decremented, so it's the index of the parent to load)
    push dx
    movzx ax, byte [spawn_depth]
    mov dx, 11
    mul dx                          ; AX = spawn_depth * 11
    add ax, spawn_parent_stack
    mov [.exit_parent_si], ax       ; Stash computed address
    pop dx

    ; Find parent file
    mov si, [.exit_parent_si]
    mov ah, FS_FIND_FILE
    int 0x81
    jc .exit_spawn_fail             ; Parent not found — fall through to shell

    ; CX = sectors, BL = attrs
    ; Load parent to TPA
    mov si, [.exit_parent_si]
    push ds
    pop es
    mov bx, USER_PROG_BASE
    mov cx, USER_PROG_MAX_SEC
    mov ah, FS_READ_FILE
    int 0x81
    jc .exit_spawn_fail

    ; Validate MNEX magic
    cmp dword [USER_PROG_BASE], 'MNEX'
    jne .exit_spawn_fail

    ; Apply v2 relocations
    mov di, USER_PROG_BASE
    test word [di + 6], MNEX_V2_FLAG_RELOC
    jz .exit_spawn_v1

    mov ax, [di + 8]               ; reloc_count
    shl ax, 1
    add ax, MNEX_V2_HDR_BASE
    cmp ax, [di + 10]
    jne .exit_spawn_v1

    mov cx, [di + 8]
    jcxz .exit_spawn_v1
    push si
    lea si, [di + MNEX_V2_HDR_BASE]
.exit_spawn_patch:
    lodsw
    mov bx, di
    add bx, ax
    add word [bx], di
    loop .exit_spawn_patch
    pop si

    ; v2 entry
    mov ax, [di + 10]
    add ax, di
    jmp .exit_spawn_jump

.exit_spawn_v1:
    mov ax, USER_PROG_BASE + MNEX_HDR_SIZE

.exit_spawn_jump:
    mov [exec_entry_addr], ax

    ; If depth is now 0, this is the outermost parent — restore shell ret addr
    ; and clear the trampoline.  Otherwise keep trampoline active for next exit.
    cmp byte [spawn_depth], 0
    jne .exit_spawn_still_nested

    ; Outermost: restore original shell return address on stack
    mov bx, [SHELL_SAVED_SP]
    mov ax, [spawn_saved_ret]
    mov [bx], ax
    mov word [spawn_saved_ret], 0   ; Mark spawn chain as fully resolved
    DBG "SPAWN: outermost, shell ret restored"
    jmp .exit_spawn_launch

.exit_spawn_still_nested:
    ; Still nested: re-install trampoline at [SHELL_SAVED_SP].
    ; The previous trampoline's `int 0x80` pushed FLAGS onto [SHELL_SAVED_SP],
    ; destroying the trampoline address. We must restore it for the next unwind.
    mov bx, [SHELL_SAVED_SP]
    mov word [bx], .spawn_trampoline
    DBG "SPAWN: trampoline re-installed"

.exit_spawn_launch:
    ; Clear args for the restarted parent
    mov word [SHELL_ARGS_PTR], 0
    mov byte [ARGV_ARGC], 0

    ; Jump to parent entry — same mechanism as .fn_exec:
    ; SHELL_SAVED_SP still points to the shell's return frame.
    cli
    mov sp, [SHELL_SAVED_SP]
    sti
    cld
    jmp [exec_entry_addr]

.exit_parent_si: dw 0               ; Temp storage for parent filename address

.exit_spawn_fail:
    ; Could not reload parent — clear all spawn state, return to shell
    mov byte [spawn_depth], 0
    ; Restore the original shell return address before returning
    mov bx, [SHELL_SAVED_SP]
    mov ax, [spawn_saved_ret]
    test ax, ax
    jz .exit_spawn_fail_ret         ; No saved ret (shouldn't happen, but safe)
    mov [bx], ax
.exit_spawn_fail_ret:
    mov word [spawn_saved_ret], 0
    mov sp, [SHELL_SAVED_SP]
    ret                             ; Near return to shell

; ─── SYS_GET_ARGS (AH=0x24) ──────────────────────────────────────────────────
; Return a pointer to the command-line arguments passed to the current program.
;
; The shell stores a pointer to the argument string (everything after the
; filename in `run PROG.MNX args...`) at the fixed address SHELL_ARGS_PTR
; before launching the program.
;
; Input:  AH = 0x24
; Output: SI = pointer to NUL-terminated argument string (DS:SI, DS=0)
;         CX = length of argument string (0 if no args)
;         CF = clear (always succeeds)
; ──────────────────────────────────────────────────────────────────────────────
.fn_get_args:
    push ds
    xor ax, ax
    mov ds, ax
    mov si, [SHELL_ARGS_PTR]        ; SI → args string (or 0 if none)
    ; Calculate length (count until NUL)
    xor cx, cx
    test si, si
    jz .ga_done
    push si
.ga_len:
    cmp byte [si], 0
    je .ga_len_done
    inc si
    inc cx
    jmp .ga_len
.ga_len_done:
    pop si                          ; Restore SI to start of string
.ga_done:
    pop ds
    clc
    syscall_ret_cf

; ─── SYS_GET_ARGC (AH=0x25) ──────────────────────────────────────────────────
; Return the number of parsed arguments for the current program.
;
; The shell writes the argument count to ARGV_ARGC (0x7F00) before launching.
;
; Input:  AH = 0x25
; Output: CL = argument count (0–15)
;         CF = clear (always succeeds)
; ──────────────────────────────────────────────────────────────────────────────
.fn_get_argc:
    push ds
    xor ax, ax
    mov ds, ax
    mov cl, [ARGV_ARGC]
    pop ds
    clc
    syscall_ret_cf

; ─── SYS_GET_ARGV (AH=0x26) ──────────────────────────────────────────────────
; Return a pointer to the Nth argument string.
;
; Input:  AH = 0x26
;         CL = argument index (0-based)
; Output: SI = pointer to NUL-terminated argument string (segment 0)
;         CX = length of argument string
;         CF = clear on success, set if index >= argc
; ──────────────────────────────────────────────────────────────────────────────
.fn_get_argv:
    push ds
    xor ax, ax
    mov ds, ax
    ; Validate index
    cmp cl, [ARGV_ARGC]
    jae .argv_bad_index
    ; Get pointer from argv table: ARGV_PTRS + index*2
    movzx bx, cl
    shl bx, 1
    add bx, ARGV_PTRS
    mov si, [bx]
    ; Calculate length
    xor cx, cx
    push si
.argv_len:
    cmp byte [si], 0
    je .argv_len_done
    inc si
    inc cx
    jmp .argv_len
.argv_len_done:
    pop si
    pop ds
    mov bx, [cs:.sc_temp]           ; Restore original BX
    clc
    syscall_ret_cf

.argv_bad_index:
    pop ds
    mov bx, [cs:.sc_temp]           ; Restore original BX
    stc
    syscall_ret_cf

; ─── SYS_EXEC (AH=0x27) ──────────────────────────────────────────────────────
; Replace the current program with a new one (overlay exec).
;
; Loads the named .MNX file into the TPA, applies v2 relocations, sets up
; arguments, and jumps to the new program's entry point.  The calling program
; is destroyed.  The new program returns to the shell (not the caller).
;
; Input:  AH = 0x27
;         DS:SI = pointer to 11-byte filename (8.3 uppercase padded)
;         DS:DI = pointer to NUL-terminated argument string (0 = no args)
; Output: Does not return on success
;         CF set on failure, AX = error code:
;           1 = file not found
;           2 = file is system / not executable
;           3 = file too large
;           4 = read error
;           5 = bad MNEX header
;
; On failure, returns to caller (TPA not yet destroyed).
; All validation happens BEFORE loading, so the caller survives errors.
; ──────────────────────────────────────────────────────────────────────────────
.fn_exec:
    ; Save original BX (clobbered by dispatch)
    mov bx, [cs:.sc_temp]

    ; --- Copy filename (11 bytes) to kernel scratch ---------------------------
    ; SI may point into the caller's TPA, which will be overwritten.
    push di
    push cx
    push es
    xor ax, ax
    mov es, ax                      ; ES = 0 (flat)
    mov di, exec_fname_buf          ; Destination in kernel data
    mov cx, 11
    rep movsb                       ; Copy 11-byte filename
    pop es
    pop cx
    pop di

    ; --- Copy args string to kernel scratch -----------------------------------
    ; DI = pointer to args string (or 0 for no args)
    mov word [exec_args_len], 0
    test di, di
    jz .exec_no_args
    cmp byte [di], 0
    je .exec_no_args

    ; Copy up to 127 bytes of args to exec_args_buf
    push si
    push cx
    mov si, di                      ; Source = caller's args pointer
    mov di, exec_args_buf           ; Dest = kernel scratch
    xor cx, cx
.exec_copy_args:
    lodsb
    stosb
    inc cx
    test al, al                     ; Hit NUL?
    jz .exec_args_copied
    cmp cx, 127                     ; Max 127 chars + NUL
    jb .exec_copy_args
    mov byte [di], 0                ; Force NUL-terminate
    inc cx
.exec_args_copied:
    mov [exec_args_len], cx
    pop cx
    pop si
    jmp .exec_find_file

.exec_no_args:
    mov byte [exec_args_buf], 0     ; Empty string

.exec_find_file:
    ; --- Find the file via INT 0x81 -------------------------------------------
    mov si, exec_fname_buf          ; DS:SI = 11-byte filename in kernel space
    mov ah, FS_FIND_FILE
    int 0x81
    jc .exec_err_not_found          ; CF set = not found

    ; CX = sector count, BL = attributes, EAX = start sector (from FS)
    ; --- Validate attributes --------------------------------------------------
    test bl, MNFS_ATTR_SYSTEM
    jnz .exec_err_not_exec
    test bl, MNFS_ATTR_EXEC
    jz .exec_err_not_exec

    ; --- Validate size --------------------------------------------------------
    cmp cx, USER_PROG_MAX_SEC
    ja .exec_err_too_large

    ; --- Load file to TPA (point of no return for caller) ---------------------
    ; After this, the caller is destroyed.  All data we need is in kernel space.
    mov si, exec_fname_buf
    push ds
    pop es                          ; ES = DS = 0 (flat)
    mov bx, USER_PROG_BASE          ; ES:BX = load address
    mov cx, USER_PROG_MAX_SEC
    mov ah, FS_READ_FILE
    int 0x81
    jc .exec_err_load               ; Load failed (TPA may be corrupted)

    ; --- Validate MNEX magic --------------------------------------------------
    cmp dword [USER_PROG_BASE], 'MNEX'
    jne .exec_err_bad_header

    ; --- Apply v2 relocations -------------------------------------------------
    mov di, USER_PROG_BASE
    test word [di + 6], MNEX_V2_FLAG_RELOC
    jz .exec_v1_entry

    ; Secondary validation: entry_offset == 12 + reloc_count * 2
    mov ax, [di + 8]               ; reloc_count
    shl ax, 1                      ; * 2
    add ax, MNEX_V2_HDR_BASE       ; + 12
    cmp ax, [di + 10]              ; Compare with entry_offset
    jne .exec_v1_entry             ; Mismatch → v1 fallback

    ; Patch relocation table
    mov cx, [di + 8]               ; reloc_count
    jcxz .exec_v1_entry            ; Zero relocs → skip
    push si
    lea si, [di + MNEX_V2_HDR_BASE] ; SI = relocation table

.exec_patch_loop:
    lodsw                          ; AX = file-relative offset
    mov bx, di
    add bx, ax                     ; BX = absolute address of word
    add word [bx], di              ; Add load_base
    loop .exec_patch_loop
    pop si

    ; Entry point from v2 header
    mov ax, [di + 10]              ; entry_offset
    add ax, di                     ; Absolute entry address
    mov [exec_entry_addr], ax
    jmp .exec_setup_args

.exec_v1_entry:
    ; Legacy v1: entry at offset 6
    mov word [exec_entry_addr], USER_PROG_BASE + MNEX_HDR_SIZE

.exec_setup_args:
    ; --- Set up args for new program ------------------------------------------
    ; Store args pointer at the shared ABI location
    mov word [SHELL_ARGS_PTR], exec_args_buf

    ; Parse args into argc/argv table (kernel-local tokenizer)
    ; Reads from exec_args_buf, populates ARGV_ARGC / ARGV_PTRS / ARGV_STORAGE
    call exec_parse_args

    ; --- Jump to new program --------------------------------------------------
    ; The new program should return to the shell, just as if the shell called it.
    ; SHELL_SAVED_SP was set by the shell before launching the FIRST program.
    ; It points to the stack location where the shell's return address sits.
    ; We restore that SP so the new program's `ret` returns to shell.
    DBG "EXEC: launching new program"

    ; Clear spawn_pending — exec succeeded, spawn state is committed
    mov byte [spawn_pending], 0

    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, [SHELL_SAVED_SP]        ; SP → shell's return address on stack
    sti
    cld                             ; Clear direction flag (ABI requirement)

    ; Jump to the new program's entry (not call — shell return addr is on stack)
    jmp [exec_entry_addr]

    ; --- Error paths (return to caller with CF set, AX = error code) ----------
    ; These only execute BEFORE the file is loaded, so the caller is intact.
    ; If called via SYS_SPAWN, undo the spawn state before returning.
.exec_err_not_found:
    call .spawn_rollback_if_pending
    mov ax, 1
    stc
    syscall_ret_cf

.exec_err_not_exec:
    call .spawn_rollback_if_pending
    mov ax, 2
    stc
    syscall_ret_cf

.exec_err_too_large:
    call .spawn_rollback_if_pending
    mov ax, 3
    stc
    syscall_ret_cf

    ; Post-load errors — TPA is destroyed, cannot return to caller.
    ; Fall through to shell via SYS_EXIT-style return.
.exec_err_load:
    mov si, .exec_msg_load_err
    jmp .exec_fatal_to_shell

.exec_err_bad_header:
    mov si, .exec_msg_bad_hdr
    jmp .exec_fatal_to_shell

.exec_fatal_to_shell:
    ; Print error message and return to shell (caller is dead)
    mov ah, SYS_PRINT_STRING
    int 0x80
    ; If called via spawn, fully reset spawn state
    cmp byte [spawn_pending], 0
    je .exec_fatal_no_spawn
    mov byte [spawn_pending], 0
    mov byte [spawn_depth], 0
    ; Restore original shell return address if we saved one
    cmp word [spawn_saved_ret], 0
    je .exec_fatal_no_spawn
    push bx
    mov bx, [SHELL_SAVED_SP]
    mov ax, [spawn_saved_ret]
    mov [bx], ax                    ; Restore shell's real return address
    mov word [spawn_saved_ret], 0
    pop bx
.exec_fatal_no_spawn:
    ; Return to shell via stack restore
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, [SHELL_SAVED_SP]
    sti
    ret                             ; Returns to shell

.exec_msg_load_err: db 13, 10, 'EXEC: read error', 13, 10, 0
.exec_msg_bad_hdr:  db 13, 10, 'EXEC: bad MNEX header', 13, 10, 0

; ─── SYS_SPAWN (AH=0x28) ─────────────────────────────────────────────────────
; Spawn a child program, then reload the caller when the child exits.
;
; Like SYS_EXEC but the kernel saves the caller's filename.  When the child
; exits (ret or SYS_EXIT), the kernel reloads the caller from disk and
; restarts it fresh (no state preserved).  The caller appears to "return"
; from the spawn call, but it's actually a fresh load.
;
; Input:  AH = 0x28
;         DS:SI = child's 11-byte filename (8.3 padded)
;         DS:DI = child's argument string (or 0)
;         DS:BX = caller's own 11-byte filename (for reload after child exits)
; Output: Does not return on success (child takes over)
;         CF set on failure, AX = error code (same as SYS_EXEC)
; ──────────────────────────────────────────────────────────────────────────────
.fn_spawn:
    ; Push caller's filename onto the spawn parent stack.
    ; At handler entry, BX = original caller value (dispatch restored it).

    ; Check depth limit
    cmp byte [spawn_depth], SPAWN_MAX_DEPTH
    jb .fn_spawn_push
    ; Stack full — fail with error
    mov ax, 4                       ; Error: spawn nesting too deep
    stc
    syscall_ret_cf

.fn_spawn_push:
    push si
    push di
    push cx
    push ax

    ; Calculate destination: spawn_parent_stack + spawn_depth * 11
    movzx ax, byte [spawn_depth]
    mov cx, 11
    mul cx                          ; AX = spawn_depth * 11
    lea di, [spawn_parent_stack]
    add di, ax

    ; Copy caller's filename (11 bytes from DS:BX) to stack[depth]
    mov si, bx
    mov cx, 11
    rep movsb

    ; Increment depth
    inc byte [spawn_depth]

    pop ax
    pop cx
    pop di
    pop si

    ; Install stack trampoline so child's `ret` triggers SYS_EXIT —
    ; but ONLY if this is the outermost spawn.  Nested spawns (a spawned
    ; program calling SYS_SPAWN again) must NOT overwrite spawn_saved_ret,
    ; because [SHELL_SAVED_SP] already contains the trampoline from the
    ; first spawn.  Overwriting would save the trampoline address instead
    ; of the real shell return address, causing a crash on final exit.
    cmp word [spawn_saved_ret], 0
    jne .fn_spawn_exec              ; Already in a spawn chain — skip trampoline

    push bx
    mov bx, [SHELL_SAVED_SP]        ; BX = stack addr where return addr lives
    mov ax, [bx]                    ; AX = original shell return address
    mov [spawn_saved_ret], ax       ; Save it for later restoration
    mov word [bx], .spawn_trampoline ; Child's ret will hit this trampoline
    pop bx

.fn_spawn_exec:
    ; Mark that exec was called via spawn (for rollback on pre-load errors)
    mov byte [spawn_pending], 1
    ; Now do exactly what SYS_EXEC does (jump into .fn_exec)
    jmp .fn_exec

; Trampoline: reached when a spawn'd child does `ret`.
; Converts the ret into SYS_EXIT so spawn_depth is checked.
.spawn_trampoline:
    mov ah, SYS_EXIT
    int 0x80

; ─── spawn_rollback_if_pending ────────────────────────────────────────────────
; Called from .fn_exec error paths.  If exec was invoked via SYS_SPAWN and
; the file couldn't be loaded (pre-load error), undo the spawn state so the
; caller can continue running without corrupted spawn machinery.
;
; Undoes: spawn_depth decrement, trampoline removal, spawn_saved_ret restore.
; Preserves all registers except flags.
; ──────────────────────────────────────────────────────────────────────────────
.spawn_rollback_if_pending:
    cmp byte [spawn_pending], 0
    je .spawn_rb_done
    mov byte [spawn_pending], 0

    ; Decrement depth (undo the increment in .fn_spawn_push)
    dec byte [spawn_depth]

    ; If depth is back to 0, this was the outermost spawn — restore trampoline
    cmp byte [spawn_depth], 0
    jne .spawn_rb_done

    ; Restore original shell return address over the trampoline
    cmp word [spawn_saved_ret], 0
    je .spawn_rb_done               ; Safety: nothing to restore
    push bx
    push ax
    mov bx, [SHELL_SAVED_SP]
    mov ax, [spawn_saved_ret]
    mov [bx], ax                    ; Undo trampoline → restore real ret addr
    mov word [spawn_saved_ret], 0
    pop ax
    pop bx

.spawn_rb_done:
    ret

; ─── exec_parse_args ──────────────────────────────────────────────────────────
; Kernel-local argument parser for SYS_EXEC.
; Tokenizes the string at exec_args_buf into ARGV_ARGC / ARGV_PTRS / ARGV_STORAGE.
;
; Same semantics as shell_parse_args but operates on the kernel's exec_args_buf.
; Preserves all registers.
; ──────────────────────────────────────────────────────────────────────────────
exec_parse_args:
    push ax
    push bx
    push cx
    push si
    push di

    xor cl, cl                      ; CL = argc counter
    mov byte [ARGV_ARGC], 0

    mov si, exec_args_buf
    cmp byte [si], 0
    je .epa_done                    ; Empty → argc=0

    mov di, ARGV_STORAGE            ; DI = destination for tokenized strings

.epa_skip_spaces:
    lodsb
    cmp al, ' '
    je .epa_skip_spaces
    cmp al, 9
    je .epa_skip_spaces
    cmp al, 0
    je .epa_done
    dec si                          ; Back up

    ; Check max args
    cmp cl, ARGV_MAX_ARGS
    jae .epa_done

    ; Record argv[argc] pointer
    mov bx, cx
    and bx, 0x00FF                  ; Zero-extend CL
    shl bx, 1
    mov [ARGV_PTRS + bx], di

    ; Check for quoted argument
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
    inc si                          ; Skip opening quote
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
    stosb                           ; NUL-terminate this arg
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
