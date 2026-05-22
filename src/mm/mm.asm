; =============================================================================
; MNOS16 Memory Manager (MM.SYS) — MNMM Heap Allocator with HMA Support
;
; Loaded by KERNEL.SYS into memory at 0x2800.  Provides dynamic memory
; allocation services via INT 0x82 — fully decoupled from the kernel's
; INT 0x80 and the filesystem's INT 0x81.
;
; Architecture:
;   User mode (SHELL)  →  INT 0x82  →  MM.SYS  →  manages heap in HMA
;
; The heap resides in the High Memory Area (HMA): segment 0xFFFF, offsets
; 0x0010–0xFF00.  This gives ~65 KB of heap space, accessible from real
; mode because A20 is enabled at boot.  Physical addresses: 0x100000–0x10FEF0.
;
; If A20 is not functional, the heap is disabled (size=0, all allocations
; fail with CF set).  This allows the TPA to span 0x8000–0xF7FF (30 KB).
;
; MCB (Memory Control Block) structure is unchanged:
;   Offset 0: size   (word)  — total block size INCLUDING this header
;   Offset 2: flags  (byte)  — bit 0: 1=allocated, 0=free
;   Offset 3: magic  (byte)  — 'M' (0x4D) for integrity checking
;
; Allocation uses first-fit with forward coalescing on free.  All sizes
; are rounded up to the nearest even number (word-aligned).
;
; The heap is accessed via ES segment register (ES=HMA_SEG or ES=0).
; DS remains 0 at all times — interrupt-safe.  CLI is used around ES
; manipulation to prevent IRQ handlers from seeing a modified ES.
;
; API change from pre-HMA: MEM_ALLOC now returns AX=segment, BX=offset.
; Callers access allocated memory via ES:BX (mov es, ax; mov [es:bx], val).
;
; INT 0x82 functions (AH = function number):
;   0x01  MEM_ALLOC  — Allocate CX bytes → AX=seg, BX=offset, CF on error
;   0x02  MEM_FREE   — Free block at BX (segment from internal state) → CF err
;   0x03  MEM_AVAIL  — Query free memory → AX=largest, DX=total
;   0x04  MEM_INFO   — Heap statistics → AX=total, BX=used, CX=free, DX=blocks
;   0x05  MEM_QUERY  — Query heap location → AX=segment, BX=start, CX=size
;
; CF propagation: Handlers use `sti; retf 2` to preserve CF across iret,
; matching the kernel and FS syscall convention.
;
; See doc/MEMORY-MANAGER.md for the complete specification.
;
; Assembled with:  nasm -f bin -o mm.sys src/mm/mm.asm
; =============================================================================

%include "memory.inc"
%include "debug.inc"

[BITS 16]
[ORG MM_OFF]                         ; Loaded at 0x2800

; =============================================================================
; MM.SYS HEADER
; =============================================================================
mm_magic        db 'MNMM'           ; Magic identifier — memory manager
%ifdef DEBUG
mm_sectors      dw 3                 ; Module size in sectors (debug build)
%else
mm_sectors      dw 2                 ; Module size in sectors (release build)
%endif

