; =============================================================================
; COMMAND: sysinfo
; Display 5 pages of system information, pausing between each page.
; All hardware queries go through INT 0x80 syscalls.
; =============================================================================
cmd_sysinfo:
    ; =========================================================================
    ; PAGE 1 — CPU Information (CPUID via syscall)
    ; =========================================================================
    mov ah, SYS_CLEAR_SCREEN        ; Clear screen for page 1
    int 0x80

    mov si, msg_page1_hdr
    mov ah, SYS_PRINT_STRING
    int 0x80

    ; --- Check CPUID support via kernel syscall ------------------------------
    mov ah, SYS_CHECK_CPUID         ; Returns AL=1 if CPUID supported
    int 0x80
    test al, al
    jz .no_cpuid

    ; --- CPUID leaf 0: Vendor string via SYS_CPUID ---------------------------
    ; Leaf number goes in EDI (kernel moves it to EAX before executing CPUID)
    xor edi, edi                    ; EDI = 0 (leaf 0: vendor string)
    mov ah, SYS_CPUID
    int 0x80
    ; Returns: EBX:EDX:ECX = vendor string (12 bytes)
    mov [cpuid_vendor], ebx
    mov [cpuid_vendor+4], edx
    mov [cpuid_vendor+8], ecx
    mov byte [cpuid_vendor+12], 0

    mov si, msg_cpu_vendor
    mov ah, SYS_PRINT_STRING
    int 0x80
    mov si, cpuid_vendor
    mov ah, SYS_PRINT_STRING
    int 0x80
    mov si, msg_crlf
    mov ah, SYS_PRINT_STRING
    int 0x80

    ; --- CPUID leaf 1: Version and feature flags -----------------------------
    mov edi, 1                      ; EDI = 1 (leaf 1: version info)
    mov ah, SYS_CPUID
    int 0x80
    ; Returns: EAX=version, EDX=feature flags, ECX=extended features
    mov [cpuid_ver], eax
    mov [cpuid_feat_edx], edx
    mov [cpuid_feat_ecx], ecx

    ; Print CPU family
    mov si, msg_cpu_family
    mov ah, SYS_PRINT_STRING
    int 0x80
    mov eax, [cpuid_ver]
    shr eax, 8
    and ax, 0x0F
    mov dx, ax
    mov ah, SYS_PRINT_DEC16
    int 0x80
    mov si, msg_crlf
    mov ah, SYS_PRINT_STRING
    int 0x80

    ; Print CPU model
    mov si, msg_cpu_model
    mov ah, SYS_PRINT_STRING
    int 0x80
    mov eax, [cpuid_ver]
    shr eax, 4
    and ax, 0x0F
    mov dx, ax
    mov ah, SYS_PRINT_DEC16
    int 0x80
    mov si, msg_crlf
    mov ah, SYS_PRINT_STRING
    int 0x80

    ; Print CPU stepping
    mov si, msg_cpu_step
    mov ah, SYS_PRINT_STRING
    int 0x80
    mov eax, [cpuid_ver]
    and ax, 0x0F
    mov dx, ax
    mov ah, SYS_PRINT_DEC16
    int 0x80
    mov si, msg_crlf
    mov ah, SYS_PRINT_STRING
    int 0x80

    ; --- Feature flags -------------------------------------------------------
    mov si, msg_cpu_feat
    mov ah, SYS_PRINT_STRING
    int 0x80

    mov edx, [cpuid_feat_edx]

    test edx, 1                     ; Bit 0: FPU
    jz .no_fpu
    mov si, msg_f_fpu
    mov ah, SYS_PRINT_STRING
    int 0x80
.no_fpu:
    test edx, (1<<4)                ; Bit 4: TSC
    jz .no_tsc
    mov si, msg_f_tsc
    mov ah, SYS_PRINT_STRING
    int 0x80
.no_tsc:
    test edx, (1<<5)                ; Bit 5: MSR
    jz .no_msr
    mov si, msg_f_msr
    mov ah, SYS_PRINT_STRING
    int 0x80
.no_msr:
    test edx, (1<<8)                ; Bit 8: CX8
    jz .no_cx8
    mov si, msg_f_cx8
    mov ah, SYS_PRINT_STRING
    int 0x80
.no_cx8:
    test edx, (1<<13)               ; Bit 13: PGE
    jz .no_pge
    mov si, msg_f_pge
    mov ah, SYS_PRINT_STRING
    int 0x80
