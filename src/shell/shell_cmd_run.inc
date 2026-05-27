; =============================================================================
; COMMAND: implicit program execution
; Load and execute a user program (.MNX) from the MNFS filesystem.
;
; Any unrecognized command typed at the shell prompt is treated as a program
; name.  The .MNX extension is optional — the shell auto-appends it or uses
; FS_FIND_BASE to search by base name.
;
; Execution flow:
;   1. Parse filename from command line
;   2. Convert to uppercase 8.3 format (auto-append MNX if no extension)
;   3. Find file via FS_FIND_BASE (no ext) or FS_FIND_FILE (ext provided)
;   4. Check attributes: no ATTR_SYSTEM, has ATTR_EXEC
;   5. Check size <= USER_PROG_MAX_SEC sectors
;   6. Load file to USER_PROG_BASE (0x8000)
;   7. Validate MNEX magic at load address
;   8. Detect v2 (flag check + secondary validation: entry_offset == 12+N*2)
;   9. Apply relocations if v2; else fall back to v1 entry at offset 6
;  10. Save shell state (SP, args pointer)
;  11. Execute program via memory-indirect call to entry point
;
; See doc/PROGRAM-LOADER.md for the complete specification.
; =============================================================================

; Implicit execution entry — entire cmd_buf is the command line
cmd_run_implicit:
    mov si, cmd_buf                 ; SI → start of buffer

    ; Skip any leading spaces
run_skip_spaces:
    cmp byte [si], ' '
    jne .run_have_filename
    inc si
    jmp run_skip_spaces

.run_have_filename:
    ; Check if we have a filename at all
    cmp byte [si], 0
    je .run_err_usage

    ; SI now points to the filename. Copy and convert to uppercase 8.3.
    ; We need to produce an 11-byte padded name in run_fname_buf.
    call run_parse_filename         ; SI=input, run_fname_buf=output
    jc .run_err_usage               ; CF set = parse error

    ; --- Step 2: Find where args start (after filename) -----------------------
    ; run_args_ptr was set by run_parse_filename to point past the filename
    ; Store the args pointer at the shared ABI location
    mov ax, [run_args_ptr]
    mov [SHELL_ARGS_PTR], ax

    ; --- Step 2b: Parse args into structured argc/argv table ------------------
    call shell_parse_args

    ; --- Step 3: Find file -------------------------------------------------------
    ; Debug: trace file lookup
    push bx
    push si
    mov bx, dbg_tag
    mov si, dbg_run_find
    mov ah, SYS_DBG_PRINT
    int 0x80
    pop si
    pop bx

    mov si, run_fname_buf           ; DS:SI = 11-byte filename (or 8-byte name)
    cmp byte [run_ext_provided], 0
    jne .run_exact_search

    ; No extension: search by base name only (8 bytes)
    mov ah, FS_FIND_BASE
    int 0x81                        ; Returns: EAX=start, CX=sectors, BL=attr
    jc .run_err_not_found           ; Not found at all

    ; Save sector count
    mov [run_file_sectors], cx

    ; Check attributes of what was found
    test bl, MNFS_ATTR_SYSTEM
    jnz .run_err_system             ; System module — cannot run

    test bl, MNFS_ATTR_EXEC
    jz .run_err_not_exec            ; Found but not executable

    ; Has EXEC — it's a .MNX file, proceed to load
    jmp .run_do_load

.run_exact_search:
    ; Extension provided: exact 11-byte match
    mov ah, FS_FIND_FILE
    int 0x81                        ; Returns: EAX=start, CX=sectors, BL=attr
    jc .run_err_not_found           ; Not found

    ; Save sector count for size check
    mov [run_file_sectors], cx

    ; --- Step 4: Attribute validation (before extension check) -----------------
    ; BL = attribute byte from FS_FIND_FILE
    test bl, MNFS_ATTR_SYSTEM
    jnz .run_err_system             ; Cannot run system files

    ; --- Step 5: Validate extension is "MNX" ----------------------------------
    ; Check bytes 8-10 of run_fname_buf
    cmp byte [run_fname_buf + 8], 'M'
    jne .run_err_not_exec
    cmp byte [run_fname_buf + 9], 'N'
    jne .run_err_not_exec
    cmp byte [run_fname_buf + 10], 'X'
    jne .run_err_not_exec

    ; --- Step 6: EXEC attribute required --------------------------------------
    test bl, MNFS_ATTR_EXEC
    jz .run_err_not_exec            ; Must have EXEC attribute

