; =============================================================================
; MNMON.MNX — MNOS16 Machine Monitor
;
; Interactive memory monitor with WinDbg-style commands:
;   db XXXX [YYYY]  — display bytes (hex + ASCII)
;   dw XXXX [YYYY]  — display words (16-bit)
;   eb XXXX BB ...  — enter (write) bytes
;   ew XXXX WWWW .. — enter (write) words
;   g XXXX          — go (call address, ret returns)
;   ?               — help
;   q               — quit to shell
;
; Relocatable user-mode executable (MNEX v2 format).
; The shell applies relocations at load time — binary portable across versions.
; Returns to shell via `ret`.
;
; Build: nasm -f bin -I src/include/ -o build/boot/mnmon.mnx src/programs/mnmon.asm
; Run:   mnos:\> mnmon
;
; See doc/MNMON.md for the full design specification.
; =============================================================================

%include "syscalls.inc"
%include "memory.inc"
%include "bib.inc"
%include "mnfs.inc"

[BITS 16]
%ifndef RELOC_BASE
%define RELOC_BASE 0
%endif
[ORG RELOC_BASE]

; =============================================================================
; ENTRY POINT
; =============================================================================
entry:
    ; Print banner
    mov si, str_banner
    call mn_print_string

    ; Initialize current address to 0
    mov word [mon_addr], 0

; =============================================================================
; MAIN LOOP — read command, dispatch
; =============================================================================
mon_loop:
    ; Print prompt "* "
    mov si, str_prompt
    call mn_print_string

    ; Read a line of input
    call mon_readline               ; mon_buf filled, CX = length

    ; Empty line → repeat last db (auto-increment)
    test cx, cx
    jz .cmd_db_next

    ; Parse first character for single-char commands
    mov si, mon_buf
    cmp byte [si], '?'
    je .cmd_help
    cmp byte [si], 'q'
    je .cmd_quit
    cmp byte [si], 'Q'
    je .cmd_quit

    ; Two-char command dispatch
    mov al, [si]
    or al, 0x20                     ; lowercase
    mov ah, [si+1]
    or ah, 0x20                     ; lowercase

    cmp al, 'd'
    jne .not_display
    cmp ah, 'b'
    je cmd_db
    cmp ah, 'w'
    je cmd_dw
    cmp ah, 'i'
    je cmd_dir
    jmp .cmd_unknown
.not_display:
    cmp al, 'e'
    jne .not_enter
    cmp ah, 'b'
    je cmd_eb
    cmp ah, 'w'
    je cmd_ew
    jmp .cmd_unknown
.not_enter:
    cmp al, 'g'
    jne .not_g
    ; 'g' command — skip past 'g' and parse address
    inc si
    call skip_spaces
    call parse_hex16
    mov [mon_addr], ax
    jmp cmd_go
.not_g:
    cmp al, 'x'
    jne .not_x
    ; 'x' command — exec another program
    inc si                          ; Skip 'x'
    call skip_spaces
    cmp byte [si], 0
    je .exec_no_args
    jmp cmd_exec
.not_x:
    ; Three-char commands: bib, ivt, mcb
    mov cl, [si+2]
    or cl, 0x20                     ; lowercase third char
    cmp al, 'b'
    jne .not_bib
    cmp ah, 'i'
    jne .not_bib
    cmp cl, 'b'
    je cmd_bib
.not_bib:
    cmp al, 'i'
    jne .not_ivt
    cmp ah, 'v'
    jne .not_ivt
    cmp cl, 't'
    je cmd_ivt
.not_ivt:
    cmp al, 'm'
    jne .cmd_unknown
    cmp ah, 'c'
    jne .cmd_unknown
    cmp cl, 'b'
    je cmd_mcb
    jmp .cmd_unknown

.cmd_unknown:
    mov si, str_err_cmd
    call mn_print_string
    jmp mon_loop

.exec_no_args:
    mov si, str_exec_usage
    call mn_print_string
    jmp mon_loop

.cmd_help:
    mov si, str_help
    call mn_print_string
    jmp mon_loop

.cmd_quit:
    mov si, str_crlf
    call mn_print_string
    ret                             ; Return to shell

; --- db with no args (Enter on empty line) ---
.cmd_db_next:
    jmp cmd_db_exec

