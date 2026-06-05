; =============================================================================
; mnoslib_mm.inc — User-mode wrappers for INT 0x82 memory manager syscalls.
;
; Pure 1:1 wrappers.  See doc/MEMORY-MANAGER.md for HMA layout and ES:BX
; access pattern.
;
; Dependencies: %include "memory.inc" before this file (for MEM_* constants).
; =============================================================================

%ifndef MNOSLIB_MM_INC_INCLUDED
%define MNOSLIB_MM_INC_INCLUDED

; -----------------------------------------------------------------------------
; mn_alloc — Allocate a block from the HMA heap.
; Input:    CX = requested size in bytes, DL = owner ID (MCB_OWNER_*)
; Output:   AX = HMA_SEG (0xFFFF), BX = offset; access via ES:BX after mov es,ax
;           CF=1 on failure (out of memory or invalid size)
; -----------------------------------------------------------------------------
mn_alloc:
    mov ah, MEM_ALLOC
    int 0x82
    ret

; -----------------------------------------------------------------------------
; mn_free — Free a block previously returned by mn_alloc.
; Input:    BX = pointer (same offset returned by mn_alloc)
; Output:   CF=1 on failure (invalid pointer, double free, corruption)
; -----------------------------------------------------------------------------
mn_free:
    mov ah, MEM_FREE
    int 0x82
    ret

; -----------------------------------------------------------------------------
; mn_avail — Query free heap bytes.
; Output:   AX = largest contiguous free block, DX = total free bytes
; -----------------------------------------------------------------------------
mn_avail:
    mov ah, MEM_AVAIL
    int 0x82
    ret

; -----------------------------------------------------------------------------
; mn_mem_info — Query heap statistics.
; Output:   AX = total heap size, BX = bytes used,
;           CX = bytes free, DX = total block count
; -----------------------------------------------------------------------------
mn_mem_info:
    mov ah, MEM_INFO
    int 0x82
    ret

; -----------------------------------------------------------------------------
; mn_mem_query — Query heap location (segment + start + size).
; Output:   AX = heap segment (HMA_SEG or 0 for conv heap),
;           BX = heap start offset,
;           CX = heap size in bytes
; -----------------------------------------------------------------------------
mn_mem_query:
    mov ah, MEM_QUERY
    int 0x82
    ret

%endif ; MNOSLIB_MM_INC_INCLUDED