.no_pge:
    test edx, (1<<15)               ; Bit 15: CMOV
    jz .no_cmov
    mov si, msg_f_cmov
    mov ah, SYS_PRINT_STRING
    int 0x80
.no_cmov:
    test edx, (1<<23)               ; Bit 23: MMX
    jz .no_mmx
    mov si, msg_f_mmx
    mov ah, SYS_PRINT_STRING
    int 0x80
.no_mmx:
    test edx, (1<<25)               ; Bit 25: SSE
    jz .no_sse
    mov si, msg_f_sse
    mov ah, SYS_PRINT_STRING
    int 0x80
.no_sse:
    test edx, (1<<26)               ; Bit 26: SSE2
    jz .no_sse2
    mov si, msg_f_sse2
    mov ah, SYS_PRINT_STRING
    int 0x80
.no_sse2:

    mov ecx, [cpuid_feat_ecx]

    test ecx, 1                     ; Bit 0: SSE3
    jz .no_sse3
    mov si, msg_f_sse3
    mov ah, SYS_PRINT_STRING
    int 0x80
.no_sse3:
    test ecx, (1<<19)               ; Bit 19: SSE4.1
    jz .no_sse41
    mov si, msg_f_sse41
    mov ah, SYS_PRINT_STRING
    int 0x80
.no_sse41:
    test ecx, (1<<20)               ; Bit 20: SSE4.2
    jz .no_sse42
    mov si, msg_f_sse42
    mov ah, SYS_PRINT_STRING
    int 0x80
.no_sse42:

    mov si, msg_crlf
    mov ah, SYS_PRINT_STRING
    int 0x80

    ; --- Hypervisor detection ------------------------------------------------
    mov ecx, [cpuid_feat_ecx]
    test ecx, (1<<31)               ; Bit 31: Hypervisor present
    jz .no_hypervisor

    mov si, msg_hv_yes
    mov ah, SYS_PRINT_STRING
    int 0x80

    ; CPUID leaf 0x40000000: hypervisor vendor string via syscall
    mov edi, 0x40000000             ; EDI = hypervisor info leaf
    mov ah, SYS_CPUID
    int 0x80
    mov [cpuid_vendor], ebx
    mov [cpuid_vendor+4], ecx
    mov [cpuid_vendor+8], edx
    mov byte [cpuid_vendor+12], 0

    mov si, msg_hv_vendor
    mov ah, SYS_PRINT_STRING
    int 0x80
    mov si, cpuid_vendor
    mov ah, SYS_PRINT_STRING
    int 0x80
    mov si, msg_crlf
    mov ah, SYS_PRINT_STRING
    int 0x80
    jmp .cpuid_done

.no_hypervisor:
    mov si, msg_hv_no
    mov ah, SYS_PRINT_STRING
    int 0x80
    jmp .cpuid_done

.no_cpuid:
    mov si, msg_no_cpuid
    mov ah, SYS_PRINT_STRING
    int 0x80

.cpuid_done:
    ; Wait for keypress, then clear screen for next page
    mov ah, SYS_WAIT_KEY
    int 0x80

    ; =========================================================================
    ; PAGE 2 — Memory
    ; =========================================================================
    mov si, msg_page2_hdr
    mov ah, SYS_PRINT_STRING
    int 0x80

    ; Conventional memory via syscall
    mov si, msg_conv_mem
    mov ah, SYS_PRINT_STRING
    int 0x80
    mov ah, SYS_GET_CONV_MEM        ; Returns AX = conventional KB
    int 0x80
    mov dx, ax
    mov ah, SYS_PRINT_DEC16
    int 0x80
    mov si, msg_kb
    mov ah, SYS_PRINT_STRING
    int 0x80

    ; Extended memory via syscall
    mov si, msg_ext_mem
    mov ah, SYS_PRINT_STRING
    int 0x80
    mov ah, SYS_GET_EXT_MEM         ; Returns AX = extended KB, CF=err
    int 0x80
    jc .no_ext
    mov dx, ax
    mov ah, SYS_PRINT_DEC16
    int 0x80
    mov si, msg_kb
    mov ah, SYS_PRINT_STRING
    int 0x80
    jmp .e820_start
.no_ext:
    mov si, msg_na
    mov ah, SYS_PRINT_STRING
    int 0x80

