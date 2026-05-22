; =============================================================================
; FILE I/O
; =============================================================================

ed_cmd_save:
    cmp byte [filename_len], 0
    je ed_cmd_save_as               ; No filename — prompt for one
    call ed_save_file
    ret

ed_cmd_save_as:
    ; Save current filename for overwrite check
    push si
    push di
    push cx
    mov si, filename
    mov di, _prev_filename
    mov cx, 11
    rep movsb
    pop cx
    pop di
    pop si
    ; Show file picker for Save As (select existing or type at prompt)
    mov word [_fp_title_ptr], str_fp_save_title
    call ed_file_picker
    cmp byte [filename_len], 0
    je .sa_cancelled
    ; Check if file exists on disk (and is different from current)
    call ed_check_overwrite
    jc .sa_cancelled                ; User declined overwrite
    call ed_save_file
.sa_cancelled:
    ret

; -----------------------------------------------------------------------------
; ed_check_overwrite — If file exists on disk and differs from prev filename,
;                      ask user to confirm overwrite.
; Returns: CF=0 proceed with save, CF=1 user cancelled
; -----------------------------------------------------------------------------
ed_check_overwrite:
    ; Check if this is the same file we already had open (no warning needed)
    push si
    push di
    push cx
    mov si, filename
    mov di, _prev_filename
    mov cx, 11
    repe cmpsb
    pop cx
    pop di
    pop si
    je .ow_ok                       ; Same file — no warning

    ; Check if file exists on disk
    mov si, filename
    mov ah, FS_FIND_FILE
    int 0x81
    jc .ow_ok                       ; File doesn't exist — safe to save

    ; File exists — ask for confirmation
    mov si, str_msg_overwrite
    call ed_confirm_yn
    cmp al, 'Y'
    je .ow_ok
    stc
    ret
.ow_ok:
    clc
    ret

ed_cmd_open:
    ; Save current filename for "already open" check
    push si
    push di
    push cx
    mov si, filename
    mov di, _prev_filename
    mov cx, 11
    rep movsb
    pop cx
    pop di
    pop si
    ; Show file picker dialog, retry on file-not-found
.open_retry:
    mov word [_fp_title_ptr], str_fp_title
    call ed_file_picker
    cmp byte [filename_len], 0
    je .open_cancelled
    ; Check if same file is already open
    call ed_check_same_file
    jc .open_same
    call ed_load_file_from_name
    jnc .open_cancelled             ; Success — done
    ; File not found — show message box then retry
    mov si, str_msg_load_err
    call ed_msgbox
    call ed_draw_screen             ; Redraw before showing picker again
    jmp .open_retry
.open_same:
    mov si, str_msg_already_open
    call ed_msgbox
    call ed_draw_screen
    jmp .open_retry
.open_cancelled:
    ret

ed_cmd_new:
    ; Reinitialize editor (clear buffer)
    call ed_init
    call ed_draw_screen
    ret

; -----------------------------------------------------------------------------
; ed_check_same_file — Check if filename matches currently open file
; Returns: CF=1 if same file, CF=0 if different
; Uses _fp_input_buf (the just-selected/typed name in 8.3 format stored in filename)
; Actually compares the filename buffer against the previous filename stored in
; a temp copy. We compare the 11-byte name portion.
; -----------------------------------------------------------------------------
ed_check_same_file:
    push si
    push di
    push cx
    ; filename already has the NEW name (set by file picker)
    ; We need to compare against _prev_filename (saved before picker)
    mov si, filename
    mov di, _prev_filename
    mov cx, 11
    repe cmpsb
    jne .csf_different
    stc                             ; Same file
    jmp .csf_done
.csf_different:
    clc                             ; Different file
.csf_done:
    pop cx
    pop di
    pop si
    ret

; -----------------------------------------------------------------------------
; ed_load_file — Load file (filename from SI/SYS_GET_ARGV)
; Input: SI = pointer to filename string (e.g. "hello.txt"), CX = length
; Converts to 8.3 format then loads
; -----------------------------------------------------------------------------
ed_load_file:
    ; Copy argument to _fp_input_buf for 8.3 parsing
    push cx
    push si
    mov di, _fp_input_buf
    rep movsb
    mov byte [di], 0                ; NUL terminate
    pop si
    pop cx
    movzx ax, cl
    mov [_fp_input_len], cl

    ; Convert to 8.3 padded uppercase
    call ed_fp_parse_typed_name

    ; Fall through to load
    call ed_load_file_from_name
    ret

; -----------------------------------------------------------------------------
; ed_load_file_from_name — Load file using stored filename
; -----------------------------------------------------------------------------
ed_load_file_from_name:
    ; Set up FS_READ_FILE call
    ; SI = filename (8.3 padded)
    ; ES:BX = buffer
    ; Returns: CF=0 success, CF=1 failure
    mov si, filename
    mov bx, GAP_BUF_START           ; Load directly into buffer start
    mov ah, FS_READ_FILE
    int 0x81
    jc .load_fail

    ; AX = bytes read, DL = sectors
    ; Set up gap buffer: text is at start, gap is after text
    mov bx, GAP_BUF_START
    add bx, ax                      ; gap_start = start + bytes_read
    mov [gap_start], bx
    mov word [gap_end], GAP_BUF_END + 1  ; gap extends to end

    ; Count lines
    call ed_count_lines

    ; Reset cursor
    mov word [cursor_row], 0
    mov word [cursor_col], 0
    mov word [view_top], 0
    mov byte [modified], 0
    mov byte [sel_active], 0

    ; Sync gap to cursor (gap is at end of text after load, cursor is at 0,0)
    xor ax, ax
    call ed_gap_move_to

    call ed_draw_screen
    clc
    ret

.load_fail:
    stc
    ret

; -----------------------------------------------------------------------------
; ed_save_file — Save buffer to file using stored filename
; -----------------------------------------------------------------------------
ed_save_file:
    ; Flatten gap buffer: we need contiguous data
    ; Text is in two parts: [GAP_BUF_START..gap_start) and [gap_end..GAP_BUF_END+1)
    ; For FS_WRITE_FILE we need:
    ;   SI = filename (8.3 format)
    ;   ES:BX = data buffer
    ;   ECX = file size in bytes
    ;   DL = attributes (0)
    ;
    ; Strategy: move gap to end (so all text is contiguous at start)
    call ed_get_text_length         ; AX = total bytes
    push ax                         ; Save length

    ; Move gap to end of text
    call ed_gap_move_to             ; AX = offset to move to (= text length)

    ; Delete existing file first (FS_WRITE_FILE rejects duplicates)
    mov si, filename
    mov ah, FS_DELETE_FILE
    int 0x81
    ; Ignore error (file may not exist yet — new file)

    pop ax                          ; AX = file size
    movzx ecx, ax                   ; ECX = file size (zero-extended to 32-bit)
    mov si, filename
    mov bx, GAP_BUF_START           ; Data buffer
    xor dl, dl                      ; No special attributes
    mov ah, FS_WRITE_FILE
    int 0x81
    jc .save_fail

    mov byte [modified], 0
    call ed_msg_saved
    call ed_draw_status
    ret

.save_fail:
    call ed_msg_save_error
    ret