; --- db command entry ---
cmd_db:
    add si, 2                       ; Skip "db"
    call skip_spaces
    cmp byte [si], 0                ; No args? Use current addr
    je cmd_db_exec
    call parse_hex16                ; AX = start address
    mov [mon_addr], ax
    call skip_spaces
    cmp byte [si], 0                ; Second arg (end addr)?
    je cmd_db_exec
    call parse_hex16                ; AX = end address
    mov [mon_end], ax
    jmp cmd_db_range

cmd_db_exec:
    ; Display 16 bytes at [mon_addr]
    mov ax, [mon_addr]
    mov bx, ax
    add bx, 15                      ; End = start + 15
    mov [mon_end], bx
    jmp cmd_db_range

cmd_db_range:
    ; Display bytes from [mon_addr] to [mon_end], 16 per line
    mov di, [mon_addr]
.db_line:
    cmp di, [mon_end]
    ja .db_done

    ; Print address prefix "XXXX: "
    mov dx, di
    call mn_print_hex16
    mov al, ':'
    call mn_print_char
    mov al, ' '
    call mn_print_char

    ; Save line start for ASCII column
    mov [mon_line_start], di

    ; Print 16 hex bytes (or fewer if range ends)
    mov cx, 16
.db_byte:
    cmp di, [mon_end]
    ja .db_pad

    mov al, [di]
    call mn_print_hex8

    ; Separator: '-' after byte 7, ' ' otherwise
    inc di
    mov ax, di
    sub ax, [mon_line_start]
    cmp ax, 8
    jne .db_space
    mov al, '-'
    call mn_print_char
    dec cx
    jmp .db_byte
.db_space:
    mov al, ' '
    call mn_print_char
    dec cx
    jmp .db_byte

.db_pad:
    ; Pad remaining positions with spaces (3 per byte)
    test cx, cx
    jz .db_ascii
    mov al, ' '
    call mn_print_char
    call mn_print_char
    call mn_print_char
    dec cx
    jmp .db_pad

.db_ascii:
    ; Print ASCII column: "  " + printable chars
    mov al, ' '
    call mn_print_char
    call mn_print_char

    ; Walk from line start to current DI
    mov si, [mon_line_start]

    mov cx, 16
.db_ascii_ch:
    cmp si, di                      ; DI = where hex stopped
    jae .db_ascii_end
    cmp si, [mon_end]
    ja .db_ascii_end

    mov al, [si]
    cmp al, 0x20
    jb .db_dot
    cmp al, 0x7E
    ja .db_dot
    jmp .db_print_ch
.db_dot:
    mov al, '.'
.db_print_ch:
    call mn_print_char
    inc si
    dec cx
    jmp .db_ascii_ch

.db_ascii_end:
    ; Newline
    mov si, str_crlf
    call mn_print_string
    jmp .db_line

.db_done:
    mov [mon_addr], di              ; Update current address
    jmp mon_loop

; =============================================================================
; DW — Display Words
; =============================================================================
cmd_dw:
    add si, 2                       ; Skip "dw"
    call skip_spaces
    cmp byte [si], 0
    je .dw_noaddr
    call parse_hex16
    mov [mon_addr], ax
    call skip_spaces
    cmp byte [si], 0
    je .dw_noend
    call parse_hex16
    mov [mon_end], ax
    jmp .dw_range
.dw_noaddr:
.dw_noend:
    ; Default: 16 bytes (8 words)
    mov ax, [mon_addr]
    add ax, 15
    mov [mon_end], ax

.dw_range:
    mov di, [mon_addr]
.dw_line:
    cmp di, [mon_end]
    ja .dw_done

    ; Address prefix
    mov dx, di
    call mn_print_hex16
    mov al, ':'
    call mn_print_char

    ; Print 8 words per line
    mov cx, 8
.dw_word:
    cmp di, [mon_end]
    ja .dw_eol
    test cx, cx
    jz .dw_eol

    mov al, ' '
    call mn_print_char

    ; Read word at [di] (little-endian) and print as 4 hex digits
    mov dx, [di]
    call mn_print_hex16

    add di, 2
    dec cx
    jmp .dw_word

.dw_eol:
    mov si, str_crlf
    call mn_print_string
    jmp .dw_line

