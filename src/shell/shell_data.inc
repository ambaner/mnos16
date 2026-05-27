; =============================================================================
; DATA — String constants
; =============================================================================

; --- Shell strings -----------------------------------------------------------
msg_banner      db 13, 10
                db '  MNOS ', OS_VERSION_STR, ' ', 0      ; Tag appended at runtime

msg_tag_release db '[Release]', 13, 10, 13, 10, 0
msg_tag_debug   db '[Debug]', 13, 10, 13, 10, 0

msg_prompt      db 'mnos:\>', 0

msg_unknown     db 'Unknown command: ', 0

msg_help_text   db 'Available commands:', 13, 10
                db '  mem      - Detailed memory info and layout', 13, 10
                db '  dir      - List files on disk (MNFS)', 13, 10
                db '  copy     - Copy a file (copy SRC.EXT DST.EXT)', 13, 10
                db '  del      - Delete a file (del FILENAME.EXT)', 13, 10
                db '  ren      - Rename a file (ren OLD.EXT NEW.EXT)', 13, 10
                db '  ver      - Show version and build info', 13, 10
                db '  help     - Show this help message', 13, 10
                db '  cls      - Clear the screen', 13, 10
                db '  reboot   - Restart the system', 13, 10
                db '  (type any .MNX program name to run it)', 13, 10, 0

; --- Command name strings (for strcmp) ----------------------------------------
str_help        db 'help', 0
str_mem         db 'mem', 0
str_ver         db 'ver', 0
str_cls         db 'cls', 0
str_reboot      db 'reboot', 0
str_dir         db 'dir', 0
str_del         db 'del', 0
str_ren         db 'ren', 0
str_copy        db 'copy', 0

; --- ver command strings -----------------------------------------------------
msg_ver_line1   db '  MNOS ', OS_VERSION_STR, ' ', 0     ; Tag appended at runtime
msg_ver_rest    db '  Arch:      x86 real mode (16-bit)', 13, 10
                db '  Assembler: NASM', 13, 10
                db '  Platform:  Hyper-V Gen 1', 13, 10
                db '  Boot:      MBR -> VBR -> LOADER -> KERNEL -> SHELL', 13, 10
                db '  Filesystem: MNFS v1 (read/write, INT 0x81)', 13, 10
                db '  Memory:    HMA heap (64 KB) + TPA (30 KB)', 13, 10
                db '  Disk:      16 MB fixed VHD', 13, 10
                db '  Source:    github.com/ambaner/mini-os', 13, 10, 0

; --- ver command strings -----------------------------------------------------
; --- Memory strings (shared with mem command) ---------------------------------
msg_conv_mem    db '  Conv. memory: ', 0
msg_ext_mem     db '  Ext. memory:  ', 0
msg_kb          db ' KB', 13, 10, 0
msg_na          db 'N/A', 13, 10, 0
msg_e820_hdr    db '  E820 Memory Map:', 13, 10, 0
msg_e820_base   db '    Base=', 0
msg_e820_len    db ' Len=', 0
msg_e820_type   db ' Type=', 0

; E820 type names
msg_type_usable   db ' (Usable)', 13, 10, 0
msg_type_reserved db ' (Reserved)', 13, 10, 0
msg_type_acpi     db ' (ACPI)', 13, 10, 0
msg_type_nvs      db ' (ACPI NVS)', 13, 10, 0
msg_type_bad      db ' (Bad)', 13, 10, 0
msg_type_unknown  db ' (?)', 13, 10, 0

; --- mem command strings -----------------------------------------------------
msg_mem_hdr     db 13, 10, '--- Memory Information ---', 13, 10, 0
msg_bytes       db ' bytes', 13, 10, 0

msg_a20         db '  A20 gate:     ', 0
msg_a20_on      db 'Enabled (set by loader at boot)', 13, 10, 0
msg_a20_off     db 'FAILED - all 3 methods failed', 13, 10, 0
msg_a20_live    db '  A20 verify:   ', 0
msg_a20_on_short  db 'OK', 13, 10, 0
msg_a20_off_short db 'FAIL (wrap detected)', 13, 10, 0

