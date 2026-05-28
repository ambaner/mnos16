; =============================================================================
; DATA
; =============================================================================
msg_syscall     db 'Syscall handler (INT 0x80)', 0
msg_fs          db 'FS.SYS loaded', 0
msg_fs_init     db 'Filesystem (INT 0x81)', 0
msg_fs_find     db 'FS.SYS not found', 0
msg_fs_load     db 'FS.SYS load', 0
msg_fs_initf    db 'FS.SYS init', 0
msg_shell       db 'SHELL.SYS loaded', 0
msg_sh_find     db 'SHELL.SYS not found', 0
msg_sh_load     db 'SHELL.SYS load', 0
msg_mm          db 'MM.SYS loaded', 0
msg_mm_init     db 'Memory manager (INT 0x82)', 0
msg_mm_find     db 'MM.SYS not found', 0
msg_mm_load     db 'MM.SYS load', 0
msg_mm_initf    db 'MM.SYS init', 0
msg_anykey      db 13, 10, '  Press any key...', 0

; 11-byte "8.3" filenames for directory lookup
fname_fs        db 'FS      SYS'
fname_fsd       db 'FSD     SYS'
fname_mm        db 'MM      SYS'
fname_mmd       db 'MMD     SYS'
fname_shell     db 'SHELL   SYS'
fname_shelld    db 'SHELLD  SYS'

; --- Disk Address Packet (DAP) for INT 13h AH=42h ----------------------------
; Used by both load_mnex (during init) and SYS_READ_SECTOR (during runtime).
; These never run concurrently, so sharing one DAP is safe.
dap:
    db 0x10, 0
dap_sectors:
    dw 0
dap_buffer:
    dw 0, 0
dap_lba:
    dd 0, 0

; --- Dynamic module loader state ---------------------------------------------
next_base:  dw 0                    ; Next available load address (kernel_start sets it)

; --- SYS_EXEC scratch space ---------------------------------------------------
exec_fname_buf  times 11 db 0       ; 11-byte filename copy (safe from TPA overwrite)
exec_args_buf   times 128 db 0      ; Args string copy (max 127 chars + NUL)
exec_args_len   dw 0                ; Length of copied args (including NUL)
exec_entry_addr dw 0                ; Computed entry point for new program
; --- SYS_SPAWN nesting stack --------------------------------------------------
; Supports up to SPAWN_MAX_DEPTH levels of nested spawn (e.g., mnmon→mnmon→edit).
; Each level stores the parent's 11-byte filename.
SPAWN_MAX_DEPTH equ 4
spawn_depth     db 0                ; Current nesting depth (0 = no spawn active)
spawn_parent_stack:
    times (SPAWN_MAX_DEPTH * 11) db 0  ; Stack of parent filenames (11 bytes each)
spawn_saved_ret dw 0                ; Original shell return addr (saved by outermost spawn)
spawn_pending   db 0                ; 1 = .fn_spawn jumped to .fn_exec (rollback on error)