.dw_done:
    mov [mon_addr], di
    jmp mon_loop

; =============================================================================
; EB — Enter Bytes
; =============================================================================
cmd_eb:
    add si, 2                       ; Skip "eb"
    call skip_spaces

    ; Must start with a hex digit (address required)
    mov al, [si]
    call is_hex_digit
    jc .eb_err                      ; Not hex → syntax error

    ; Parse address
    call parse_hex16
    mov [mon_addr], ax
    call skip_spaces

.eb_bytes:
    ; Parse and write bytes until end of line
    cmp byte [si], 0
    je .eb_done

    call parse_hex8
    jc .eb_done                     ; Not a hex digit — stop
    mov di, [mon_addr]
    mov [di], al
    inc word [mon_addr]
    call skip_spaces
    jmp .eb_bytes

.eb_err:
    mov si, str_err_syn
    call mn_print_string
    jmp mon_loop

.eb_done:
    jmp mon_loop

; =============================================================================
; EW — Enter Words
; =============================================================================
cmd_ew:
    add si, 2                       ; Skip "ew"
    call skip_spaces
    cmp byte [si], 0
    je .ew_err

    ; First arg is always address
    call parse_hex16
    mov [mon_addr], ax
    call skip_spaces

.ew_words:
    cmp byte [si], 0
    je .ew_done
    call parse_hex16                ; AX = word value
    mov di, [mon_addr]
    mov [di], ax                    ; Store little-endian
    add word [mon_addr], 2
    call skip_spaces
    jmp .ew_words

.ew_err:
    mov si, str_err_syn
    call mn_print_string
    jmp mon_loop

.ew_done:
    jmp mon_loop

; =============================================================================
; G — Go (call address)
; =============================================================================
cmd_go:
    mov ax, [mon_addr]
    mov [.go_addr], ax
    call [.go_addr]                 ; Near call — ret returns here
    jmp mon_loop

.go_addr: dw 0                     ; Filled at runtime

; =============================================================================
; X — Exec (overlay-launch another program via SYS_EXEC)
;
; Usage: x <name>      — executes NAME.MNX (replaces MNMON in memory)
;        x edit file   — executes EDIT.MNX with args "file"
;
; The program name is converted to 11-byte 8.3 format with "MNX" extension.
; Any text after the name is passed as the argument string.
; =============================================================================
cmd_exec:
    ; SI points past 'x ' to the program name
    ; Build 11-byte 8.3 filename in exec_fname
    mov di, exec_fname
    ; Fill with spaces
    push cx
    mov cx, 11
    mov al, ' '
    rep stosb
    pop cx
    mov di, exec_fname

    ; Copy name (up to 8 chars, uppercase, stop at space/NUL)
    xor cx, cx                      ; char count
.exec_copy_name:
    mov al, [si]
    cmp al, 0
    je .exec_pad_ext
    cmp al, ' '
    je .exec_got_name
    cmp al, '.'
    je .exec_got_dot
    ; Uppercase
    cmp al, 'a'
    jb .exec_store_name
    cmp al, 'z'
    ja .exec_store_name
    sub al, 32
.exec_store_name:
    cmp cx, 8
    jge .exec_skip_char
    mov [di], al
    inc di
    inc cx
.exec_skip_char:
    inc si
    jmp .exec_copy_name

.exec_got_dot:
    ; User provided extension — copy 3 chars
    inc si                          ; skip '.'
    mov di, exec_fname + 8
    mov cx, 3
.exec_copy_ext:
    mov al, [si]
    cmp al, 0
    je .exec_done_fname
    cmp al, ' '
    je .exec_done_fname
    cmp al, 'a'
    jb .exec_store_ext
    cmp al, 'z'
    ja .exec_store_ext
    sub al, 32
.exec_store_ext:
    mov [di], al
    inc di
    dec cx
    jz .exec_skip_ext
    inc si
    jmp .exec_copy_ext
.exec_skip_ext:
    inc si
    jmp .exec_done_fname

.exec_got_name:
    ; No dot — default extension is MNX
.exec_pad_ext:
    mov byte [exec_fname + 8], 'M'
    mov byte [exec_fname + 9], 'N'
    mov byte [exec_fname + 10], 'X'

