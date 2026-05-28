; =============================================================================
; boot_msg.inc — Linux-style boot status messages
;
; Provides formatted boot progress output modeled after systemd:
;   [  OK  ] <message>          — Successful boot step
;   [FAIL] <message>            — Fatal boot error (halts CPU)
;
; Example output:
;   [  OK  ] LOADER.SYS
;   [  OK  ] A20 gate enabled
;   [  OK  ] KERNEL.SYS
;   [  OK  ] Syscall handler (INT 0x80)
;   [  OK  ] FS.SYS
;   [  OK  ] Filesystem (INT 0x81)
;   [  OK  ] SHELL.SYS
;
; Requirements:
;   - The including file must define: puts (print NUL-terminated string)
;
; Each boot-chain binary (%includes this file) gets its own copy — same
; pattern as find_file.inc and load_binary.inc.
; =============================================================================

; -----------------------------------------------------------------------------
; boot_ok — Print a success status line
;
; Input:  SI = NUL-terminated message string
; Output: none
; Preserves: EAX, CX (critical — find_file returns values in these)
; Clobbers: SI (via puts)
; -----------------------------------------------------------------------------
boot_ok:
    push eax
    push cx
    push si
    mov si, _bm_pfx_ok
    call puts
    pop si
    call puts
    mov si, _bm_crlf
    call puts
    pop cx
    pop eax
    ret

; -----------------------------------------------------------------------------
; boot_fail — Print a failure status line with optional register dump, then halt
;
; Input:  SI = NUL-terminated message string
; Output: does not return (CPU halted)
;
; If BOOT_REGDUMP is defined before %include, prints register values:
;   [FAIL] <message>
;     AX=xxxx BX=xxxx CX=xxxx DX=xxxx
;     SI=xxxx DI=xxxx SP=xxxx BP=xxxx
;     DS=xxxx ES=xxxx SS=xxxx FL=xxxx
; Otherwise prints just the message and halts.
; -----------------------------------------------------------------------------
boot_fail:
%ifdef BOOT_REGDUMP
    ; Save registers FIRST, before any calls clobber them
    mov [_bm_sv_ax], ax
    mov [_bm_sv_bx], bx
    mov [_bm_sv_cx], cx
    mov [_bm_sv_dx], dx
    mov [_bm_sv_si], si
    mov [_bm_sv_di], di
    mov [_bm_sv_sp], sp
    mov [_bm_sv_bp], bp
    mov [_bm_sv_ds], ds
    mov [_bm_sv_es], es
    mov [_bm_sv_ss], ss
    pushf
    pop word [_bm_sv_fl]
%endif

    ; Print [FAIL] prefix + message + newline
    push si
    mov si, _bm_pfx_fail
    call puts
    pop si
    call puts
    mov si, _bm_crlf
    call puts

%ifdef BOOT_REGDUMP
    ; Line 1: AX BX CX DX
    mov si, _bm_lbl_ax
    call puts
    mov ax, [_bm_sv_ax]
    call _bm_hex16

    mov si, _bm_lbl_bx
    call puts
    mov ax, [_bm_sv_bx]
    call _bm_hex16

    mov si, _bm_lbl_cx
    call puts
    mov ax, [_bm_sv_cx]
    call _bm_hex16

    mov si, _bm_lbl_dx
    call puts
    mov ax, [_bm_sv_dx]
    call _bm_hex16

    mov si, _bm_crlf
    call puts

    ; Line 2: SI DI SP BP
    mov si, _bm_lbl_si
    call puts
    mov ax, [_bm_sv_si]
    call _bm_hex16

    mov si, _bm_lbl_di
    call puts
    mov ax, [_bm_sv_di]
    call _bm_hex16

    mov si, _bm_lbl_sp
    call puts
    mov ax, [_bm_sv_sp]
    call _bm_hex16

    mov si, _bm_lbl_bp
    call puts
    mov ax, [_bm_sv_bp]
    call _bm_hex16

    mov si, _bm_crlf
    call puts

    ; Line 3: DS ES SS FL
    mov si, _bm_lbl_ds
    call puts
    mov ax, [_bm_sv_ds]
    call _bm_hex16

    mov si, _bm_lbl_es
    call puts
    mov ax, [_bm_sv_es]
    call _bm_hex16

    mov si, _bm_lbl_ss
    call puts
    mov ax, [_bm_sv_ss]
    call _bm_hex16

    mov si, _bm_lbl_fl
    call puts
    mov ax, [_bm_sv_fl]
    call _bm_hex16

    mov si, _bm_crlf
    call puts
%endif

    cli
.bf_halt:
    hlt
    jmp .bf_halt                    ; Safety: infinite halt loop

%ifdef BOOT_REGDUMP
; -----------------------------------------------------------------------------
; _bm_hex16 — Print AX as 4 hex digits (internal helper)
; Clobbers: AX, CX
; -----------------------------------------------------------------------------
_bm_hex16:
    push cx
    push ax
    mov cx, 4
.hex_loop:
    rol ax, 4
    push ax
    and al, 0x0F
    add al, '0'
    cmp al, '9'
    jbe .hex_digit
    add al, 7
.hex_digit:
    mov ah, 0x0E
    xor bh, bh
    int 0x10
    pop ax
    dec cx
    jnz .hex_loop
    pop ax
    pop cx
    ret
%endif

; --- String constants --------------------------------------------------------
_bm_pfx_ok:    db '[  OK  ] ', 0      ; 10 bytes
_bm_pfx_fail:  db '[FAIL] ', 0        ; 8 bytes
_bm_crlf:      db 13, 10, 0           ; 3 bytes

%ifdef BOOT_REGDUMP
_bm_lbl_ax:    db '  AX=', 0
_bm_lbl_bx:    db ' BX=', 0
_bm_lbl_cx:    db ' CX=', 0
_bm_lbl_dx:    db ' DX=', 0
_bm_lbl_si:    db '  SI=', 0
_bm_lbl_di:    db ' DI=', 0
_bm_lbl_sp:    db ' SP=', 0
_bm_lbl_bp:    db ' BP=', 0
_bm_lbl_ds:    db '  DS=', 0
_bm_lbl_es:    db ' ES=', 0
_bm_lbl_ss:    db ' SS=', 0
_bm_lbl_fl:    db ' FL=', 0
_bm_sv_ax:     dw 0
_bm_sv_bx:     dw 0
_bm_sv_cx:     dw 0
_bm_sv_dx:     dw 0
_bm_sv_si:     dw 0
_bm_sv_di:     dw 0
_bm_sv_sp:     dw 0
_bm_sv_bp:     dw 0
_bm_sv_ds:     dw 0
_bm_sv_es:     dw 0
_bm_sv_ss:     dw 0
_bm_sv_fl:     dw 0
%endif