.run_do_load:
    ; --- Size check -----------------------------------------------------------
    cmp word [run_file_sectors], USER_PROG_MAX_SEC
    ja .run_err_too_large

    ; Debug: trace file load
    push bx
    push si
    mov bx, dbg_tag
    mov si, dbg_run_loading
    mov ah, SYS_DBG_PRINT
    int 0x80
    pop si
    pop bx

    ; --- Load file to TPA -----------------------------------------------------
    mov si, run_fname_buf           ; DS:SI = filename
    push ds
    pop es                          ; ES = DS = 0
    mov bx, USER_PROG_BASE          ; ES:BX = load address
    mov cx, USER_PROG_MAX_SEC        ; CX = max sectors to read
    mov ah, FS_READ_FILE
    int 0x81
    jc .run_err_load

    ; --- Step 8: Validate MNEX magic at load address --------------------------
    cmp dword [USER_PROG_BASE], 'MNEX'
    jne .run_err_bad_header

    ; --- Step 8b: Apply relocations if v2 header ------------------------------
    ; Check flags field (offset 6) for MNEX_V2_FLAG_RELOC
    mov di, USER_PROG_BASE
    test word [di + 6], MNEX_V2_FLAG_RELOC
    jz .run_no_relocs

    ; Secondary v2 validation: entry_offset must equal 12 + reloc_count * 2
    ; This prevents false-positive detection when a v1 binary's first opcode
    ; happens to have bit 0 set (e.g., 0xEB jmp short, 0x55 push bp).
    mov ax, [di + 8]               ; AX = reloc_count
    shl ax, 1                      ; AX = reloc_count * 2
    add ax, MNEX_V2_HDR_BASE       ; AX = expected entry_offset (12 + N*2)
    cmp ax, [di + 10]              ; Compare with actual entry_offset field
    jne .run_no_relocs             ; Mismatch → not really v2, treat as v1

    ; v2 relocatable program — patch absolute references
    mov cx, [di + 8]               ; reloc_count
    jcxz .run_no_relocs            ; Zero relocs = nothing to do
    lea si, [di + MNEX_V2_HDR_BASE] ; SI = start of relocation table

.run_patch_loop:
    lodsw                          ; AX = file-relative offset of word to patch
    mov bx, di
    add bx, ax                     ; BX = absolute address of the word
    add word [bx], di              ; Add load_base (USER_PROG_BASE) to value
    loop .run_patch_loop

    ; Entry point from v2 header
    mov ax, [di + 10]              ; entry_offset (file-relative)
    add ax, di                     ; Convert to absolute address
    mov [run_entry_addr], ax
    jmp .run_do_exec

.run_no_relocs:
    ; Legacy v1 program — entry at fixed offset 6
    mov ax, USER_PROG_BASE + MNEX_HDR_SIZE
    mov [run_entry_addr], ax

.run_do_exec:
    ; --- Step 9: Save shell state for SYS_EXIT --------------------------------
    ; Save SP-2 (where the return address will be after `call`)
    mov ax, sp
    sub ax, 2
    mov [SHELL_SAVED_SP], ax

    ; --- Step 10: Execute the program -----------------------------------------
    ; Debug trace before launch
    push bx
    push si
    mov bx, dbg_tag
    mov si, dbg_run_exec
    mov ah, SYS_DBG_PRINT
    int 0x80
    pop si
    pop bx

    ; Indirect call to program entry via register (avoids relocation issues)
    call [run_entry_addr]

    ; --- Program returned (via ret or SYS_EXIT) -------------------------------
    ; Print a newline and return to prompt
    mov si, msg_crlf
    mov ah, SYS_PRINT_STRING
    int 0x80
    jmp shell_prompt

; --- Error handlers -----------------------------------------------------------

.run_err_usage:
    push bx
    push si
    mov bx, dbg_tag
    mov si, dbg_run_err_usage
    mov ah, SYS_DBG_PRINT
    int 0x80
    pop si
    pop bx
    mov si, msg_run_usage
    mov ah, SYS_PRINT_STRING
    int 0x80
    jmp shell_prompt

; --- Error: not found ---------------------------------------------------------
.run_err_not_found:
    push bx
    push si
    mov bx, dbg_tag
    mov si, dbg_run_err_notfound
    mov ah, SYS_DBG_PRINT
    int 0x80
    pop si
    pop bx
    mov si, msg_run_not_found
    mov ah, SYS_PRINT_STRING
    int 0x80
    jmp shell_prompt

.run_err_not_exec:
    push bx
    push si
    mov bx, dbg_tag
    mov si, dbg_run_err_noexec
    mov ah, SYS_DBG_PRINT
    int 0x80
    pop si
    pop bx
    mov si, msg_run_not_exec
    mov ah, SYS_PRINT_STRING
    int 0x80
    jmp shell_prompt