.exec_done_fname:
    ; SI may point to args (past the filename + space)
    ; Skip spaces to find args
    cmp byte [si], 0
    je .exec_no_arg_str
.exec_skip_arg_spaces:
    cmp byte [si], ' '
    jne .exec_have_args
    inc si
    jmp .exec_skip_arg_spaces

.exec_have_args:
    cmp byte [si], 0
    je .exec_no_arg_str
    ; SI → args string (already NUL-terminated in mon_buf)
    mov di, si                      ; DI = args pointer
    jmp .exec_do_call

.exec_no_arg_str:
    xor di, di                      ; DI = 0 (no args)

.exec_do_call:
    ; Print what we're about to exec
    push di
    push si
    mov si, str_exec_run
    call mn_print_string
    mov si, exec_fname
    ; Print 8 chars of name (trim trailing spaces)
    mov cx, 8
.exec_print_name:
    lodsb
    cmp al, ' '
    je .exec_print_dot
    call mn_print_char
    dec cx
    jnz .exec_print_name
.exec_print_dot:
    mov al, '.'
    call mn_print_char
    ; Print extension
    mov si, exec_fname + 8
    mov cx, 3
.exec_print_ext:
    lodsb
    call mn_print_char
    dec cx
    jnz .exec_print_ext
    mov si, str_crlf
    call mn_print_string
    pop si
    pop di

    ; Call SYS_SPAWN (child runs, then MNMON is reloaded)
    mov si, exec_fname              ; DS:SI = child's 11-byte filename
    mov bx, mnmon_fname             ; DS:BX = our own filename (for reload)
    call mn_spawn

    ; Only reaches here on failure (CF=1, AX=error code)
    push ax
    mov si, str_exec_fail
    call mn_print_string
    pop dx
    call mn_print_dec16
    mov si, str_crlf
    call mn_print_string
    jmp mon_loop

; =============================================================================
; READLINE — Read a line into mon_buf
;   Output: mon_buf filled (NUL-terminated), CX = length
; =============================================================================
mon_readline:
    xor cx, cx                      ; Length counter
    mov di, mon_buf

.rl_key:
    call mn_read_key                  ; AL = ASCII, AH = scancode

    cmp al, 13                      ; Enter?
    je .rl_done

    cmp al, 8                       ; Backspace?
    je .rl_bs

    ; Printable character?
    cmp al, 0x20
    jb .rl_key                      ; Ignore non-printable

    ; Buffer full?
    cmp cx, 39                      ; Max 39 chars + NUL
    jge .rl_key

    ; Auto-lowercase
    cmp al, 'A'
    jb .rl_store
    cmp al, 'Z'
    ja .rl_store
    or al, 0x20

.rl_store:
    mov [di], al
    inc di
    inc cx

    ; Echo character
    call mn_print_char
    jmp .rl_key

.rl_bs:
    test cx, cx
    jz .rl_key                      ; Nothing to delete
    dec di
    dec cx

    ; Erase on screen: backspace + space + backspace
    mov al, 8
    call mn_print_char
    mov al, ' '
    call mn_print_char
    mov al, 8
    call mn_print_char
    jmp .rl_key

.rl_done:
    mov byte [di], 0                ; NUL terminate
    ; Print newline
    push cx
    mov si, str_crlf
    call mn_print_string
    pop cx
    ret

; =============================================================================
; PARSE_HEX16 — Parse 1-4 hex digits from [SI] into AX
;   Input:  SI = pointer to text
;   Output: AX = value, SI advanced past digits
; =============================================================================
parse_hex16:
    xor ax, ax
    push bx
.ph16_loop:
    mov bl, [si]
    call is_hex_char                ; Returns value in BL, CF=error
    jc .ph16_done
    shl ax, 4
    or al, bl
    inc si
    jmp .ph16_loop
.ph16_done:
    pop bx
    ret

; =============================================================================
; PARSE_HEX8 — Parse 1-2 hex digits from [SI] into AL
;   Input:  SI = pointer to text
;   Output: AL = value, SI advanced, CF set if no hex digit found
; =============================================================================
parse_hex8:
    push bx
    mov bl, [si]
    call is_hex_char
    jc .ph8_fail                    ; First char not hex
    mov al, bl
    inc si

    ; Try second digit
    mov bl, [si]
    call is_hex_char
    jc .ph8_one                     ; Only one digit
    shl al, 4
    or al, bl
    inc si
