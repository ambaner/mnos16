; =============================================================================
; COMMAND: del <filename>
; Delete a file from the MNFS filesystem via INT 0x81 AH=0x07.
; =============================================================================
cmd_del:
    ; Point SI past "del " to the filename
    mov si, cmd_buf + 4

    ; Skip leading spaces
.del_skip:
    cmp byte [si], ' '
    jne .del_have_arg
    inc si
    jmp .del_skip

.del_have_arg:
    ; Empty arg?
    cmp byte [si], 0
    je .del_usage

    ; Parse filename into run_fname_buf (11-byte 8.3 format)
    call run_parse_filename
    jc .del_usage

    ; Require an extension for delete (must be a fully-qualified name)
    cmp byte [run_ext_provided], 0
    je .del_usage

    ; Call FS_DELETE_FILE: DS:SI → 11-byte name
    mov si, run_fname_buf
    mov ah, FS_DELETE_FILE
    int 0x81
    jc .del_error

    ; Success
    mov si, msg_del_ok
    mov ah, SYS_PRINT_STRING
    int 0x80
    jmp shell_prompt

.del_error:
    ; AL = error code
    cmp al, FS_ERR_NOT_FOUND
    je .del_not_found
    cmp al, FS_ERR_PROTECTED
    je .del_protected

    ; Generic I/O or unknown error
    mov si, msg_del_io_err
    mov ah, SYS_PRINT_STRING
    int 0x80
    jmp shell_prompt

.del_not_found:
    mov si, msg_del_not_found
    mov ah, SYS_PRINT_STRING
    int 0x80
    jmp shell_prompt

.del_protected:
    mov si, msg_del_protected
    mov ah, SYS_PRINT_STRING
    int 0x80
    jmp shell_prompt

.del_usage:
    mov si, msg_del_usage
    mov ah, SYS_PRINT_STRING
    int 0x80
    jmp shell_prompt

; =============================================================================
; COMMAND: ren <oldname> <newname>
; Rename a file on the MNFS filesystem via INT 0x81 AH=0x08.
; =============================================================================
cmd_ren:
    ; Point SI past "ren " to the first filename
    mov si, cmd_buf + 4

    ; Skip leading spaces
.ren_skip1:
    cmp byte [si], ' '
    jne .ren_have_arg1
    inc si
    jmp .ren_skip1

.ren_have_arg1:
    cmp byte [si], 0
    je .ren_usage

    ; Parse first filename (old name) into run_fname_buf
    call run_parse_filename
    jc .ren_usage

    ; Require extension on old name
    cmp byte [run_ext_provided], 0
    je .ren_usage

    ; Copy old name from run_fname_buf to ren_old_buf
    mov si, run_fname_buf
    mov di, ren_old_buf
    mov cx, 11
    rep movsb

    ; Now parse the second filename (new name) from run_args_ptr
    mov si, [run_args_ptr]
    cmp byte [si], 0
    je .ren_usage

    ; Parse second filename into run_fname_buf
    call run_parse_filename
    jc .ren_usage

    ; Require extension on new name
    cmp byte [run_ext_provided], 0
    je .ren_usage

    ; Call FS_RENAME_FILE: DS:SI → old 11-byte name, ES:DI → new 11-byte name
    mov si, ren_old_buf
    push ds
    pop es
    mov di, run_fname_buf
    mov ah, FS_RENAME_FILE
    int 0x81
    jc .ren_error

    ; Success
    mov si, msg_ren_ok
    mov ah, SYS_PRINT_STRING
    int 0x80
    jmp shell_prompt

.ren_error:
    cmp al, FS_ERR_NOT_FOUND
    je .ren_not_found
    cmp al, FS_ERR_EXISTS
    je .ren_exists

    mov si, msg_ren_io_err
    mov ah, SYS_PRINT_STRING
    int 0x80
    jmp shell_prompt

.ren_not_found:
    mov si, msg_ren_not_found
    mov ah, SYS_PRINT_STRING
    int 0x80
    jmp shell_prompt

.ren_exists:
    mov si, msg_ren_exists
    mov ah, SYS_PRINT_STRING
    int 0x80
    jmp shell_prompt

.ren_usage:
    mov si, msg_ren_usage
    mov ah, SYS_PRINT_STRING
    int 0x80
    jmp shell_prompt

