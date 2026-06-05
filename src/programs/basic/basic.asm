; =============================================================================
; BASIC.MNX — Tiny GW-BASIC-style interpreter for MNOS16
;
; Invocation:
;   basic                 → empty workspace, REPL
;   basic FOO.BAS         → load FOO.BAS, drop into REPL with program loaded
;   basic FOO             → auto-append `.BAS`
;
; REPL accepts:
;   - Lines starting with a number  → enter/replace/delete program line.
;   - Bare command (PRINT, LIST, RUN, NEW, SAVE, LOAD, FILES, CLS,
;     SYSTEM, HELP, REM, END, STOP, CLEAR) → execute immediately.
;
; Exit: SYSTEM command (or Ctrl+C at the prompt aborts current input only).
;
; Modules:
;   basic_data.inc    — fixed-address BSS layout, constants, error codes
;   basic_tokens.inc  — keyword/function/operator token IDs + spellings
;   basic_lex.inc     — tokenizer + detokenizer
;   basic_err.inc     — central error path, ERR/ERL, message table
;   basic_edit.inc    — readline + program-line list ops
;   basic_load.inc    — LOAD / SAVE / FILES helpers
;   basic_stmt.inc    — statement dispatcher + handler set
; =============================================================================

%include "syscalls.inc"
%include "memory.inc"
%include "mnfs.inc"

[BITS 16]
%ifndef RELOC_BASE
%define RELOC_BASE 0
%endif
[ORG RELOC_BASE]

%include "basic_data.inc"

; =============================================================================
; ENTRY POINT (offset 6 — preceded by MNEX magic in the linker)
; =============================================================================
entry:
    cld

    ; Initialise interpreter state
    call bas_init

    ; Banner
    mov si, bas_banner
    call bas_puts_nul

    ; If we got a filename arg, attempt to LOAD it first
    call mn_get_argc
    cmp cl, 0
    je .no_arg
    xor cl, cl                      ; argv[0]
    call mn_get_argv
    jc .no_arg
    test cx, cx
    jz .no_arg
    ; DS:SI = arg, CX = length
    ; Pre-arm the error longjmp: if anything inside the load path calls
    ; bas_error, we want it to land at bas_repl_resume with a valid SP
    ; rather than jumping with SP=0 (which would push to 0xFFFE and crash).
    ; A leaked heap buffer in the failure case is acceptable; the user can
    ; NEW or re-LOAD to recover.
    mov [bas_repl_sp], sp
    call bas_load_file
    jnc .no_arg                     ; success — fall through into REPL
    ; Load failed: print "?<Name> Error" but stay in REPL
    push ax                         ; save error code
    mov al, '?'
    call bas_lex_putc
    pop ax
    xor ah, ah
    cmp al, BERR_MAX
    jbe .arg_err_ok
    xor al, al
.arg_err_ok:
    mov bx, ax
    shl bx, 1
    mov si, [bas_err_table + bx]
    call bas_puts_nul
    mov si, bas_err_msg_suffix
    call bas_puts_nul
    call bas_crlf

.no_arg:
    ; --- REPL loop --------------------------------------------------------
    ; bas_repl_resume is the longjmp target for bas_error.
bas_repl_resume:
    mov [bas_repl_sp], sp           ; record SP for longjmp recovery
    mov byte [bas_in_run], 0
.repl_loop:
    mov si, bas_prompt_ok
    call bas_puts_nul
    call bas_readline               ; DS:SI = line, AX = length
    ; Empty line? Just re-prompt.
    test ax, ax
    jz .repl_loop
    call bas_handle_input_line
    jmp .repl_loop


; =============================================================================
; bas_init — initialise interpreter state.
; =============================================================================
bas_init:
    push ax
    push cx
    push di
    push es
    ; Zero BSS scalars + tables
    mov ax, ds
    mov es, ax
    mov di, BAS_BSS_BASE
    mov cx, 0xC000 - BAS_BSS_BASE   ; bytes to zero
    xor ax, ax
    cld
    rep stosb
    pop es
    pop di
    pop cx
    pop ax
    ; Default DEF SEG = our data segment
    push ax
    mov ax, ds
    mov [bas_def_seg], ax
    pop ax
    ret


; =============================================================================
; Banner / prompt strings
; =============================================================================
bas_banner:
    db 'MNOS16 BASIC 1.0',13,10
    db 'Type HELP for commands, SYSTEM to exit.',13,10,0
bas_prompt_ok:
    db 'Ok',13,10,0


; =============================================================================
; MODULE INCLUDES (order matters — define labels before use)
; =============================================================================
%include "basic_tokens.inc"
%include "basic_lex.inc"
%include "basic_err.inc"
%include "basic_edit.inc"
%include "basic_load.inc"
%include "basic_var.inc"
%include "basic_expr.inc"
%include "basic_stmt.inc"
%include "mnoslib.inc"
