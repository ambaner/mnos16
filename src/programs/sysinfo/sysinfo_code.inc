; =============================================================================
; SYSINFO CODE  5 pages of system information
; =============================================================================
    ; =========================================================================
    ; PAGE 1 — CPU Information (CPUID via syscall)
    ; =========================================================================
    call mn_clear_screen                  ; Clear screen for page 1

    mov si, msg_page1_hdr
    call mn_print_string

    ; --- Check CPUID support via kernel syscall ------------------------------
    call mn_check_cpuid                  ; Returns AL=1 if CPUID supported
    test al, al
    jz .no_cpuid

    ; --- CPUID leaf 0: Vendor string via SYS_CPUID ---------------------------
    ; Leaf number goes in EDI (kernel moves it to EAX before executing CPUID)
    xor edi, edi                    ; EDI = 0 (leaf 0: vendor string)
    call mn_cpuid
    ; Returns: EBX:EDX:ECX = vendor string (12 bytes)
    mov [cpuid_vendor], ebx
    mov [cpuid_vendor+4], edx
    mov [cpuid_vendor+8], ecx
    mov byte [cpuid_vendor+12], 0

    mov si, msg_cpu_vendor
    call mn_print_string
    mov si, cpuid_vendor
    call mn_print_string
    mov si, msg_crlf
    call mn_print_string

    ; --- CPUID leaf 1: Version and feature flags -----------------------------
    mov edi, 1                      ; EDI = 1 (leaf 1: version info)
    call mn_cpuid
    ; Returns: EAX=version, EDX=feature flags, ECX=extended features
    mov [cpuid_ver], eax
    mov [cpuid_feat_edx], edx
    mov [cpuid_feat_ecx], ecx

    ; Print CPU family
    mov si, msg_cpu_family
    call mn_print_string
    mov eax, [cpuid_ver]
    shr eax, 8
    and ax, 0x0F
    mov dx, ax
    call mn_print_dec16
    mov si, msg_crlf
    call mn_print_string

    ; Print CPU model
    mov si, msg_cpu_model
    call mn_print_string
    mov eax, [cpuid_ver]
    shr eax, 4
    and ax, 0x0F
    mov dx, ax
    call mn_print_dec16
    mov si, msg_crlf
    call mn_print_string

    ; Print CPU stepping
    mov si, msg_cpu_step
    call mn_print_string
    mov eax, [cpuid_ver]
    and ax, 0x0F
    mov dx, ax
    call mn_print_dec16
    mov si, msg_crlf
    call mn_print_string

    ; --- Feature flags -------------------------------------------------------
    mov si, msg_cpu_feat
    call mn_print_string

    mov edx, [cpuid_feat_edx]

    test edx, 1                     ; Bit 0: FPU
    jz .no_fpu
    mov si, msg_f_fpu
    call mn_print_string
.no_fpu:
    test edx, (1<<4)                ; Bit 4: TSC
    jz .no_tsc
    mov si, msg_f_tsc
    call mn_print_string
.no_tsc:
    test edx, (1<<5)                ; Bit 5: MSR
    jz .no_msr
    mov si, msg_f_msr
    call mn_print_string
.no_msr:
    test edx, (1<<8)                ; Bit 8: CX8
    jz .no_cx8
    mov si, msg_f_cx8
    call mn_print_string
.no_cx8:
    test edx, (1<<13)               ; Bit 13: PGE
    jz .no_pge
    mov si, msg_f_pge
    call mn_print_string
.no_pge:
    test edx, (1<<15)               ; Bit 15: CMOV
    jz .no_cmov
    mov si, msg_f_cmov
    call mn_print_string
.no_cmov:
    test edx, (1<<23)               ; Bit 23: MMX
    jz .no_mmx
    mov si, msg_f_mmx
    call mn_print_string
.no_mmx:
    test edx, (1<<25)               ; Bit 25: SSE
    jz .no_sse
    mov si, msg_f_sse
    call mn_print_string
