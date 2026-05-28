; =============================================================================
; disk.inc — Partition disk layout constants
;
; Defines the partition-relative sector offsets and maximum sizes for each
; binary in the boot chain.  These offsets are used by the VBR, LOADER, and
; KERNEL to locate and load the next stage from disk.
;
; Partition layout (sector offsets from partition start):
;   Offset 0:   VBR         (2 sectors  = 1 KB)
;   Offset 4:   LOADER.BIN  (up to 16 sectors = 8 KB)
;   Offset 20:  KERNEL.BIN  (up to 16 sectors = 8 KB)
;   Offset 36:  SHELL.BIN   (up to 32 sectors = 16 KB)
;
; See doc/DESIGN.md for the disk layout diagram.
; =============================================================================

LOADER_PART_OFF equ 4               ; LOADER.BIN starts at partition + 4
LOADER_MAX_SEC  equ 16              ; Maximum 16 sectors (8 KB)

KERNEL_PART_OFF equ 20              ; KERNEL.BIN starts at partition + 20
KERNEL_MAX_SEC  equ 16              ; Maximum 16 sectors (8 KB)

SHELL_PART_OFF  equ 36              ; SHELL.BIN starts at partition + 36
SHELL_MAX_SEC   equ 32              ; Maximum 32 sectors (16 KB)
