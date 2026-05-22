; =============================================================================
; MNOS16 Shell (SHELL.SYS) - Interactive Command Shell (User-Mode Executable)
;
; Loaded by KERNEL.SYS into memory at 0x3000.  Provides the interactive
; command-line interface for MNOS16.
;
; This is a user-mode executable (MNEX).  ALL hardware access goes through
; the kernel's INT 0x80 syscall interface — no direct BIOS calls or port I/O.
;
; The Boot Info Block (BIB) is obtained via SYS_GET_BIB (not hard-coded).
;   Offset 0: boot_drive  (1 byte)  — BIOS drive number
;   Offset 1: a20_status  (1 byte)  — A20 gate result (1=enabled, 0=failed)
;   Offset 2: part_lba    (4 bytes) — partition start LBA
;
; Header layout (first 6 bytes):
;   Offset 0: 'MNEX'   Magic identifier (4 bytes)  — user-mode executable
;   Offset 4: dw N     Shell size in sectors
;
; Available commands:
;   sysinfo  - Display 5 pages of system information
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
; Assembled with:  nasm -f bin -o shell.sys src/shell/shell.asm
; =============================================================================

%include "syscalls.inc"
%include "version.inc"
%include "mnfs.inc"
%include "memory.inc"

[BITS 16]
[ORG 0x3000]                        ; Kernel loads us here

; =============================================================================
; SHELL HEADER
; =============================================================================
shell_magic     db 'MNEX'           ; Magic identifier — user-mode executable
shell_sectors   dw 19               ; Shell size in sectors (updated as needed)

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

    ; "sysinfo"
    mov si, cmd_buf
    mov di, str_sysinfo
    call strcmp
    je cmd_sysinfo

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
%include "shell_cmd_sysinfo.inc"

%include "shell_readline.inc"

%include "shell_data.inc"

; =============================================================================
; PADDING — fill to sector boundary (16 sectors = 8192 bytes)
; =============================================================================
times (19 * 512) - ($ - $$) db 0
