; =============================================================================
; debug.inc — Debug logging and assertion macros for MNOS16
;
; Provides DBG, DBG_REG, DBG_REGS, ASSERT, ASSERT_CF_CLEAR, and ASSERT_MAGIC
; macros that output to the serial port.  All macros are conditional on
; %ifdef DEBUG — in release builds they expand to exactly 0 bytes.
;
; Prerequisites:
;   - serial.inc must be %included in the same file (provides serial_*)
;   - serial_init must have been called before any macro is used
;   - Define ASSERT_HAS_SCREEN before %include "debug.inc" if the binary
;     provides a `puts` subroutine (enables screen output on assert failure)
;
; Usage:
;   %include "serial.inc"
;   %define ASSERT_HAS_SCREEN        ; optional — if puts is available
;   %include "debug.inc"
;
;   DBG "Loading FS.SYS"          ; → [DBG] Loading FS.SYS\r\n  (to COM1)
;   DBG_REG "AX", ax              ; → AX=1234                   (to COM1)
;   DBG_REGS                       ; → AX=xxxx BX=xxxx ... \r\n (to COM1)
;   ASSERT cx, ne, 0, "sector count must not be zero"
;   ASSERT_CF_CLEAR "disk read failed"
;   ASSERT_MAGIC bx, 'MNFS', "bad directory magic"
;
; See doc/DEBUGGING.md §3 and §5 for full specification.
; =============================================================================

%ifdef DEBUG

; -----------------------------------------------------------------------------
; DBG — Print a debug message to serial (compile-time string constant)
;
; Generates an inline string with a jmp to skip over it.
; Output format: [DBG] <message>\r\n
;
; Usage:  DBG "Loading FS.SYS"
; -----------------------------------------------------------------------------
%macro DBG 1
    jmp %%after
    %%msg: db '[DBG] ', %1, 13, 10, 0
%%after:
    push si
    mov si, %%msg
    call serial_puts
    pop si
%endmacro

; -----------------------------------------------------------------------------
; DBG_REG — Print a register name and its hex value to serial
;
; Output format: <name>=<hex_value><space>  (no newline)
;
; Usage:  DBG_REG "AX", ax
;         DBG_REG "DI", di
; -----------------------------------------------------------------------------
%macro DBG_REG 2
    jmp %%after
    %%lbl: db %1, '=', 0
%%after:
    push si
    push ax
    mov si, %%lbl
    call serial_puts
    mov ax, %2
    call serial_hex16
    mov al, ' '
    call serial_putc
    pop ax
    pop si
%endmacro

; -----------------------------------------------------------------------------
; DBG_REGS — Dump all general-purpose registers to serial
;
; Output format: AX=xxxx BX=xxxx CX=xxxx DX=xxxx SI=xxxx DI=xxxx\r\n
;
; Usage:  DBG_REGS
; -----------------------------------------------------------------------------
%macro DBG_REGS 0
    DBG_REG "AX", ax
    DBG_REG "BX", bx
    DBG_REG "CX", cx
    DBG_REG "DX", dx
    DBG_REG "SI", si
    DBG_REG "DI", di
    call serial_crlf
%endmacro

; -----------------------------------------------------------------------------
; ASSERT — Halt with message if a register comparison fails
;
; Compares a register against an immediate or register value.  If the condition
; is NOT met, logs the assertion failure to serial AND screen, dumps registers,
; and halts the CPU.
;
; Usage:
;   ASSERT ax, e, 0x4D4E, "MNFS magic mismatch (first word)"
;   ASSERT cx, ne, 0, "sector count must not be zero"
;   ASSERT bx, b, 32, "sector count exceeds maximum"
;
; Parameters:
;   %1 = register or operand to test
;   %2 = condition code (e/ne/b/be/a/ae/z/nz)
;   %3 = comparison value (immediate or register)
;   %4 = failure message string
;
; The condition code specifies what MUST be true.  If it is false, we fail.
; Internally: cmp %1, %3 → j<condition> %%ok → [failure path]
; -----------------------------------------------------------------------------
%macro ASSERT 4
    cmp %1, %3
    j%2 %%ok
    ; --- Assertion failed ---
    jmp %%after_msg
    %%msg: db '[ASSERT FAIL] ', %4, 13, 10, 0