.no_sse:
    test edx, (1<<26)               ; Bit 26: SSE2
    jz .no_sse2
    mov si, msg_f_sse2
    call mn_print_string
.no_sse2:

    mov ecx, [cpuid_feat_ecx]

    test ecx, 1                     ; Bit 0: SSE3
    jz .no_sse3
    mov si, msg_f_sse3
    call mn_print_string
.no_sse3:
    test ecx, (1<<19)               ; Bit 19: SSE4.1
    jz .no_sse41
    mov si, msg_f_sse41
    call mn_print_string
.no_sse41:
    test ecx, (1<<20)               ; Bit 20: SSE4.2
    jz .no_sse42
    mov si, msg_f_sse42
    call mn_print_string
.no_sse42:

    mov si, msg_crlf
    call mn_print_string

    ; --- Hypervisor detection ------------------------------------------------
    mov ecx, [cpuid_feat_ecx]
    test ecx, (1<<31)               ; Bit 31: Hypervisor present
    jz .no_hypervisor

    mov si, msg_hv_yes
    call mn_print_string

    ; CPUID leaf 0x40000000: hypervisor vendor string via syscall
    mov edi, 0x40000000             ; EDI = hypervisor info leaf
    call mn_cpuid
    mov [cpuid_vendor], ebx
    mov [cpuid_vendor+4], ecx
    mov [cpuid_vendor+8], edx
    mov byte [cpuid_vendor+12], 0

    mov si, msg_hv_vendor
    call mn_print_string
    mov si, cpuid_vendor
    call mn_print_string
    mov si, msg_crlf
    call mn_print_string
    jmp .cpuid_done

.no_hypervisor:
    mov si, msg_hv_no
    call mn_print_string
    jmp .cpuid_done

.no_cpuid:
    mov si, msg_no_cpuid
    call mn_print_string

.cpuid_done:
    ; Wait for keypress, then clear screen for next page
    call mn_wait_key

    ; =========================================================================
    ; PAGE 2 — Memory
    ; =========================================================================
    mov si, msg_page2_hdr
    call mn_print_string

    ; Conventional memory via syscall
    mov si, msg_conv_mem
    call mn_print_string
    call mn_get_conv_mem                  ; Returns AX = conventional KB
    mov dx, ax
    call mn_print_dec16
    mov si, msg_kb
    call mn_print_string

    ; Extended memory via syscall
    mov si, msg_ext_mem
    call mn_print_string
    call mn_get_ext_mem                  ; Returns AX = extended KB, CF=err
    jc .no_ext
    mov dx, ax
    call mn_print_dec16
    mov si, msg_kb
    call mn_print_string
    jmp .e820_start
.no_ext:
    mov si, msg_na
    call mn_print_string

.e820_start:
    ; E820 memory map via SYS_GET_E820
    mov si, msg_e820_hdr
    call mn_print_string

    xor ebx, ebx                    ; Start E820 enumeration
    mov di, e820_buf

.e820_loop:
    call mn_get_e820                  ; EBX=continuation, ES:DI=buffer

    jc .e820_done
    push ebx

    ; Print base address (4 bytes)
    mov si, msg_e820_base
    call mn_print_string
    mov al, [e820_buf+3]
    call mn_print_hex8
    mov al, [e820_buf+2]
    call mn_print_hex8
    mov al, [e820_buf+1]
    call mn_print_hex8
    mov al, [e820_buf]
    call mn_print_hex8

    ; Print length (4 bytes at offset 8)
    mov si, msg_e820_len
    call mn_print_string
    mov al, [e820_buf+11]
    call mn_print_hex8
    mov al, [e820_buf+10]
    call mn_print_hex8
    mov al, [e820_buf+9]
    call mn_print_hex8
    mov al, [e820_buf+8]
    call mn_print_hex8

    ; Print type number and name
    mov si, msg_e820_type
    call mn_print_string
    mov al, [e820_buf+16]
    add al, '0'
    call mn_print_char

    mov al, [e820_buf+16]
    mov si, msg_type_usable
    cmp al, 1
    je .print_type
    mov si, msg_type_reserved
    cmp al, 2
    je .print_type
    mov si, msg_type_acpi
    cmp al, 3
    je .print_type
    mov si, msg_type_nvs
    cmp al, 4
    je .print_type
    mov si, msg_type_bad
    cmp al, 5
    je .print_type
    mov si, msg_type_unknown