msg_layout_hdr  db 13, 10, '  Real-mode memory layout:', 13, 10, 0
msg_layout      db '    0x00000-0x003FF  1 KB    IVT (Interrupt Vector Table)', 13, 10
                db '    0x00400-0x004FF  256 B   BDA (BIOS Data Area)', 13, 10
                db '    0x00500-0x005FF  256 B   Free (BIOS scratch)', 13, 10
                db '    0x00600-0x0060F  16 B    Boot Info Block (BIB)', 13, 10
                db '    0x00800-0x027FF  8 KB    FS.SYS (INT 0x81)', 13, 10
                db '    0x02800-0x02FFF  2 KB    MM.SYS (INT 0x82)', 13, 10
                db '    0x03000-0x04FFF  8 KB    SHELL.SYS (this code)', 13, 10
                db '    0x05000-0x06FFF  8 KB    KERNEL.SYS (INT 0x80)', 13, 10
                db '    0x07000-0x07BFF  3 KB    Stack', 13, 10
                db '    0x07C00-0x07FFF  1 KB    VBR/ABI (SP+args)', 13, 10
                db '    0x08000-0x08FFF  4 KB    HEAP (managed by MM)', 13, 10
                db '    0x09000-0x0F7FF  26 KB   TPA (user programs)', 13, 10
                db '    0x0A000-0x0BFFF  8 KB    Video RAM', 13, 10
                db '    0x0C000-0x0FFFF  16 KB   ROM area (BIOS, VGA)', 13, 10
                db '    0x10000-0xFFFFF  960 KB  Extended (requires A20)', 13, 10, 0

; --- Heap statistics strings --------------------------------------------------
msg_heap_hdr    db 13, 10, '  Heap (INT 0x82):', 13, 10, 0
msg_heap_type   db '    Type:     ', 0
msg_heap_hma    db 'HMA (~64 KB)', 13, 10, 0
msg_heap_conventional db 'Conventional (4 KB)', 13, 10, 0
msg_heap_total  db '    Total:    ', 0
msg_heap_used   db '    Used:     ', 0
msg_heap_free   db '    Free:     ', 0
msg_heap_blocks db '    Blocks:   ', 0
msg_heap_largest db '    Largest:  ', 0

; --- Heap block detail strings ------------------------------------------------
msg_heap_detail_hdr db 13, 10, '    Addr   Size   Status  Owner', 13, 10, 0
msg_heap_sz_eq  db '  ', 0
msg_heap_alloc  db '  used    ', 0
msg_heap_free_tag db '  free', 13, 10, 0

; Owner name strings
msg_own_none    db 'none', 13, 10, 0
msg_own_kern    db 'kernel', 13, 10, 0
msg_own_fs      db 'fs', 13, 10, 0
msg_own_mm      db 'mm', 13, 10, 0
msg_own_shell   db 'shell', 13, 10, 0
msg_own_usr1    db 'usr1', 13, 10, 0
msg_own_usr2    db 'usr2', 13, 10, 0
msg_own_usr3    db 'usr3', 13, 10, 0

; Owner name pointer table (indexed by owner ID 0-7)
mem_owner_table:
    dw msg_own_none
    dw msg_own_kern
    dw msg_own_fs
    dw msg_own_mm
    dw msg_own_shell
    dw msg_own_usr1
    dw msg_own_usr2
    dw msg_own_usr3

; --- Shared strings ----------------------------------------------------------
msg_crlf        db 13, 10, 0
msg_indent      db '    ', 0
msg_anykey      db 13, 10, '  Press any key...', 0

; =============================================================================
; RUNTIME DATA
; =============================================================================
cmd_buf         times 32 db 0       ; Command input buffer (31 chars + NUL)
cmd_len         db 0
e820_buf        times 20 db 0       ; E820 buffer (one entry)

; --- dir command strings -------------------------------------------------------
msg_dir_hdr     db 13, 10, '  Volume: MNFS v1', 13, 10, 0
msg_dir_cols    db '  Name          Type   Sec    Bytes', 13, 10
                db '  -----------------------------------', 13, 10, 0
msg_dir_indent  db '  ', 0
msg_dir_space   db '   ', 0
msg_dir_sys     db 'SYS  ', 0
msg_dir_exe     db 'EXE  ', 0
msg_dir_sysx    db 'SYS+X', 0
msg_dir_dat     db '---  ', 0
msg_dir_sec_suffix db ' sec', 0
msg_dir_bytes_suffix db 13, 10, 0
msg_dir_sep     db '  -----------------------------------', 13, 10, '  ', 0
msg_dir_summary db ' file(s)           ', 0
msg_dir_total_bytes db ' bytes', 13, 10, 0
msg_dir_used    db '  Used: ', 0
msg_dir_free    db '  Free: ', 0
msg_dir_of      db ' / ', 0
msg_dir_kb      db ' KB', 13, 10, 0
msg_dir_empty   db '  (no files)', 13, 10, 0

