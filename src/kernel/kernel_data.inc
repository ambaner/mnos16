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