.print_type:
    call mn_print_string

    pop ebx
    test ebx, ebx
    jnz .e820_loop

.e820_done:
    call mn_wait_key                  ; Wait for keypress, clear screen

    ; =========================================================================
    ; PAGE 3 — BIOS Data Area (via SYS_GET_BDA_BYTE / SYS_GET_BDA_WORD)
    ;
    ; Instead of reading memory at 0x0400+ directly, we use kernel syscalls
    ; that safely read BDA values for us.
    ; =========================================================================
    mov si, msg_page3_hdr
    call mn_print_string

    ; --- COM ports (BDA 0x0400, 0x0402, 0x0404, 0x0406) ----------------------
    mov si, msg_com_hdr
    call mn_print_string

    mov cx, 4                       ; 4 COM ports
    mov word [bda_offset], 0x0400   ; Starting BDA offset for COM1

.com_loop:
    push cx
    mov si, msg_indent
    call mn_print_string

    ; Print "COM" label
    mov al, 'C'
    call mn_print_char
    mov al, 'O'
    call mn_print_char
    mov al, 'M'
    call mn_print_char

    ; Print port number (5 - cx = 1,2,3,4)
    mov al, 5
    pop cx
    push cx
    sub al, cl
    add al, '0'
    call mn_print_char
    mov al, ':'
    call mn_print_char
    mov al, ' '
    call mn_print_char

    ; Read COM port address from BDA via syscall
    mov bx, [bda_offset]
    call mn_get_bda_word                  ; BX=BDA offset; Returns AX=word
    test ax, ax
    jz .com_none

    ; Port present — print its I/O address as hex
    mov dx, ax
    call mn_print_hex16
    mov si, msg_crlf
    call mn_print_string
    jmp .com_next

.com_none:
    mov si, msg_not_present
    call mn_print_string

.com_next:
    add word [bda_offset], 2        ; Next COM port offset
    pop cx
    dec cx
    jnz .com_loop

    ; --- LPT ports (BDA 0x0408, 0x040A, 0x040C) -----------------------------
    mov si, msg_lpt_hdr
    call mn_print_string

    mov cx, 3                       ; 3 LPT ports
    mov word [bda_offset], 0x0408   ; Starting BDA offset for LPT1

.lpt_loop:
    push cx
    mov si, msg_indent
    call mn_print_string

    ; Print "LPT" label
    mov al, 'L'
    call mn_print_char
    mov al, 'P'
    call mn_print_char
    mov al, 'T'
    call mn_print_char

    ; Print port number (4 - cx = 1,2,3)
    mov al, 4
    pop cx
    push cx
    sub al, cl
    add al, '0'
    call mn_print_char
    mov al, ':'
    call mn_print_char
    mov al, ' '
    call mn_print_char

    ; Read LPT port address from BDA via syscall
    mov bx, [bda_offset]
    call mn_get_bda_word                  ; BX=BDA offset; Returns AX=word
    test ax, ax
    jz .lpt_none

    mov dx, ax
    call mn_print_hex16
    mov si, msg_crlf
    call mn_print_string
    jmp .lpt_next

.lpt_none:
    mov si, msg_not_present
    call mn_print_string