.ph8_one:
    clc
    pop bx
    ret
.ph8_fail:
    xor al, al
    stc
    pop bx
    ret

; =============================================================================
; IS_HEX_CHAR — Check if BL is a hex digit, return value
;   Input:  BL = character
;   Output: BL = 0-15 (value), CF clear if valid; CF set if not hex
; =============================================================================
is_hex_char:
    cmp bl, '0'
    jb .ihc_fail
    cmp bl, '9'
    jbe .ihc_09
    cmp bl, 'a'
    jb .ihc_fail
    cmp bl, 'f'
    ja .ihc_fail
    sub bl, 'a' - 10
    clc
    ret
.ihc_09:
    sub bl, '0'
    clc
    ret
.ihc_fail:
    stc
    ret

; =============================================================================
; IS_HEX_DIGIT — Check if AL is a hex digit (CF set = not hex)
; =============================================================================
is_hex_digit:
    cmp al, '0'
    jb .ihd_no
    cmp al, '9'
    jbe .ihd_yes
    cmp al, 'a'
    jb .ihd_no
    cmp al, 'f'
    jbe .ihd_yes
.ihd_no:
    stc
    ret
.ihd_yes:
    clc
    ret

; =============================================================================
; SKIP_SPACES — Advance SI past spaces
; =============================================================================
skip_spaces:
    cmp byte [si], ' '
    jne .ss_done
    inc si
    jmp skip_spaces
.ss_done:
    ret

; =============================================================================
; BIB — Display Boot Info Block (0x0600)
; =============================================================================
cmd_bib:
    mov si, str_bib_hdr
    call mn_print_string

    ; boot_drive
    mov si, str_bib_drv
    call mn_print_string
    mov al, [BIB_DRIVE]
    call mn_print_hex8
    mov si, str_crlf
    call mn_print_string

    ; a20_status
    mov si, str_bib_a20
    call mn_print_string
    mov al, [BIB_A20]
    call mn_print_hex8
    mov si, str_crlf
    call mn_print_string

    ; part_lba (4 bytes, show as two words: high:low)
    mov si, str_bib_lba
    call mn_print_string
    mov dx, [BIB_PART_LBA+2]
    call mn_print_hex16
    mov dx, [BIB_PART_LBA]
    call mn_print_hex16
    mov si, str_crlf
    call mn_print_string

    ; boot_mode
    mov si, str_bib_mode
    call mn_print_string
    mov al, [BIB_BOOT_MODE]
    call mn_print_hex8
    mov si, str_crlf
    call mn_print_string

    ; int_depth
    mov si, str_bib_int
    call mn_print_string
    mov al, [BIB_INT_DEPTH]
    call mn_print_hex8
    mov si, str_crlf
    call mn_print_string

    jmp mon_loop

; =============================================================================
; IVT — Display Interrupt Vector Table entries
;   Shows key vectors: 0x00-0x07 (CPU faults), 0x80-0x82 (OS syscalls)
; =============================================================================
cmd_ivt:
    ; CPU exception vectors 0x00–0x07
    mov si, str_ivt_cpu
    call mn_print_string
    xor bx, bx                     ; BX = vector number
    mov di, 0x0000                  ; DI = IVT offset (vec * 4)
.ivt_cpu:
    cmp bx, 8
    jge .ivt_os

    mov si, str_ivt_int
    call mn_print_string
    mov al, bl
    call mn_print_hex8
    mov si, str_ivt_sep
    call mn_print_string

    ; Print SEG:OFF (segment at [di+2], offset at [di])
    mov dx, [di+2]
    call mn_print_hex16
    mov al, ':'
    call mn_print_char
    mov dx, [di]
    call mn_print_hex16
    mov si, str_crlf
    call mn_print_string

    add di, 4
    inc bx
    jmp .ivt_cpu

.ivt_os:
    ; OS syscall vectors 0x80–0x82
    mov si, str_ivt_os
    call mn_print_string
    mov bx, 0x80
    mov di, 0x0200                  ; 0x80 * 4 = 0x200
