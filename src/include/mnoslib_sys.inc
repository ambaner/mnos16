; =============================================================================
; mnoslib_sys.inc — User-mode wrappers for INT 0x80 system, process, and
; debug syscalls (everything in syscalls.inc that is not console/keyboard I/O).
;
; Pure 1:1 wrappers.  Preservation contract matches each underlying syscall.
;
; Dependencies: %include "syscalls.inc" before this file.
; =============================================================================

%ifndef MNOSLIB_SYS_INC_INCLUDED
%define MNOSLIB_SYS_INC_INCLUDED

; --- System info ------------------------------------------------------------

; mn_get_version — Returns OS version.
; Output: AH = major, AL = minor
mn_get_version:
    mov ah, SYS_GET_VERSION
    int 0x80
    ret

; mn_get_bib — Returns pointer to Boot Info Block.
; Output: ES:BX = BIB address
mn_get_bib:
    mov ah, SYS_GET_BIB
    int 0x80
    ret

; mn_get_equip — Returns BIOS equipment word.
; Output: AX = equipment flags
mn_get_equip:
    mov ah, SYS_GET_EQUIP
    int 0x80
    ret

; mn_get_video — Returns current video state.
; Output: AL = video mode, AH = column count, BH = active page
mn_get_video:
    mov ah, SYS_GET_VIDEO
    int 0x80
    ret

; mn_get_bda_byte — Read a byte from the BIOS Data Area.
; Input:  BX = BDA offset (0..0xFF)
; Output: AL = byte
mn_get_bda_byte:
    mov ah, SYS_GET_BDA_BYTE
    int 0x80
    ret

; mn_get_bda_word — Read a word from the BIOS Data Area.
; Input:  BX = BDA offset
; Output: AX = word
mn_get_bda_word:
    mov ah, SYS_GET_BDA_WORD
    int 0x80
    ret

; mn_check_a20 — Test whether the A20 line is enabled.
; Output: AL = 1 if enabled, 0 if not
mn_check_a20:
    mov ah, SYS_CHECK_A20
    int 0x80
    ret

; mn_get_conv_mem — Conventional memory size in KB.
; Output: AX = KB of conventional memory
mn_get_conv_mem:
    mov ah, SYS_GET_CONV_MEM
    int 0x80
    ret

; mn_get_ext_mem — Extended memory size in KB.
; Output: AX = KB of extended memory, CF=1 on error
mn_get_ext_mem:
    mov ah, SYS_GET_EXT_MEM
    int 0x80
    ret

; mn_get_e820 — Fetch one E820 memory map entry.
; Input:  EBX = continuation (0 to start), ES:DI = 20-byte buffer
; Output: EAX = signature ('SMAP' on success), EBX = next continuation,
;         ECX = entry size, CF=1 on error
mn_get_e820:
    mov ah, SYS_GET_E820
    int 0x80
    ret

; mn_get_drive_info — Query boot drive geometry.
; Output: drive geometry registers per BIOS INT 13h AH=08h; CF=1 on error
mn_get_drive_info:
    mov ah, SYS_GET_DRIVE_INFO
    int 0x80
    ret

; mn_get_edd — Query Enhanced Disk Drive info for the given drive.
; Input:  DL = drive
; Output: EDD info per BIOS INT 13h AH=48h; CF=1 on error
mn_get_edd:
    mov ah, SYS_GET_EDD
    int 0x80
    ret

; mn_get_ivt — Read an IVT vector.
; Input:  CL = vector number (0..0xFF)
; Output: AX = offset, DX = segment
mn_get_ivt:
    mov ah, SYS_GET_IVT
    int 0x80
    ret

; mn_check_cpuid — Test whether CPUID is supported.
; Output: AL = 1 if CPUID supported, 0 if not
mn_check_cpuid:
    mov ah, SYS_CHECK_CPUID
    int 0x80
    ret

; mn_cpuid — Execute CPUID for a given leaf.
; Input:  EDI = leaf number
; Output: EAX, EBX, ECX, EDX = CPUID results
mn_cpuid:
    mov ah, SYS_CPUID
    int 0x80
    ret

; mn_read_sector — Read raw disk sectors via the kernel.
; Input:  EDI = LBA, ES:BX = buffer, CL = sector count
; Output: CF=1 on error
mn_read_sector:
    mov ah, SYS_READ_SECTOR
    int 0x80
    ret

; --- Process control --------------------------------------------------------

; mn_reboot — Warm reboot the machine.  Does NOT return.
mn_reboot:
    mov ah, SYS_REBOOT
    int 0x80
    ret                                 ; unreachable

; mn_exit — Terminate the running program and return to the shell.
; Input:  AL = exit code (reserved; ignored today).  Does NOT return.
mn_exit:
    mov ah, SYS_EXIT
    int 0x80
    ret                                 ; unreachable

; mn_get_args — Get raw argument string (Layer 1).
; Output: SI = pointer to args (DS-relative), CX = length
mn_get_args:
    mov ah, SYS_GET_ARGS
    int 0x80
    ret

; mn_get_argc — Get parsed argument count (Layer 2).
; Output: CL = argc (0..15)
mn_get_argc:
    mov ah, SYS_GET_ARGC
    int 0x80
    ret

; mn_get_argv — Get one parsed argument by index (Layer 2).
; Input:  CL = index (0..argc-1)
; Output: SI = pointer to arg string (DS-relative), CX = length
mn_get_argv:
    mov ah, SYS_GET_ARGV
    int 0x80
    ret

; mn_exec — Replace the current program with another (chain).
; Input:  DS:SI = 11-byte filename, DS:DI = args (or 0 for none)
; Output: Does NOT return on success.  CF=1 on failure.
mn_exec:
    mov ah, SYS_EXEC
    int 0x80
    ret

; mn_spawn — Run another program, then reload and resume the caller.
; Input:  DS:SI = child filename, DS:DI = child args (or 0),
;         DS:BX = caller's 11-byte filename
; Output: CF=1 on failure.  On success the caller resumes after the child exits.
mn_spawn:
    mov ah, SYS_SPAWN
    int 0x80
    ret

; --- Debug syscalls (release-build no-ops) ----------------------------------

; mn_dbg_print — Print a tagged debug message to COM1 (debug builds only).
; Input:  DS:SI = NUL-terminated message, DS:BX = NUL-terminated tag (or 0)
mn_dbg_print:
    mov ah, SYS_DBG_PRINT
    int 0x80
    ret

; mn_dbg_hex16 — Print a tagged 16-bit hex value to COM1.
; Input:  DX = value, DS:BX = tag (or 0)
mn_dbg_hex16:
    mov ah, SYS_DBG_HEX16
    int 0x80
    ret

; mn_dbg_regs — Dump AX,BX,CX,DX,SI,DI with a tag to COM1.
; Input:  DS:BX = tag (or 0)
mn_dbg_regs:
    mov ah, SYS_DBG_REGS
    int 0x80
    ret

%endif ; MNOSLIB_SYS_INC_INCLUDED