; =============================================================================
; COMMAND: copy <source> <dest>
; Copy a file on the MNFS filesystem.
; Reads source via FS_READ_FILE into USER_PROG_BASE, then writes via
; FS_WRITE_FILE with the destination name.
; =============================================================================
cmd_copy:
    ; Point SI past "copy " to the first filename
    mov si, cmd_buf + 5

    ; Skip leading spaces
.cp_skip1:
    cmp byte [si], ' '
    jne .cp_have_arg1
    inc si
    jmp .cp_skip1

.cp_have_arg1:
    cmp byte [si], 0
    je .cp_usage

    ; Parse source filename into run_fname_buf
    call run_parse_filename
    jc .cp_usage

    ; Require extension on source name
    cmp byte [run_ext_provided], 0
    je .cp_usage

    ; Copy source name from run_fname_buf to ren_old_buf (reuse as src buf)
    mov si, run_fname_buf
    mov di, ren_old_buf
    mov cx, 11
    rep movsb

    ; Parse destination filename from run_args_ptr
    mov si, [run_args_ptr]
    cmp byte [si], 0
    je .cp_usage

    call run_parse_filename
    jc .cp_usage

    ; Require extension on dest name
    cmp byte [run_ext_provided], 0
    je .cp_usage

    ; --- Step 1: Find source file to get its size ---
    mov si, ren_old_buf
    mov ah, FS_FIND_FILE
    int 0x81
    jc .cp_not_found

    ; EAX=start sector, CX=sectors, EDX=size in bytes, BL=attr
    mov [cp_src_sectors], cx
    mov [cp_src_bytes], edx
    mov [cp_src_attr], bl

    ; Check size: max 60 sectors (30 KB) fits in TPA buffer
    cmp cx, 60
    ja .cp_too_large

    ; --- Step 2: Read source file into USER_PROG_BASE ---
    mov si, ren_old_buf
    mov ax, USER_PROG_BASE
    shr ax, 4                       ; Convert linear addr to segment
    mov es, ax
    xor bx, bx                     ; ES:BX = segment:0 = USER_PROG_BASE linear
    mov cx, [cp_src_sectors]
    mov ah, FS_READ_FILE
    int 0x81
    jc .cp_read_err

    ; --- Step 3: Write dest file with data from buffer ---
    mov si, run_fname_buf           ; DS:SI = dest 11-byte name
    mov ax, USER_PROG_BASE
    shr ax, 4
    mov es, ax
    xor bx, bx                     ; ES:BX = buffer with file data
    mov ecx, [cp_src_bytes]         ; ECX = size in bytes
    mov dl, [cp_src_attr]           ; DL = copy source attributes
    and dl, ~MNFS_ATTR_SYSTEM       ; Clear system flag on copy
    mov ah, FS_WRITE_FILE
    int 0x81
    jc .cp_write_err

    ; Success
    mov si, msg_cp_ok
    mov ah, SYS_PRINT_STRING
    int 0x80
    jmp shell_prompt

.cp_not_found:
    mov si, msg_cp_not_found
    mov ah, SYS_PRINT_STRING
    int 0x80
    jmp shell_prompt

.cp_too_large:
    mov si, msg_cp_too_large
    mov ah, SYS_PRINT_STRING
    int 0x80
    jmp shell_prompt

.cp_read_err:
    mov si, msg_cp_read_err
    mov ah, SYS_PRINT_STRING
    int 0x80
    jmp shell_prompt

.cp_write_err:
    ; AL has the error code
    cmp al, FS_ERR_EXISTS
    je .cp_exists
    cmp al, FS_ERR_DIR_FULL
    je .cp_dir_full
    cmp al, FS_ERR_DISK_FULL
    je .cp_disk_full

    mov si, msg_cp_io_err
    mov ah, SYS_PRINT_STRING
    int 0x80
    jmp shell_prompt

.cp_exists:
    mov si, msg_cp_exists
    mov ah, SYS_PRINT_STRING
    int 0x80
    jmp shell_prompt

.cp_dir_full:
    mov si, msg_cp_dir_full
    mov ah, SYS_PRINT_STRING
    int 0x80
    jmp shell_prompt

.cp_disk_full:
    mov si, msg_cp_disk_full
    mov ah, SYS_PRINT_STRING
    int 0x80
    jmp shell_prompt

.cp_usage:
    mov si, msg_cp_usage
    mov ah, SYS_PRINT_STRING
    int 0x80
    jmp shell_prompt

; --- copy command data --------------------------------------------------------
cp_src_sectors  dw 0
cp_src_bytes    dd 0
cp_src_attr     db 0