.e820_start:
    ; E820 memory map via SYS_GET_E820
    mov si, msg_e820_hdr
    mov ah, SYS_PRINT_STRING
    int 0x80

    xor ebx, ebx                    ; Start E820 enumeration
    mov di, e820_buf

.e820_loop:
    mov ah, SYS_GET_E820            ; EBX=continuation, ES:DI=buffer
    int 0x80

    jc .e820_done
    push ebx

    ; Print base address (4 bytes)
    mov si, msg_e820_base
    mov ah, SYS_PRINT_STRING
    int 0x80
    mov al, [e820_buf+3]
    mov ah, SYS_PRINT_HEX8
    int 0x80
    mov al, [e820_buf+2]
    mov ah, SYS_PRINT_HEX8
    int 0x80
    mov al, [e820_buf+1]
    mov ah, SYS_PRINT_HEX8
    int 0x80
    mov al, [e820_buf]
    mov ah, SYS_PRINT_HEX8
    int 0x80

    ; Print length (4 bytes at offset 8)
    mov si, msg_e820_len
    mov ah, SYS_PRINT_STRING
    int 0x80
    mov al, [e820_buf+11]
    mov ah, SYS_PRINT_HEX8
    int 0x80
    mov al, [e820_buf+10]
    mov ah, SYS_PRINT_HEX8
    int 0x80
    mov al, [e820_buf+9]
    mov ah, SYS_PRINT_HEX8
    int 0x80
    mov al, [e820_buf+8]
    mov ah, SYS_PRINT_HEX8
    int 0x80

    ; Print type number and name
    mov si, msg_e820_type
    mov ah, SYS_PRINT_STRING
    int 0x80
    mov al, [e820_buf+16]
    add al, '0'
    mov ah, SYS_PRINT_CHAR
    int 0x80

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
    mov ah, SYS_PRINT_STRING
    int 0x80

    pop ebx
    test ebx, ebx
    jnz .e820_loop

.e820_done:
    mov ah, SYS_WAIT_KEY            ; Wait for keypress, clear screen
    int 0x80

    ; =========================================================================
    ; PAGE 3 — BIOS Data Area (via SYS_GET_BDA_BYTE / SYS_GET_BDA_WORD)
    ;
    ; Instead of reading memory at 0x0400+ directly, we use kernel syscalls
    ; that safely read BDA values for us.
    ; =========================================================================
    mov si, msg_page3_hdr
    mov ah, SYS_PRINT_STRING
    int 0x80

    ; --- COM ports (BDA 0x0400, 0x0402, 0x0404, 0x0406) ----------------------
    mov si, msg_com_hdr
    mov ah, SYS_PRINT_STRING
    int 0x80

    mov cx, 4                       ; 4 COM ports
    mov word [bda_offset], 0x0400   ; Starting BDA offset for COM1

.com_loop:
    push cx
    mov si, msg_indent
    mov ah, SYS_PRINT_STRING
    int 0x80

    ; Print "COM" label
    mov al, 'C'
    mov ah, SYS_PRINT_CHAR
    int 0x80
    mov al, 'O'
    mov ah, SYS_PRINT_CHAR
    int 0x80
    mov al, 'M'
    mov ah, SYS_PRINT_CHAR
    int 0x80

    ; Print port number (5 - cx = 1,2,3,4)
    mov al, 5
    pop cx
    push cx
    sub al, cl
    add al, '0'
    mov ah, SYS_PRINT_CHAR
    int 0x80
    mov al, ':'
    mov ah, SYS_PRINT_CHAR
    int 0x80
    mov al, ' '
    mov ah, SYS_PRINT_CHAR
    int 0x80

    ; Read COM port address from BDA via syscall
    mov bx, [bda_offset]
    mov ah, SYS_GET_BDA_WORD        ; BX=BDA offset; Returns AX=word
    int 0x80
    test ax, ax
    jz .com_none

    ; Port present — print its I/O address as hex
    mov dx, ax
    mov ah, SYS_PRINT_HEX16
    int 0x80
    mov si, msg_crlf
    mov ah, SYS_PRINT_STRING
    int 0x80
    jmp .com_next

.com_none:
    mov si, msg_not_present
    mov ah, SYS_PRINT_STRING
    int 0x80

.com_next:
    add word [bda_offset], 2        ; Next COM port offset
    pop cx
    dec cx
    jnz .com_loop

    ; --- LPT ports (BDA 0x0408, 0x040A, 0x040C) -----------------------------
    mov si, msg_lpt_hdr
    mov ah, SYS_PRINT_STRING
    int 0x80

    mov cx, 3                       ; 3 LPT ports
    mov word [bda_offset], 0x0408   ; Starting BDA offset for LPT1