.ivt_os_loop:
    cmp bx, 0x83
    jge .ivt_done

    mov si, str_ivt_int
    call mn_print_string
    mov al, bl
    call mn_print_hex8
    mov si, str_ivt_sep
    call mn_print_string

    mov dx, [di+2]
    call mn_print_hex16
    mov al, ':'
    call mn_print_char
    mov dx, [di]
    call mn_print_hex16
    mov si, str_crlf
    call mn_print_string

    add di, 4
    inc bx
    jmp .ivt_os_loop

.ivt_done:
    jmp mon_loop

; =============================================================================
; MCB — Walk heap MCB chain from HEAP_START
; =============================================================================
cmd_mcb:
    ; MEM_QUERY: AX=segment, BX=start, CX=size
    call mn_mem_query
    push cx                         ; Save size
    push bx                         ; Save start
    push ax                         ; Save segment

    ; Show heap segment BEFORE changing ES
    mov si, str_mcb_seg
    call mn_print_string
    pop ax                          ; Segment
    push ax                         ; Re-save
    mov dx, ax
    call mn_print_hex16
    mov si, str_crlf
    call mn_print_string

    ; Print column header
    mov si, str_mcb_hdr
    call mn_print_string

    ; Now set up ES:DI for walk
    pop ax                          ; Segment
    pop di                          ; Start offset
    pop cx                          ; Size
    add cx, di                      ; CX = heap end offset
    mov [.mcb_end], cx
    mov es, ax                      ; ES = heap segment

.mcb_loop:
    cmp di, [.mcb_end]
    jae .mcb_done

    ; Validate magic byte (via ES:)
    cmp byte [es:di + MCB_MAGIC_OFF], MCB_MAGIC
    jne .mcb_corrupt

    ; Print address
    mov dx, di
    call mn_print_hex16
    mov al, ' '
    call mn_print_char

    ; Print size
    mov dx, [es:di + MCB_SIZE_OFF]
    call mn_print_hex16
    mov al, ' '
    call mn_print_char

    ; Print status (Used/Free)
    test byte [es:di + MCB_FLAGS_OFF], MCB_FLAG_USED
    jz .mcb_free
    mov si, str_mcb_used
    jmp .mcb_stat
.mcb_free:
    mov si, str_mcb_free_s
.mcb_stat:
    call mn_print_string

    ; Print owner ID
    mov al, [es:di + MCB_FLAGS_OFF]
    and al, MCB_OWNER_MASK
    shr al, MCB_OWNER_SHIFT
    call mn_print_hex8
    mov si, str_crlf
    call mn_print_string

    ; Advance to next MCB
    add di, [es:di + MCB_SIZE_OFF]
    jmp .mcb_loop

.mcb_corrupt:
    mov si, str_mcb_bad
    call mn_print_string
.mcb_done:
    ; Restore ES to 0
    xor ax, ax
    mov es, ax
    jmp mon_loop

; Local variable for heap end offset
.mcb_end: dw 0

; =============================================================================
; DIR — Display MNFS directory (via INT 0x81 FS_LIST_FILES)
; =============================================================================
cmd_dir:
    ; Use FS_LIST_FILES: ES:BX = buffer → CL = count, buf = raw directory
    push es
    push ds
    pop es                          ; ES = DS = 0
    mov bx, mon_dir_buf
    call mn_list_files
    pop es

    mov si, str_dir_hdr
    call mn_print_string

    ; CL = file count from INT 0x81
    movzx cx, cl
    test cx, cx
    jz .dir_done

    ; Directory entries start at mon_dir_buf + MNFS_HDR_SIZE
    mov di, mon_dir_buf + MNFS_HDR_SIZE

.dir_entry:
    push cx

    ; Print 8.3 name (11 chars, insert '.' after 8)
    mov cx, 8
    push di
.dir_name:
    mov al, [di]
    cmp al, ' '
    je .dir_name_skip
    call mn_print_char
.dir_name_skip:
    inc di
    dec cx
    jnz .dir_name
    pop di

    ; Print dot separator
    mov al, '.'
    call mn_print_char

    ; Extension (3 chars)
    push di
    add di, 8
    mov cx, 3
.dir_ext:
    mov al, [di]
    cmp al, ' '
    je .dir_ext_skip
    call mn_print_char
