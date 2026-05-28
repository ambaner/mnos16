; =============================================================================
; MNOS16 Shell (SHELL.SYS) - Interactive Command Shell (User-Mode Executable)
;
; Relocatable system module — loaded by KERNEL.SYS at a dynamic address
; (immediately after MM.SYS).  Assembled with ORG 0 and relocated at load
; time via MNEX v2 header.
;
; Provides the interactive command-line interface for MNOS16.
;
; This is a user-mode executable (MNEX).  ALL hardware access goes through
; the kernel's INT 0x80 syscall interface — no direct BIOS calls or port I/O.
;
; Available commands:
;   mem      - Detailed memory info and layout
;   ver      - Show version and build info
;   help     - List available commands
;   cls      - Clear the screen
;   dir      - List files on disk
;   copy     - Copy a file
;   del      - Delete a file
;   ren      - Rename a file
;   reboot   - Warm-reboot the system
;
; Build: assembled by gen_relocs.py + pack_module.py (see tools/build.ps1)
; =============================================================================

%include "syscalls.inc"
%include "version.inc"
%include "mnfs.inc"
%include "memory.inc"

[BITS 16]

; Relocatable module — assembled at ORG 0, relocated at load time.
%ifndef RELOC_BASE
%define RELOC_BASE 0
%endif
[ORG RELOC_BASE]

; =============================================================================
; SHELL INIT
;
; 1. Clear screen via syscall
; 2. Print banner
; 3. Fall through to prompt loop
; =============================================================================
shell_init:
    ; Clear screen via kernel syscall (sets video mode 3 = 80x25 color text)
    mov ah, SYS_CLEAR_SCREEN
    int 0x80

    ; Debug: shell starting
    mov bx, dbg_tag
    mov si, dbg_init
    mov ah, SYS_DBG_PRINT
    int 0x80

    ; Print banner (version line without trailing CRLF)
    mov si, msg_banner
    mov ah, SYS_PRINT_STRING
    int 0x80

    ; Print boot mode tag ([Release] or [Debug])
    call print_boot_tag

; --- Prompt loop (returns here after each command) ---------------------------
shell_prompt:
    ; Print the shell prompt
    mov si, msg_prompt
    mov ah, SYS_PRINT_STRING
    int 0x80

    ; Read a line of user input into cmd_buf (up to 31 chars)
    call readline

    ; Debug: log the command entered
    mov bx, dbg_tag
    mov si, cmd_buf
    mov ah, SYS_DBG_PRINT
    int 0x80

    ; --- Command dispatch ----------------------------------------------------
    ; Empty input (just pressed Enter) -> re-prompt
    cmp byte [cmd_buf], 0
    je shell_prompt

    ; "help"
    mov si, cmd_buf
    mov di, str_help
    call strcmp
    je cmd_help

    ; "mem"
    mov si, cmd_buf
    mov di, str_mem
    call strcmp
    je cmd_mem

    ; "cls"
    mov si, cmd_buf
    mov di, str_cls
    call strcmp
    je cmd_cls

    ; "ver"
    mov si, cmd_buf
    mov di, str_ver
    call strcmp
    je cmd_ver

    ; "reboot"
    mov si, cmd_buf
    mov di, str_reboot
    call strcmp
    je cmd_reboot

    ; "dir"
    mov si, cmd_buf
    mov di, str_dir
    call strcmp
    je cmd_dir

    ; "del"
    mov si, cmd_buf
    mov di, str_del
    call cmdmatch
    je cmd_del

    ; "ren"
    mov si, cmd_buf
    mov di, str_ren
    call cmdmatch
    je cmd_ren

    ; "copy"
    mov si, cmd_buf
    mov di, str_copy
    call cmdmatch
    je cmd_copy

    ; Unknown command — try to execute it as a program
    jmp cmd_run_implicit


%include "shell_cmd_simple.inc"
%include "shell_cmd_dir.inc"
%include "shell_cmd_fs.inc"
%include "shell_cmd_run.inc"
%include "shell_parse_args.inc"
%include "shell_cmd_mem.inc"

%include "shell_readline.inc"

%include "shell_data.inc"

; =============================================================================
; END OF MODULE — no padding; pack_module.py handles sector alignment
; =============================================================================
