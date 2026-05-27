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
; Relocatable user-mode executable (MNEX v2 format).
; The shell applies relocations at load time — binary portable across versions.
;
; Returns to shell via SYS_EXIT.
;
; Build: nasm -f bin -I src/include/ -I src/programs/sysinfo/ -o build/boot/sysinfo.mnx src/programs/sysinfo/sysinfo.asm
; Run:   mnos:\> sysinfo
; =============================================================================

%include "syscalls.inc"
%include "memory.inc"
%include "bib.inc"

[BITS 16]
%ifndef RELOC_BASE
%define RELOC_BASE 0
%endif
[ORG RELOC_BASE]

; =============================================================================
; ENTRY POINT
; =============================================================================
entry:

%include "sysinfo_code.inc"

; =============================================================================
; DATA
; =============================================================================

%include "sysinfo_data.inc"