.lpt_loop:
    push cx
    mov si, msg_indent
    mov ah, SYS_PRINT_STRING
    int 0x80

    ; Print "LPT" label
    mov al, 'L'
    mov ah, SYS_PRINT_CHAR
    int 0x80
    mov al, 'P'
    mov ah, SYS_PRINT_CHAR
    int 0x80
    mov al, 'T'
    mov ah, SYS_PRINT_CHAR
    int 0x80

    ; Print port number (4 - cx = 1,2,3)
    mov al, 4
    pop cx
    push cx
    sub al, cl
    add al, '0'
    mov ah, SYS_PRINT_CHAR
    int 0x80
    mov al, ':'
    mov ah, SYS_PRINT_CHAR
    int 0x80
    mov al, ' '
    mov ah, SYS_PRINT_CHAR
    int 0x80

    ; Read LPT port address from BDA via syscall
    mov bx, [bda_offset]
    mov ah, SYS_GET_BDA_WORD        ; BX=BDA offset; Returns AX=word
    int 0x80
    test ax, ax
    jz .lpt_none

    mov dx, ax
    mov ah, SYS_PRINT_HEX16
    int 0x80
    mov si, msg_crlf
    mov ah, SYS_PRINT_STRING
    int 0x80
    jmp .lpt_next

.lpt_none:
    mov si, msg_not_present
    mov ah, SYS_PRINT_STRING
    int 0x80

.lpt_next:
    add word [bda_offset], 2
    pop cx
    dec cx
    jnz .lpt_loop

    ; --- Equipment word via SYS_GET_EQUIP ------------------------------------
    mov si, msg_equip
    mov ah, SYS_PRINT_STRING
    int 0x80
    mov ah, SYS_GET_EQUIP           ; Returns AX = equipment word
    int 0x80
    mov dx, ax
    mov ah, SYS_PRINT_HEX16
    int 0x80
    mov si, msg_crlf
    mov ah, SYS_PRINT_STRING
    int 0x80

    ; --- Video mode from BDA (offset 0x0449) via syscall ---------------------
    mov si, msg_vid_mode_bda
    mov ah, SYS_PRINT_STRING
    int 0x80
    mov bx, 0x0449                  ; BDA offset for current video mode
    mov ah, SYS_GET_BDA_BYTE        ; Returns AL = byte at BDA offset
    int 0x80
    mov ah, SYS_PRINT_HEX8          ; Print video mode as hex
    int 0x80
    mov si, msg_crlf
    mov ah, SYS_PRINT_STRING
    int 0x80

    ; --- Screen columns from BDA (offset 0x044A) ----------------------------
    mov si, msg_vid_cols
    mov ah, SYS_PRINT_STRING
    int 0x80
    mov bx, 0x044A                  ; BDA offset for screen columns
    mov ah, SYS_GET_BDA_WORD        ; Returns AX = word
    int 0x80
    mov dx, ax
    mov ah, SYS_PRINT_DEC16
    int 0x80
    mov si, msg_crlf
    mov ah, SYS_PRINT_STRING
    int 0x80

    ; --- Video page size from BDA (offset 0x044C) ----------------------------
    mov si, msg_vid_pagesz
    mov ah, SYS_PRINT_STRING
    int 0x80
    mov bx, 0x044C                  ; BDA offset for video page size
    mov ah, SYS_GET_BDA_WORD
    int 0x80
    mov dx, ax
    mov ah, SYS_PRINT_DEC16
    int 0x80
    mov si, msg_bytes
    mov ah, SYS_PRINT_STRING
    int 0x80

    mov ah, SYS_WAIT_KEY            ; Wait for keypress, clear screen
    int 0x80

    ; =========================================================================
    ; PAGE 4 — Video & Disk
    ; =========================================================================
    mov si, msg_page4_hdr
    mov ah, SYS_PRINT_STRING
    int 0x80

    ; --- Active video mode via SYS_GET_VIDEO ---------------------------------
    mov si, msg_vid_active
    mov ah, SYS_PRINT_STRING
    int 0x80
    mov ah, SYS_GET_VIDEO           ; Returns AL=mode, AH=cols, BH=page
    int 0x80
    push bx                         ; Save BH=display page
    push ax                         ; Save AL=video mode
    mov ah, SYS_PRINT_HEX8          ; Print video mode (AL) as hex
    int 0x80
    mov si, msg_crlf
    mov ah, SYS_PRINT_STRING
    int 0x80

    ; --- Active display page -------------------------------------------------
    mov si, msg_vid_page
    mov ah, SYS_PRINT_STRING
    int 0x80
    pop ax                          ; Restore saved AX (balance stack)
    pop bx                          ; BH = display page
    mov al, bh
    mov ah, SYS_PRINT_HEX8
    int 0x80
    mov si, msg_crlf
    mov ah, SYS_PRINT_STRING
    int 0x80

    ; --- Video memory base (determined from BDA video mode) ------------------
    mov si, msg_vid_base
    mov ah, SYS_PRINT_STRING
    int 0x80
    mov bx, 0x0449                  ; BDA offset for video mode
    mov ah, SYS_GET_BDA_BYTE
    int 0x80
    cmp al, 0x07                    ; Mode 7 = monochrome
    je .mono_base
    mov si, msg_b8000
    jmp .print_vbase