%%after_msg:
    push si
    mov si, %%msg
    call serial_puts                ; Log to serial
    pop si
    DBG_REGS                        ; Dump all registers to serial
%ifdef ASSERT_HAS_SCREEN
    push si
    mov si, %%msg
    call puts                       ; Also print to screen
    pop si
%endif
    cli
    hlt                             ; Hard stop — assertion violated
%%ok:
%endmacro

; -----------------------------------------------------------------------------
; ASSERT_CF_CLEAR — Halt if carry flag is set (error indicator)
;
; Many BIOS and MNOS16 operations signal errors via CF.  Place this macro
; immediately after such a call to catch errors that would otherwise be
; silently ignored.
;
; Usage:
;   int 0x80
;   ASSERT_CF_CLEAR "disk read failed in FS init"
;
; Parameters:
;   %1 = failure message string
; -----------------------------------------------------------------------------
%macro ASSERT_CF_CLEAR 1
    jnc %%ok
    ; --- CF is set — error condition ---
    jmp %%after_msg
    %%msg: db '[ASSERT FAIL] CF set: ', %1, 13, 10, 0
%%after_msg:
    push si
    mov si, %%msg
    call serial_puts
    pop si
    DBG_REGS
%ifdef ASSERT_HAS_SCREEN
    push si
    mov si, %%msg
    call puts
    pop si
%endif
    cli
    hlt
%%ok:
%endmacro

; -----------------------------------------------------------------------------
; ASSERT_MAGIC — Halt if a 4-byte memory value doesn't match expected magic
;
; Validates magic signatures (MNOS, MNLD, MNKN, MNEX, MNFS) at load points.
;
; Usage:
;   ASSERT_MAGIC bx, 'MNFS', "FS directory magic mismatch"
;   ASSERT_MAGIC di, 'MNKN', "kernel magic mismatch"
;
; Parameters:
;   %1 = register pointing to memory location containing the 4-byte value
;   %2 = expected 4-byte magic (e.g., 'MNFS')
;   %3 = failure message string
;
; Clobbers: none (EAX saved/restored internally)
; -----------------------------------------------------------------------------
%macro ASSERT_MAGIC 3
    push eax
    mov eax, [%1]
    cmp eax, %2
    je %%ok
    ; --- Magic mismatch ---
    jmp %%after_msg
    %%msg: db '[ASSERT FAIL] ', %3, 13, 10, 0
    %%exp_lbl: db '  Expected: ', 0
    %%act_lbl: db '  Actual:   ', 0
%%after_msg:
    push si
    ; Print failure message
    mov si, %%msg
    call serial_puts
    ; Print expected value
    mov si, %%exp_lbl
    call serial_puts
    mov eax, %2
    push ax
    shr eax, 16
    call serial_hex16               ; High word
    pop ax
    call serial_hex16               ; Low word
    call serial_crlf
    ; Print actual value
    mov si, %%act_lbl
    call serial_puts
    pop si                          ; Balance push si
    pop eax                         ; Recover original EAX (the actual value)
    push eax                        ; Re-save for final pop
    push si
    push ax
    shr eax, 16
    call serial_hex16               ; High word of actual
    pop ax
    call serial_hex16               ; Low word of actual
    call serial_crlf
    pop si
    ; Screen output (only if host provides puts)
%ifdef ASSERT_HAS_SCREEN
    mov si, %%msg
    call puts
%endif
    DBG_REGS
    cli
    hlt
%%ok:
    pop eax
%endmacro

%else
; =============================================================================
; Release build — all debug macros expand to nothing (0 bytes)
; =============================================================================
%macro DBG 1
%endmacro
%macro DBG_REG 2
%endmacro
%macro DBG_REGS 0
%endmacro
%macro ASSERT 4
%endmacro
%macro ASSERT_CF_CLEAR 1
%endmacro
%macro ASSERT_MAGIC 3
%endmacro

%endif ; DEBUG

; --- Stack canary macros (must be defined before kernel code uses them) -------
; In debug builds: call canary_init / canary_check (defined in kernel_stack.inc)
; In release builds: expand to nothing (0 bytes)
%ifdef DEBUG
    %macro CANARY_INIT 0
        call canary_init
    %endmacro
    %macro CANARY_CHECK 0
        call canary_check
    %endmacro
%else
    %macro CANARY_INIT 0
    %endmacro
    %macro CANARY_CHECK 0
    %endmacro
%endif
