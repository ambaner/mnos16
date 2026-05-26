; =============================================================================
; SYSINFO.MNX — MNOS16 System Information Utility
;
; Displays 5 pages of hardware and system information:
;   Page 1: CPU (CPUID vendor, family, model, stepping, features, hypervisor)
;   Page 2: Memory (conventional, extended, E820 map)
;   Page 3: BIOS Data Area (COM/LPT ports, equipment, video mode, page size)
;   Page 4: Video & Disk (active mode, cursor, boot drive, geometry, EDD)
;   Page 5: IVT (first 8 interrupt vectors with names)
;
; All hardware queries go through INT 0x80 kernel syscalls.
; Press any key to advance between pages.
;
; Loaded into the Transient Program Area (TPA) at 0x9000.
; Returns to shell via SYS_EXIT.
;
; Build: nasm -f bin -I src/include/ -I src/programs/sysinfo/ -o build/boot/sysinfo.mnx src/programs/sysinfo/sysinfo.asm
; Run:   mnos:\> sysinfo
; =============================================================================

%include "syscalls.inc"
%include "memory.inc"
%include "bib.inc"

[BITS 16]
[ORG USER_PROG_BASE]                ; 0x9000

; =============================================================================
; MNEX HEADER (6 bytes)
; =============================================================================
            db 'MNEX'               ; Magic — user-mode executable
sysinfo_sectors:
            dw 6                    ; Size in sectors (3072 bytes)

; =============================================================================
; ENTRY POINT (offset 6)
; =============================================================================
entry:

%include "sysinfo_code.inc"

; =============================================================================
; DATA
; =============================================================================

%include "sysinfo_data.inc"

; =============================================================================
; PADDING — pad to exact sector boundary
; =============================================================================
times (6 * 512) - ($ - $$) db 0