.mono_base:
    mov si, msg_b0000
.print_vbase:
    mov ah, SYS_PRINT_STRING
    int 0x80

    ; --- Cursor position via SYS_GET_CURSOR ----------------------------------
    mov si, msg_cursor
    mov ah, SYS_PRINT_STRING
    int 0x80
    mov ah, SYS_GET_CURSOR          ; Returns DH=row, DL=col
    int 0x80
    mov al, dh                      ; Print row
    mov ah, SYS_PRINT_HEX8
    int 0x80
    mov al, ','
    mov ah, SYS_PRINT_CHAR
    int 0x80
    mov al, dl                      ; Print column
    mov ah, SYS_PRINT_HEX8
    int 0x80
    mov si, msg_cursor_rc
    mov ah, SYS_PRINT_STRING
    int 0x80

    ; --- Boot drive info (from BIB via SYS_GET_BIB) --------------------------
    mov si, msg_boot_drv
    mov ah, SYS_PRINT_STRING
    int 0x80
    mov ah, SYS_GET_BIB             ; Returns ES:BX = BIB base
    int 0x80
    mov al, [es:bx]                 ; BIB+0 = boot drive number
    mov ah, SYS_PRINT_HEX8
    int 0x80
    mov si, msg_crlf
    mov ah, SYS_PRINT_STRING
    int 0x80

    ; --- Drive geometry via SYS_GET_DRIVE_INFO -------------------------------
    mov si, msg_drv_geom
    mov ah, SYS_PRINT_STRING
    int 0x80

    ; Get boot drive from BIB for the geometry query
    push es
    push bx
    mov ah, SYS_GET_BIB
    int 0x80
    mov dl, [es:bx]                 ; DL = boot drive number
    pop bx
    pop es

    mov ah, SYS_GET_DRIVE_INFO      ; Returns: CH,CL,DH,DL geometry; CF=err
    int 0x80
    jc .no_geom

    push dx                         ; Save DH=max head number

    ; Cylinders (CH = low 8 bits, CL bits 7-6 = high 2 bits)
    mov si, msg_drv_cyl
    mov ah, SYS_PRINT_STRING
    int 0x80
    mov al, cl
    shr al, 6                       ; High 2 bits of cylinder count
    mov ah, SYS_PRINT_HEX8
    int 0x80
    mov al, ch                      ; Low 8 bits of cylinder count
    mov ah, SYS_PRINT_HEX8
    int 0x80
    mov si, msg_crlf
    mov ah, SYS_PRINT_STRING
    int 0x80

    ; Sectors per track (CL bits 5-0)
    mov si, msg_drv_sec
    mov ah, SYS_PRINT_STRING
    int 0x80
    mov al, cl
    and al, 0x3F                    ; Mask to 6 bits = sectors/track
    movzx dx, al
    mov ah, SYS_PRINT_DEC16
    int 0x80
    mov si, msg_crlf
    mov ah, SYS_PRINT_STRING
    int 0x80

    ; Heads (DH = max head number, +1 = count)
    pop dx
    mov si, msg_drv_head
    mov ah, SYS_PRINT_STRING
    int 0x80
    mov al, dh
    inc al                          ; DH is max head index, +1 = total heads
    movzx dx, al
    mov ah, SYS_PRINT_DEC16
    int 0x80
    mov si, msg_crlf
    mov ah, SYS_PRINT_STRING
    int 0x80
    jmp .geom_done