; =============================================================================
; mm_init — Initialize the memory manager
;
; Called by the kernel after loading MM.SYS into memory.
;   1. Installs INT 0x82 handler in the IVT
;   2. Tests A20 gate with alias check
;   3. Initializes the heap (HMA if A20 works, conventional fallback)
;
; The heap segment and bounds are stored in mm_heap_seg/mm_heap_start/mm_heap_end
; so all handlers can reference them without conditional code paths.
;
; Input:  none
; Output: CF clear = success
; Clobbers: AX, BX, ES
; =============================================================================
mm_init:
    ; --- Install INT 0x82 handler in IVT ------------------------------------
    cli                              ; Disable interrupts while modifying IVT
    push es

    xor ax, ax
    mov es, ax                       ; ES = 0x0000 (IVT segment)

    ; Vector 0x82 is at 0x82 * 4 = 0x0208
    mov word [es:0x82*4],   mm_isr   ; Offset of our handler
    mov word [es:0x82*4+2], cs       ; Segment (same as our code segment)

    pop es
    sti                              ; Re-enable interrupts

    DBG "MM: INT 0x82 installed"

    ; --- A20 alias check ----------------------------------------------------
    ; If A20 is enabled, FFFF:0010 (physical 0x100000) and 0000:0000 (physical
    ; 0x00000) are DIFFERENT memory.  If A20 is disabled, they alias.
    cli
    push es
    push ds

    ; Save original values at both locations
    xor ax, ax
    mov ds, ax                       ; DS = 0x0000
    mov ax, 0xFFFF
    mov es, ax                       ; ES = 0xFFFF

    mov ax, [ds:0x0000]              ; Save original at 0000:0000
    push ax
    mov ax, [es:0x0010]              ; Save original at FFFF:0010
    push ax

    ; Write distinct values
    mov word [ds:0x0000], 0x1234     ; Write to 0000:0000
    mov word [es:0x0010], 0x5678     ; Write to FFFF:0010

    ; Check if 0000:0000 still holds 0x1234 (not overwritten by alias)
    cmp word [ds:0x0000], 0x1234
    jne .a20_failed                  ; They aliased — A20 is off

    ; Also verify FFFF:0010 holds its value
    cmp word [es:0x0010], 0x5678
    jne .a20_failed

    ; A20 is active — restore and use HMA
    pop ax
    mov [es:0x0010], ax              ; Restore FFFF:0010
    pop ax
    mov [ds:0x0000], ax              ; Restore 0000:0000

    pop ds
    pop es
    sti

    ; --- Configure for HMA heap ---------------------------------------------
    mov word [mm_heap_seg], HMA_SEG
    mov word [mm_heap_start], HMA_HEAP_START
    mov word [mm_heap_end], HMA_HEAP_END
    mov word [mm_heap_size], HMA_HEAP_SIZE

    DBG "MM: A20 OK, using HMA heap (64 KB)"

    jmp .init_heap

.a20_failed:
    ; Restore original values
    pop ax
    mov [es:0x0010], ax
    pop ax
    mov [ds:0x0000], ax

    pop ds
    pop es
    sti

    ; --- A20 failed: no heap available (TPA occupies conventional region) -----
    mov word [mm_heap_seg], 0x0000
    mov word [mm_heap_start], 0x0000
    mov word [mm_heap_end], 0x0000
    mov word [mm_heap_size], 0

    DBG "MM: A20 FAILED, no heap available"
    jmp .init_done

.init_heap:
    ; --- Initialize heap with single free block -----------------------------
    cli
    push es
    mov ax, [mm_heap_seg]
    mov es, ax
    mov bx, [mm_heap_start]

    mov ax, [mm_heap_size]
    mov word [es:bx + MCB_SIZE_OFF], ax        ; Size = entire heap
    mov byte [es:bx + MCB_FLAGS_OFF], 0x00     ; Free
    mov byte [es:bx + MCB_MAGIC_OFF], MCB_MAGIC ; 'M'

    pop es
    sti

    DBG "MM: heap initialized"

.init_done:
    clc                              ; Success
    ret

; =============================================================================
; mm_isr — INT 0x82 dispatcher
;
; Routes memory management syscalls based on AH function number using a
; jump table.  Invalid function numbers return CF set.
;
; All handlers return via `sti; retf 2` to preserve CF across iret.
; =============================================================================
mm_isr:
%ifdef DEBUG
    ; --- Syscall trace: log function number ----------------------------------
    push si
    push ax
    mov si, mm_trace_pfx            ; "[MM] AH="
    call serial_puts
    mov al, ah
    call serial_hex8
    call serial_crlf
    pop ax
    pop si