.lpt_next:
    add word [bda_offset], 2
    pop cx
    dec cx
    jnz .lpt_loop

    ; --- Equipment word via SYS_GET_EQUIP ------------------------------------
    mov si, msg_equip
    call mn_print_string
    call mn_get_equip                  ; Returns AX = equipment word
    mov dx, ax
    call mn_print_hex16
    mov si, msg_crlf
    call mn_print_string

    ; --- Video mode from BDA (offset 0x0449) via syscall ---------------------
    mov si, msg_vid_mode_bda
    call mn_print_string
    mov bx, 0x0449                  ; BDA offset for current video mode
    call mn_get_bda_byte                  ; Returns AL = byte at BDA offset
    call mn_print_hex8                  ; Print video mode as hex
    mov si, msg_crlf
    call mn_print_string

    ; --- Screen columns from BDA (offset 0x044A) ----------------------------
    mov si, msg_vid_cols
    call mn_print_string
    mov bx, 0x044A                  ; BDA offset for screen columns
    call mn_get_bda_word                  ; Returns AX = word
    mov dx, ax
    call mn_print_dec16
    mov si, msg_crlf
    call mn_print_string

    ; --- Video page size from BDA (offset 0x044C) ----------------------------
    mov si, msg_vid_pagesz
    call mn_print_string
    mov bx, 0x044C                  ; BDA offset for video page size
    call mn_get_bda_word
    mov dx, ax
    call mn_print_dec16
    mov si, msg_bytes
    call mn_print_string

    call mn_wait_key                  ; Wait for keypress, clear screen

    ; =========================================================================
    ; PAGE 4 — Video & Disk
    ; =========================================================================
    mov si, msg_page4_hdr
    call mn_print_string

    ; --- Active video mode via SYS_GET_VIDEO ---------------------------------
    mov si, msg_vid_active
    call mn_print_string
    call mn_get_video                  ; Returns AL=mode, AH=cols, BH=page
    push bx                         ; Save BH=display page
    push ax                         ; Save AL=video mode
    call mn_print_hex8                  ; Print video mode (AL) as hex
    mov si, msg_crlf
    call mn_print_string

    ; --- Active display page -------------------------------------------------
    mov si, msg_vid_page
    call mn_print_string
    pop ax                          ; Restore saved AX (balance stack)
    pop bx                          ; BH = display page
    mov al, bh
    call mn_print_hex8
    mov si, msg_crlf
    call mn_print_string

    ; --- Video memory base (determined from BDA video mode) ------------------
    mov si, msg_vid_base
    call mn_print_string
    mov bx, 0x0449                  ; BDA offset for video mode
    call mn_get_bda_byte
    cmp al, 0x07                    ; Mode 7 = monochrome
    je .mono_base
    mov si, msg_b8000
    jmp .print_vbase
.mono_base:
    mov si, msg_b0000
.print_vbase:
    call mn_print_string

    ; --- Cursor position via SYS_GET_CURSOR ----------------------------------
    mov si, msg_cursor
    call mn_print_string
    call mn_get_cursor                  ; Returns DH=row, DL=col
    mov al, dh                      ; Print row
    call mn_print_hex8
    mov al, ','
    call mn_print_char
    mov al, dl                      ; Print column
    call mn_print_hex8
    mov si, msg_cursor_rc
    call mn_print_string

    ; --- Boot drive info (from BIB via SYS_GET_BIB) --------------------------
    mov si, msg_boot_drv
    call mn_print_string
    call mn_get_bib                  ; Returns ES:BX = BIB base
    mov al, [es:bx]                 ; BIB+0 = boot drive number
    call mn_print_hex8
    mov si, msg_crlf
    call mn_print_string

    ; --- Drive geometry via SYS_GET_DRIVE_INFO -------------------------------
    mov si, msg_drv_geom
    call mn_print_string

    ; Get boot drive from BIB for the geometry query
    push es
    push bx
    call mn_get_bib
    mov dl, [es:bx]                 ; DL = boot drive number
    pop bx
    pop es

    call mn_get_drive_info                  ; Returns: CH,CL,DH,DL geometry; CF=err
    jc .no_geom

    push dx                         ; Save DH=max head number

    ; Cylinders (CH = low 8 bits, CL bits 7-6 = high 2 bits)
    mov si, msg_drv_cyl
    call mn_print_string
    mov al, cl
    shr al, 6                       ; High 2 bits of cylinder count
    call mn_print_hex8
    mov al, ch                      ; Low 8 bits of cylinder count
    call mn_print_hex8
    mov si, msg_crlf
    call mn_print_string

    ; Sectors per track (CL bits 5-0)
    mov si, msg_drv_sec
    call mn_print_string
    mov al, cl
    and al, 0x3F                    ; Mask to 6 bits = sectors/track
    movzx dx, al
    call mn_print_dec16
    mov si, msg_crlf
    call mn_print_string

    ; Heads (DH = max head number, +1 = count)
    pop dx
    mov si, msg_drv_head
    call mn_print_string
    mov al, dh
    inc al                          ; DH is max head index, +1 = total heads
    movzx dx, al
    call mn_print_dec16
    mov si, msg_crlf
    call mn_print_string
    jmp .geom_done