.no_geom:
    mov si, msg_na
    mov ah, SYS_PRINT_STRING
    int 0x80

.geom_done:

    ; --- EDD support via SYS_GET_EDD -----------------------------------------
    ; Only basic EDD check (installation check).  Extended params skipped
    ; because there is no syscall for INT 13h AH=48h.
    mov si, msg_edd_hdr
    mov ah, SYS_PRINT_STRING
    int 0x80

    ; Get boot drive from BIB
    push es
    push bx
    mov ah, SYS_GET_BIB
    int 0x80
    mov dl, [es:bx]                 ; DL = boot drive
    pop bx
    pop es

    mov ah, SYS_GET_EDD             ; DL=drive; Returns BX,AH,CX; CF=err
    int 0x80
    jc .no_edd
    cmp bx, 0xAA55                  ; EDD signature check
    jne .no_edd

    ; EDD supported — print version (AH has EDD version from BIOS)
    push ax                         ; Save AH=EDD version
    mov si, msg_edd_ver
    mov ah, SYS_PRINT_STRING
    int 0x80
    pop ax
    mov al, ah                      ; EDD version byte
    mov ah, SYS_PRINT_HEX8
    int 0x80
    mov si, msg_crlf
    mov ah, SYS_PRINT_STRING
    int 0x80

    ; (Extended EDD params skipped — no syscall for INT 13h AH=48h)
    mov si, msg_edd_ext_skip
    mov ah, SYS_PRINT_STRING
    int 0x80
    jmp .edd_done

.no_edd:
    mov si, msg_edd_none
    mov ah, SYS_PRINT_STRING
    int 0x80

.edd_done:
    mov ah, SYS_WAIT_KEY
    int 0x80

    ; =========================================================================
    ; PAGE 5 — IVT Sample (Interrupt Vector Table via SYS_GET_IVT)
    ;
    ; Uses SYS_GET_IVT to read IVT entries instead of direct memory access.
    ; =========================================================================
    mov si, msg_page5_hdr
    mov ah, SYS_PRINT_STRING
    int 0x80

    mov byte [ivt_index], 0         ; Reset loop counter

.ivt_loop:
    cmp byte [ivt_index], 8
    jge .ivt_done

    mov si, msg_indent
    mov ah, SYS_PRINT_STRING
    int 0x80

    ; Print "INT " prefix
    mov al, 'I'
    mov ah, SYS_PRINT_CHAR
    int 0x80
    mov al, 'N'
    mov ah, SYS_PRINT_CHAR
    int 0x80
    mov al, 'T'
    mov ah, SYS_PRINT_CHAR
    int 0x80
    mov al, ' '
    mov ah, SYS_PRINT_CHAR
    int 0x80

    ; Print vector number as hex
    mov al, [ivt_index]
    mov ah, SYS_PRINT_HEX8
    int 0x80
    mov al, 'h'
    mov ah, SYS_PRINT_CHAR
    int 0x80
    mov al, ':'
    mov ah, SYS_PRINT_CHAR
    int 0x80
    mov al, ' '
    mov ah, SYS_PRINT_CHAR
    int 0x80

    ; Read IVT entry via SYS_GET_IVT
    mov cl, [ivt_index]             ; CL = vector number
    mov ah, SYS_GET_IVT             ; Returns AX=offset, DX=segment
    int 0x80
    push ax                         ; Save offset

    ; Print segment:offset — DX = segment from SYS_GET_IVT
    mov ah, SYS_PRINT_HEX16
    int 0x80
    mov al, ':'
    mov ah, SYS_PRINT_CHAR
    int 0x80
    pop dx                          ; Restore offset into DX
    mov ah, SYS_PRINT_HEX16
    int 0x80

    ; Print interrupt name from lookup table
    push bx
    xor bh, bh
    mov bl, [ivt_index]
    shl bx, 1                      ; Each entry is a word (2 bytes)
    mov si, [ivt_names_table + bx]
    mov ah, SYS_PRINT_STRING
    int 0x80
    pop bx

    inc byte [ivt_index]
    jmp .ivt_loop

.ivt_done:
    mov si, msg_sysinfo_done
    mov ah, SYS_PRINT_STRING
    int 0x80

    mov ah, SYS_WAIT_KEY
    int 0x80
    jmp shell_init