.run_err_system:
    push bx
    push si
    mov bx, dbg_tag
    mov si, dbg_run_err_system
    mov ah, SYS_DBG_PRINT
    int 0x80
    pop si
    pop bx
    mov si, msg_run_system
    mov ah, SYS_PRINT_STRING
    int 0x80
    jmp shell_prompt

.run_err_too_large:
    push bx
    push si
    mov bx, dbg_tag
    mov si, dbg_run_err_toolarge
    mov ah, SYS_DBG_PRINT
    int 0x80
    pop si
    pop bx
    mov si, msg_run_too_large
    mov ah, SYS_PRINT_STRING
    int 0x80
    jmp shell_prompt

.run_err_load:
    push bx
    push si
    mov bx, dbg_tag
    mov si, dbg_run_err_load
    mov ah, SYS_DBG_PRINT
    int 0x80
    pop si
    pop bx
    mov si, msg_run_load_err
    mov ah, SYS_PRINT_STRING
    int 0x80
    jmp shell_prompt

.run_err_bad_header:
    push bx
    push si
    mov bx, dbg_tag
    mov si, dbg_run_err_header
    mov ah, SYS_DBG_PRINT
    int 0x80
    pop si
    pop bx
    mov si, msg_run_bad_header
    mov ah, SYS_PRINT_STRING
    int 0x80
    jmp shell_prompt

; =============================================================================
; run_parse_filename — Parse and convert filename to uppercase 8.3 format
;
; Input:  SI = pointer to filename string in cmd_buf (after "run ")
; Output: run_fname_buf filled with 11-byte space-padded uppercase name
;         run_args_ptr set to point to args (or NUL if none)
;         CF clear = success, CF set = error (empty or invalid)
;
; Handles: "HELLO.MNX", "hello.mnx", "test", etc.
; Converts lowercase to uppercase, pads with spaces, splits at '.'
; =============================================================================
run_parse_filename:
    push di
    push cx

    ; Default: no extension provided
    mov byte [run_ext_provided], 0

    ; Initialize fname buffer with spaces
    mov di, run_fname_buf
    mov cx, 11
    mov al, ' '
    rep stosb

    ; Start filling filename (up to 8 chars before dot)
    mov di, run_fname_buf
    xor cx, cx                      ; CX = char count in name part

.rpf_name_loop:
    mov al, [si]
    cmp al, 0                       ; End of string?
    je .rpf_done_no_ext
    cmp al, ' '                     ; Space = end of filename, args follow
    je .rpf_done_space
    cmp al, '.'                     ; Dot = start extension
    je .rpf_dot

    ; Uppercase conversion
    cmp al, 'a'
    jb .rpf_store_name
    cmp al, 'z'
    ja .rpf_store_name
    sub al, 32                      ; Convert to uppercase

.rpf_store_name:
    cmp cx, 8                       ; Max 8 chars in name
    jge .rpf_skip_name_char
    mov [di], al
    inc di
    inc cx
.rpf_skip_name_char:
    inc si
    jmp .rpf_name_loop

.rpf_dot:
    mov byte [run_ext_provided], 1
    inc si                          ; Skip the dot
    ; Fill extension part (bytes 8-10 of fname buffer)
    mov di, run_fname_buf + 8
    xor cx, cx

.rpf_ext_loop:
    mov al, [si]
    cmp al, 0
    je .rpf_done_no_args
    cmp al, ' '
    je .rpf_done_space

    ; Uppercase conversion
    cmp al, 'a'
    jb .rpf_store_ext
    cmp al, 'z'
    ja .rpf_store_ext
    sub al, 32

.rpf_store_ext:
    cmp cx, 3                       ; Max 3 chars in extension
    jge .rpf_skip_ext_char
    mov [di], al
    inc di
    inc cx
.rpf_skip_ext_char:
    inc si
    jmp .rpf_ext_loop

.rpf_done_space:
    ; SI points at the space; skip spaces to find args
    inc si
.rpf_skip_arg_spaces:
    cmp byte [si], ' '
    jne .rpf_set_args
    inc si
    jmp .rpf_skip_arg_spaces
.rpf_set_args:
    ; SI → first arg char (or NUL if just trailing spaces)
    mov [run_args_ptr], si
    clc
    pop cx
    pop di
    ret

.rpf_done_no_ext:
    ; No extension provided — leave extension as spaces (for FS_FIND_BASE)
    mov byte [run_ext_provided], 0
.rpf_done_no_args:
    ; No args — point args_ptr at an empty string (NUL)
    mov word [run_args_ptr], run_empty_args
    clc
    pop cx
    pop di
    ret