.no_geom:
    mov si, msg_na
    call mn_print_string

.geom_done:

    ; --- EDD support via SYS_GET_EDD -----------------------------------------
    ; Only basic EDD check (installation check).  Extended params skipped
    ; because there is no syscall for INT 13h AH=48h.
    mov si, msg_edd_hdr
    call mn_print_string

    ; Get boot drive from BIB
    push es
    push bx
    call mn_get_bib
    mov dl, [es:bx]                 ; DL = boot drive
    pop bx
    pop es

    call mn_get_edd                  ; DL=drive; Returns BX,AH,CX; CF=err
    jc .no_edd
    cmp bx, 0xAA55                  ; EDD signature check
    jne .no_edd

    ; EDD supported — print version (AH has EDD version from BIOS)
    push ax                         ; Save AH=EDD version
    mov si, msg_edd_ver
    call mn_print_string
    pop ax
    mov al, ah                      ; EDD version byte
    call mn_print_hex8
    mov si, msg_crlf
    call mn_print_string

    ; (Extended EDD params skipped — no syscall for INT 13h AH=48h)
    mov si, msg_edd_ext_skip
    call mn_print_string
    jmp .edd_done

.no_edd:
    mov si, msg_edd_none
    call mn_print_string

.edd_done:
    call mn_wait_key

    ; =========================================================================
    ; PAGE 5 — IVT Sample (Interrupt Vector Table via SYS_GET_IVT)
    ;
    ; Uses SYS_GET_IVT to read IVT entries instead of direct memory access.
    ; =========================================================================
    mov si, msg_page5_hdr
    call mn_print_string

    mov byte [ivt_index], 0         ; Reset loop counter

.ivt_loop:
    cmp byte [ivt_index], 8
    jge .ivt_done

    mov si, msg_indent
    call mn_print_string

    ; Print "INT " prefix
    mov al, 'I'
    call mn_print_char
    mov al, 'N'
    call mn_print_char
    mov al, 'T'
    call mn_print_char
    mov al, ' '
    call mn_print_char

    ; Print vector number as hex
    mov al, [ivt_index]
    call mn_print_hex8
    mov al, 'h'
    call mn_print_char
    mov al, ':'
    call mn_print_char
    mov al, ' '
    call mn_print_char

    ; Read IVT entry via SYS_GET_IVT
    mov cl, [ivt_index]             ; CL = vector number
    call mn_get_ivt                  ; Returns AX=offset, DX=segment
    push ax                         ; Save offset

    ; Print segment:offset — DX = segment from SYS_GET_IVT
    call mn_print_hex16
    mov al, ':'
    call mn_print_char
    pop dx                          ; Restore offset into DX
    call mn_print_hex16

    ; Print interrupt name from lookup table
    push bx
    xor bh, bh
    mov bl, [ivt_index]
    shl bx, 1                      ; Each entry is a word (2 bytes)
    mov si, [ivt_names_table + bx]
    call mn_print_string
    pop bx

    inc byte [ivt_index]
    jmp .ivt_loop

.ivt_done:
    mov si, msg_sysinfo_done
    call mn_print_string

    call mn_wait_key
    call mn_exit