%endif

    ; --- Validate function number -------------------------------------------
    cmp ah, MEM_SYSCALL_MAX
    ja .mm_bad_func

    ; --- Save caller's BX, use AH as table index ---------------------------
    push si
    movzx si, ah                     ; SI = function number (1-based)
    dec si                           ; Convert to 0-based index
    shl si, 1                        ; SI = index * 2 (word table)
    jmp [cs:mm_table + si]           ; Jump to handler (CS: since we're in ISR)

.mm_bad_func:
    stc                              ; Invalid function
    sti
    retf 2

; Jump table for INT 0x82 functions (0-based)
mm_table:
    dw mm_alloc                      ; AH=0x01 → MEM_ALLOC
    dw mm_free                       ; AH=0x02 → MEM_FREE
    dw mm_avail                      ; AH=0x03 → MEM_AVAIL
    dw mm_info                       ; AH=0x04 → MEM_INFO
    dw mm_query                      ; AH=0x05 → MEM_QUERY

; =============================================================================
; mm_alloc — Allocate CX bytes from the heap
;
; Uses first-fit: walks the MCB chain via ES:[di], finds the first free
; block large enough, optionally splits it, marks it allocated.
;
; Input:  CX = requested size in bytes (must be > 0)
;         DL = owner ID (0-7, stored in MCB flags bits 1-3)
; Output: AX = heap segment (0xFFFF for HMA, 0x0000 for conventional)
;         BX = pointer to usable memory (offset past MCB header)
;         CF clear = success, CF set = failure (no block large enough)
; Clobbers: AX, BX
; Preserves: CX, DX, SI, DI, DS, ES
; =============================================================================
mm_alloc:
    ; --- Validate request ---------------------------------------------------
    test cx, cx
    jz .alloc_fail                   ; Reject zero-size allocation

    ; --- Round up to even (word-aligned) ------------------------------------
    mov ax, cx
    inc ax                           ; AX = CX + 1
    and ax, 0xFFFE                   ; Round up to even

    ; --- Calculate total block size needed ----------------------------------
    add ax, MCB_HDR_SIZE             ; AX = total block size needed

    ; Overflow check: if AX exceeds heap size, fail
    cmp ax, [cs:mm_heap_size]
    ja .alloc_fail

    ; Enforce minimum block size
    cmp ax, MCB_MIN_BLOCK
    jae .alloc_size_ok
    mov ax, MCB_MIN_BLOCK            ; At least 8 bytes (4 hdr + 4 payload)
.alloc_size_ok:

    ; AX = required block size (including header)
    ; Now walk the heap to find a free block >= AX

    push di
    push dx
    push es

    ; Set ES to heap segment (HMA or conventional)
    cli
    push word [cs:mm_heap_seg]
    pop es
    sti

    mov di, [cs:mm_heap_start]       ; DI = current block pointer (offset)

.alloc_walk:
    ; --- Check if we've gone past the heap ----------------------------------
    cmp di, [cs:mm_heap_end]
    jae .alloc_oom                   ; Walked past end → out of memory

    ; --- Validate MCB magic -------------------------------------------------
    cmp byte [es:di + MCB_MAGIC_OFF], MCB_MAGIC
    jne .alloc_oom                   ; Corrupted heap — treat as OOM

    ; --- Skip allocated blocks ----------------------------------------------
    test byte [es:di + MCB_FLAGS_OFF], MCB_FLAG_USED
    jnz .alloc_next

    ; --- Free block found — is it large enough? -----------------------------
    cmp [es:di + MCB_SIZE_OFF], ax
    jae .alloc_found                 ; This block is big enough

.alloc_next:
    ; Advance to next block: DI += block size
    add di, [es:di + MCB_SIZE_OFF]
    jmp .alloc_walk

.alloc_found:
    ; DI = pointer to free MCB that fits
    ; AX = required block size
    ; Check if we should split: remainder >= MCB_MIN_BLOCK?
    mov dx, [es:di + MCB_SIZE_OFF]   ; DX = current block size
    sub dx, ax                       ; DX = remainder after allocation
    cmp dx, MCB_MIN_BLOCK
    jb .alloc_no_split

    ; --- Split: create a new free block after the allocated portion ----------
    push bx
    mov bx, di
    add bx, ax                       ; BX = address of new free block

    ; Bounds check: new block must be within heap
    cmp bx, [cs:mm_heap_end]
    jae .alloc_no_split_pop

    mov [es:bx + MCB_SIZE_OFF], dx           ; Remainder size
    mov byte [es:bx + MCB_FLAGS_OFF], 0x00   ; Free
    mov byte [es:bx + MCB_MAGIC_OFF], MCB_MAGIC
    pop bx

    ; Update current block size to exactly what was requested
    mov [es:di + MCB_SIZE_OFF], ax
    jmp .alloc_mark

.alloc_no_split_pop:
    pop bx

.alloc_no_split:
    ; Use the entire block (no split — remainder too small)

.alloc_mark:
    ; --- Mark block as allocated with owner ID --------------------------------
    push cx
    mov cl, dl
    and cl, 0x07                     ; Mask to 3-bit owner (0-7)
    shl cl, MCB_OWNER_SHIFT          ; Shift into bits 1-3
    or  cl, MCB_FLAG_USED            ; Set allocated bit
    mov byte [es:di + MCB_FLAGS_OFF], cl
    pop cx

    ; --- Return segment:offset past the header ------------------------------
    mov bx, di
    add bx, MCB_HDR_SIZE             ; BX = usable memory offset
    mov ax, [cs:mm_heap_seg]         ; AX = heap segment

    pop es
    pop dx
    pop di
    pop si                           ; Restore SI saved by dispatcher

%ifdef DEBUG
    push si
    push ax
    mov si, mm_alloc_ok             ; "[MM] alloc sz="
    call serial_puts
    mov ax, cx
    call serial_hex16
    mov si, mm_ptr_eq               ; " ptr="
    call serial_puts
    mov ax, bx
    call serial_hex16
    mov si, mm_own_eq               ; " own="
    call serial_puts
    mov al, dl
    and al, 0x07
    add al, '0'
    call serial_putc
    call serial_crlf
    pop ax
    pop si
%endif

    clc                              ; Success
    sti
    retf 2

.alloc_oom:
    pop es
    pop dx
    pop di
.alloc_fail:
    pop si                           ; Restore SI saved by dispatcher

%ifdef DEBUG
    push si
    push ax
    mov si, mm_alloc_fail_msg       ; "[MM] alloc FAIL sz="
    call serial_puts
    mov ax, cx
    call serial_hex16
    call serial_crlf
    pop ax
    pop si
%endif

    stc                              ; Out of memory
    sti
    retf 2

; =============================================================================
; mm_free — Free a previously allocated block
;
; Validates the pointer, marks the block as free, then coalesces with the
; next block if it is also free (forward coalescing).
;
; Input:  BX = offset returned by MEM_ALLOC (points past MCB header)
; Output: CF clear = success, CF set = error (invalid pointer)
; Clobbers: AX
; Preserves: BX, CX, DX, SI, DI, DS, ES
; =============================================================================
mm_free:
    ; --- Validate pointer range ---------------------------------------------
    cmp bx, [cs:mm_heap_start]
    jb .free_fail                    ; Below heap start
    add bx, 0                       ; (no-op, clarity)
    cmp bx, [cs:mm_heap_end]
    jae .free_fail                   ; Above heap

    ; Pointer must be past MCB header
    push ax
    mov ax, [cs:mm_heap_start]
    add ax, MCB_HDR_SIZE
    cmp bx, ax
    pop ax
    jb .free_fail                    ; Not past header

    ; --- Set up ES for heap access ------------------------------------------
    push di
    push es

    cli
    push word [cs:mm_heap_seg]
    pop es
    sti

    ; --- Step back to the MCB header ----------------------------------------
    mov di, bx
    sub di, MCB_HDR_SIZE             ; DI = MCB header offset

    ; --- Validate MCB magic -------------------------------------------------
    cmp byte [es:di + MCB_MAGIC_OFF], MCB_MAGIC
    jne .free_fail_es                ; Not a valid MCB

    ; --- Check that block is currently allocated ----------------------------
    test byte [es:di + MCB_FLAGS_OFF], MCB_FLAG_USED
    jz .free_fail_es                 ; Already free (double free)

    ; --- Mark as free -------------------------------------------------------
    mov byte [es:di + MCB_FLAGS_OFF], 0x00

    ; --- Forward coalesce: merge with next block if free --------------------
    push ax
    push dx

.free_coalesce:
    mov ax, [es:di + MCB_SIZE_OFF]   ; AX = current block size
    mov dx, di
    add dx, ax                       ; DX = next block offset

    ; Check bounds
    cmp dx, [cs:mm_heap_end]
    jae .free_done                   ; At end of heap — nothing to merge

    ; Check next block's magic
    push bx
    mov bx, dx
    cmp byte [es:bx + MCB_MAGIC_OFF], MCB_MAGIC
    jne .free_coalesce_end           ; Next block corrupted — stop

    ; Check if next block is free
    test byte [es:bx + MCB_FLAGS_OFF], MCB_FLAG_USED
    jnz .free_coalesce_end           ; Next block is allocated — stop

    ; --- Merge: absorb next block's size into current -----------------------
    mov ax, [es:bx + MCB_SIZE_OFF]   ; AX = next block size
    add [es:di + MCB_SIZE_OFF], ax   ; Current size += next size

    ; Invalidate the absorbed block's magic
    mov byte [es:bx + MCB_MAGIC_OFF], 0x00

    pop bx
    jmp .free_coalesce               ; Check for more adjacent free blocks

.free_coalesce_end:
    pop bx

.free_done:
    pop dx
    pop ax
    pop es
    pop di
    pop si                           ; Restore SI saved by dispatcher

%ifdef DEBUG
    push si
    push ax
    mov si, mm_free_ok              ; "[MM] free ptr="
    call serial_puts
    mov ax, bx
    call serial_hex16
    call serial_crlf
    pop ax
    pop si
%endif

    clc                              ; Success
    sti
    retf 2

.free_fail_es:
    pop es
    pop di
.free_fail:
    pop si                           ; Restore SI saved by dispatcher

%ifdef DEBUG
    push si
    push ax
    mov si, mm_free_fail_msg        ; "[MM] free FAIL ptr="
    call serial_puts
    mov ax, bx
    call serial_hex16
    call serial_crlf
    pop ax
    pop si
%endif

    stc                              ; Error
    sti
    retf 2

; =============================================================================
; mm_avail — Query available heap memory
;
; Walks the entire MCB chain and reports:
;   AX = largest contiguous free block (usable bytes, excluding header)
;   DX = total free bytes (usable, excluding headers)
;
; Input:  none
; Output: AX = largest free block usable bytes, DX = total free usable bytes
;         CF always clear
; Clobbers: AX, DX
; Preserves: BX, CX, SI, DI, DS, ES
; =============================================================================
mm_avail:
    push di
    push cx
    push es

    ; Set ES to heap segment
    cli
    push word [cs:mm_heap_seg]
    pop es
    sti

    xor ax, ax                       ; AX = largest free block (usable)
    xor dx, dx                       ; DX = total free (usable)
    mov di, [cs:mm_heap_start]

.avail_walk:
    cmp di, [cs:mm_heap_end]
    jae .avail_done

    cmp byte [es:di + MCB_MAGIC_OFF], MCB_MAGIC
    jne .avail_done                  ; Corrupted — stop walking

    ; Check if free
    test byte [es:di + MCB_FLAGS_OFF], MCB_FLAG_USED
    jnz .avail_next                  ; Skip allocated blocks

    ; Free block: usable = size - header
    mov cx, [es:di + MCB_SIZE_OFF]
    sub cx, MCB_HDR_SIZE             ; CX = usable bytes in this block
    add dx, cx                       ; Total free += usable

    ; Update largest
    cmp cx, ax
    jbe .avail_next
    mov ax, cx                       ; New largest

.avail_next:
    add di, [es:di + MCB_SIZE_OFF]
    jmp .avail_walk

.avail_done:
    pop es
    pop cx
    pop di
    pop si                           ; Restore SI saved by dispatcher
    clc
    sti
    retf 2

; =============================================================================
; mm_info — Query heap statistics
;
; Walks the MCB chain and reports:
;   AX = total heap size
;   BX = bytes used (allocated blocks, including headers)
;   CX = bytes free (free blocks, including headers)
;   DX = total block count
;
; Input:  none
; Output: AX, BX, CX, DX as above.  CF always clear.
; Clobbers: AX, BX, CX, DX
; Preserves: SI, DI, DS, ES
; =============================================================================
mm_info:
    push di
    push es

    ; Set ES to heap segment
    cli
    push word [cs:mm_heap_seg]
    pop es
    sti

    mov ax, [cs:mm_heap_size]        ; AX = total heap size
    xor bx, bx                      ; BX = bytes used
    xor cx, cx                       ; CX = bytes free
    xor dx, dx                       ; DX = block count
    mov di, [cs:mm_heap_start]

.info_walk:
    cmp di, [cs:mm_heap_end]
    jae .info_done

    cmp byte [es:di + MCB_MAGIC_OFF], MCB_MAGIC
    jne .info_done                   ; Corrupted — stop

    inc dx                           ; Count this block

    test byte [es:di + MCB_FLAGS_OFF], MCB_FLAG_USED
    jz .info_free

    ; Allocated block
    add bx, [es:di + MCB_SIZE_OFF]   ; Used += block size
    jmp .info_next

.info_free:
    add cx, [es:di + MCB_SIZE_OFF]   ; Free += block size

.info_next:
    add di, [es:di + MCB_SIZE_OFF]
    jmp .info_walk

.info_done:
    pop es
    pop di
    pop si                           ; Restore SI saved by dispatcher
    clc
    sti
    retf 2

; =============================================================================
; mm_query — Query heap location and configuration
;
; Returns information about where the heap lives so callers know which
; segment to use when accessing allocated memory.
;
; Input:  none
; Output: AX = heap segment (0xFFFF for HMA, 0x0000 for conventional)
;         BX = heap start offset
;         CX = heap total size in bytes
;         CF always clear
; Clobbers: AX, BX, CX
; Preserves: DX, SI, DI, DS, ES
; =============================================================================
mm_query:
    mov ax, [cs:mm_heap_seg]
    mov bx, [cs:mm_heap_start]
    mov cx, [cs:mm_heap_size]
    pop si                           ; Restore SI saved by dispatcher
    clc
    sti
    retf 2

; =============================================================================
; Heap configuration (set by mm_init based on A20 probe)
; =============================================================================
mm_heap_seg     dw 0                 ; Heap segment (0xFFFF=HMA, 0x0000=conventional)
mm_heap_start   dw 0                 ; Heap start offset within segment
mm_heap_end     dw 0                 ; Heap end offset (exclusive)
mm_heap_size    dw 0                 ; Total heap size in bytes

; =============================================================================
; Debug trace strings (debug build only)
; =============================================================================
%ifdef DEBUG
mm_trace_pfx       db '[MM] AH=', 0
mm_alloc_ok        db '[MM] alloc sz=', 0
mm_alloc_fail_msg  db '[MM] alloc FAIL sz=', 0
mm_free_ok         db '[MM] free ptr=', 0
mm_free_fail_msg   db '[MM] free FAIL ptr=', 0
mm_ptr_eq          db ' ptr=', 0
mm_own_eq          db ' own=', 0
%endif

; =============================================================================
; Serial I/O functions (debug build only)
; =============================================================================
%include "serial.inc"

; =============================================================================
; PADDING — fill to sector boundary
; =============================================================================
%ifdef DEBUG
times (3 * 512) - ($ - $$) db 0
%else
times (2 * 512) - ($ - $$) db 0
%endif