; --- dir command data ---------------------------------------------------------
dir_file_count  dw 0                ; File count for dir command
dir_used_sec    dw 0                ; Used sectors (from FS_GET_INFO)
dir_cap_sec     dw 0                ; Capacity sectors (from FS_GET_INFO)
dir_buffer      times 512 db 0      ; Buffer for MNFS directory data

; --- Debug syscall tag and messages -------------------------------------------
dbg_tag         db 'SHL', 0
dbg_init        db 'shell starting', 0
dbg_unknown     db 'unknown command', 0
dbg_run_exec    db 'launching program', 0
dbg_run_find    db 'find file', 0
dbg_run_loading db 'loading to TPA', 0
dbg_run_err_usage    db 'ERR: usage', 0
dbg_run_err_notfound db 'ERR: file_not_found', 0
dbg_run_err_noexec   db 'ERR: not_executable', 0
dbg_run_err_system   db 'ERR: system_file', 0
dbg_run_err_toolarge db 'ERR: too_large', 0
dbg_run_err_load     db 'ERR: load_failed', 0
dbg_run_err_header   db 'ERR: bad_header', 0

; --- run command strings ------------------------------------------------------
msg_run_usage      db 'Usage: PROGRAM[.MNX] [args...]', 13, 10, 0
msg_run_not_found  db 'Bad command or file not found', 13, 10, 0
msg_run_not_exec   db 'Not executable (.mnx required)', 13, 10, 0
msg_run_system     db 'System module, cannot run in user mode', 13, 10, 0
msg_run_too_large  db 'Program too large (26 KB max)', 13, 10, 0
msg_run_load_err   db 'Load error', 13, 10, 0
msg_run_bad_header db 'Invalid program header', 13, 10, 0

; --- run command data ---------------------------------------------------------
run_fname_buf   times 11 db 0       ; 8.3 filename buffer (uppercase, padded)
run_file_sectors dw 0               ; File size in sectors (from FS_FIND_FILE)
run_args_ptr    dw 0                ; Pointer to argument string in cmd_buf
run_ext_provided db 0               ; 1 if user typed a dot+extension, 0 if not
run_empty_args  db 0                ; Empty NUL string for no-args case
run_entry_addr  dw 0                ; Computed entry point address for program

; --- del/ren command strings --------------------------------------------------
msg_del_ok      db 'File deleted.', 13, 10, 0
msg_del_not_found db 'File not found.', 13, 10, 0
msg_del_protected db 'Cannot delete system file.', 13, 10, 0
msg_del_io_err  db 'Delete failed (I/O error).', 13, 10, 0
msg_del_usage   db 'Usage: del FILENAME.EXT', 13, 10, 0

msg_ren_ok      db 'File renamed.', 13, 10, 0
msg_ren_not_found db 'Source file not found.', 13, 10, 0
msg_ren_exists  db 'Target name already exists.', 13, 10, 0
msg_ren_io_err  db 'Rename failed (I/O error).', 13, 10, 0
msg_ren_usage   db 'Usage: ren OLDNAME.EXT NEWNAME.EXT', 13, 10, 0

; --- ren command data ---------------------------------------------------------
ren_old_buf     times 11 db 0       ; Old filename buffer for rename

; --- copy command strings -----------------------------------------------------
msg_cp_ok       db '1 file(s) copied.', 13, 10, 0
msg_cp_not_found db 'Source file not found.', 13, 10, 0
msg_cp_too_large db 'Source file too large (30 KB max).', 13, 10, 0
msg_cp_read_err db 'Read error.', 13, 10, 0
msg_cp_exists   db 'Destination already exists.', 13, 10, 0
msg_cp_dir_full db 'Directory full (15 files max).', 13, 10, 0
msg_cp_disk_full db 'Disk full.', 13, 10, 0
msg_cp_io_err   db 'Write error.', 13, 10, 0
msg_cp_usage    db 'Usage: copy SOURCE.EXT DEST.EXT', 13, 10, 0

