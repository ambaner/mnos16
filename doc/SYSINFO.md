# SYSINFO.MNX — System Information Utility

## Overview

A standalone user program (6 sectors, 3 KB) that displays five pages of system
information, with "Press any key..." between each page and a screen clear before
each new page.  Run from the shell prompt by typing `sysinfo`.

## Pages

| Page | Title | Information |
|------|-------|-------------|
| 1 | CPU Information | CPUID vendor string, family/model/stepping, feature flags (FPU, TSC, MSR, CX8, PGE, CMOV, MMX, SSE/2/3/4.1/4.2), hypervisor detection + vendor |
| 2 | Memory | INT 12h conventional memory, INT 15h AH=88h extended memory, E820 memory map |
| 3 | BIOS Data Area | COM/LPT port addresses, equipment word, video mode, columns, page size |
| 4 | Video & Disk | Current video mode, cursor position, video memory base, boot drive geometry, EDD version/total sectors/bytes per sector |
| 5 | IVT Sample | First 8 interrupt vectors (INT 0-7) with descriptions |

## Source Structure

```
src/programs/sysinfo/
├── sysinfo.asm           # Entry point, MNEX v2 header (6 sectors)
├── sysinfo_code.inc      # Display logic (CPU, memory, BDA, disk, IVT)
└── sysinfo_data.inc      # Strings & runtime buffers
```

## CPUID Detection

The CPUID instruction (available on 486+) is detected by attempting to flip bit 21
(the ID flag) in EFLAGS.  If the bit toggles, CPUID is supported.  Leaf 0
returns the 12-byte vendor string; leaf 1 returns the CPU family, model, stepping,
and feature flags in EDX/ECX.  When the hypervisor-present flag (ECX bit 31) is
set, leaf 0x40000000 returns the hypervisor vendor string (e.g., "Microsoft Hv").

## EDD (Enhanced Disk Drive)

INT 13h AH=41h checks for EDD extension support.  If present, AH=48h returns
an extended parameter block with total sector count (64-bit) and bytes per sector,
providing more detail than the legacy CHS geometry from AH=08h.

## Technical Details

- **Format**: MNEX v2 relocatable binary
- **Load address**: TPA at 0x8000 (shell applies relocations)
- **Exit**: Returns to shell via `SYS_EXIT`
- **Syscalls used**: `SYS_CLEAR_SCREEN`, `SYS_PRINT_STRING`, `SYS_PRINT_CHAR`,
  `SYS_PRINT_HEX8`, `SYS_PRINT_HEX16`, `SYS_PRINT_DEC16`, `SYS_READ_KEY`,
  `SYS_GET_BIB`, `SYS_EXIT`

## Build

```
nasm -f bin -I src/include/ -I src/programs/sysinfo/ -o build/boot/sysinfo.mnx src/programs/sysinfo/sysinfo.asm
```