.dir_ext_skip:
    inc di
    dec cx
    jnz .dir_ext
    pop di

    ; Spacing
    mov al, ' '
    call mn_print_char
    call mn_print_char

    ; Attr byte
    mov al, [di + MNFS_ENT_ATTR]
    call mn_print_hex8
    mov al, ' '
    call mn_print_char

    ; Start sector (4 bytes — show low word only for space)
    mov dx, [di + MNFS_ENT_START]
    call mn_print_hex16
    mov al, ' '
    call mn_print_char

    ; Sectors
    mov dx, [di + MNFS_ENT_SECTORS]
    call mn_print_hex16

    mov si, str_crlf
    call mn_print_string

    ; Next entry
    add di, MNFS_ENTRY_SIZE
    pop cx
    dec cx
    jnz .dir_entry

.dir_done:
    jmp mon_loop

; =============================================================================
; DATA
; =============================================================================
str_banner: db 13, 10, 'mnmon v1.1', 13, 10, 0
str_prompt: db '* ', 0
str_crlf:   db 13, 10, 0
str_err_cmd: db '^ Unknown command', 13, 10, 0
str_err_syn: db '^ Syntax error', 13, 10, 0
str_help:   db 'db [addr [end]]  Display bytes', 13, 10
            db 'dw [addr [end]]  Display words', 13, 10
            db 'eb addr bb ..    Enter bytes', 13, 10
            db 'ew addr ww ..    Enter words', 13, 10
            db 'g addr           Go (call addr)', 13, 10
            db 'x name [args]    Exec program', 13, 10
            db 'bib              Boot Info Block', 13, 10
            db 'ivt              Interrupt vectors', 13, 10
            db 'mcb              Heap MCB walk', 13, 10
            db 'dir              MNFS directory', 13, 10
            db '?                Help', 13, 10
            db 'q                Quit', 13, 10, 0

; --- BIB command strings ---
str_bib_hdr:  db '-- BIB (0600) --', 13, 10, 0
str_bib_drv:  db '  Drive:   ', 0
str_bib_a20:  db '  A20:     ', 0
str_bib_lba:  db '  PartLBA: ', 0
str_bib_mode: db '  Mode:    ', 0
str_bib_int:  db '  IntDep:  ', 0

; --- IVT command strings ---
str_ivt_cpu:  db '--- CPU Exceptions ---', 13, 10, 0
str_ivt_os:   db '--- OS Syscalls ---', 13, 10, 0
str_ivt_int:  db '  INT ', 0
str_ivt_sep:  db ': ', 0

; --- MCB command strings ---
str_mcb_hdr:    db 'Addr Size Stat Own', 13, 10, 0
str_mcb_seg:    db 'Seg: ', 0
str_mcb_used:   db 'USED ', 0
str_mcb_free_s: db 'FREE ', 0
str_mcb_bad:    db '^ Corrupt MCB (bad magic)', 13, 10, 0

; --- DIR command strings ---
str_dir_hdr:  db 'Name         At Start Sec', 13, 10, 0

; --- EXEC command strings ---
str_exec_usage: db 'Usage: x <name> [args]', 13, 10, 0
str_exec_run:   db 'Exec: ', 0
str_exec_fail:  db 'Exec failed, error: ', 0

; =============================================================================
; BSS (uninitialized data — zeroed by sector padding)
; =============================================================================
mon_addr:       dw 0                ; Current address (sticky)
mon_end:        dw 0                ; End address for range commands
mon_line_start: dw 0                ; Line start address (for ASCII column)
mon_buf:        times 40 db 0       ; Input line buffer
exec_fname:     times 11 db 0       ; 11-byte 8.3 filename for child
mnmon_fname:    db 'MNMON   MNX'    ; MNMON's own filename (for SYS_SPAWN reload)

; Buffer for FS_LIST_FILES — lives beyond loaded sectors in free TPA
mon_dir_buf     equ (USER_PROG_BASE + 4 * 512)  ; 0x9800

; =============================================================================
; PADDING — fill to 5 sectors (2560 bytes)

; =============================================================================
; LIBRARY — mnoslib wrappers (placed after entry: per MNOSLIB.md §2)
; =============================================================================
%include "mnoslib.inc"
