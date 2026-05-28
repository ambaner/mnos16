; =============================================================================
; stub_mm.asm — Test harness for MM allocator routines
;
; Provides entry points that call simplified versions of MM functions.
; The real MM.BIN uses an ISR dispatcher that pushes SI before jumping
; to handlers, which end with 'pop si; sti; retf 2'.  Here we strip
; that ISR framing and use plain near CALLs + RET.
;
; Entry points (at known offsets from ORG 0x1000):
;   0x1000 = mm_alloc_entry   — CX=size, DL=owner → BX=ptr, CF=error
;   0x1010 = mm_free_entry    — BX=ptr → CF=error
;   0x1020 = mm_avail_entry   — → AX=largest, DX=total
;   0x1030 = mm_info_entry    — → AX=total, BX=used, CX=free, DX=blocks
;   0x1040 = mm_init_heap     — initializes heap (single free block)
; =============================================================================

[BITS 16]
[ORG 0x1000]

%include "memory.inc"

; =============================================================================
; Entry: mm_alloc (offset 0x00 from ORG)
; =============================================================================
mm_alloc_entry:
    call mm_alloc_impl
    hlt

    times 0x10 - ($ - mm_alloc_entry) db 0x90   ; Pad to offset 0x10

; =============================================================================
; Entry: mm_free (offset 0x10 from ORG)
; =============================================================================
mm_free_entry:
    call mm_free_impl
    hlt

    times 0x10 - ($ - mm_free_entry) db 0x90    ; Pad to offset 0x20

; =============================================================================
; Entry: mm_avail (offset 0x20 from ORG)
; =============================================================================
mm_avail_entry:
    call mm_avail_impl
    hlt

    times 0x10 - ($ - mm_avail_entry) db 0x90   ; Pad to offset 0x30

; =============================================================================
; Entry: mm_info (offset 0x30 from ORG)
; =============================================================================
mm_info_entry:
    call mm_info_impl
    hlt

    times 0x10 - ($ - mm_info_entry) db 0x90    ; Pad to offset 0x40

; =============================================================================
; Entry: mm_init_heap (offset 0x40 from ORG)
;   Initializes the heap as a single free block (no IVT install)
; =============================================================================
mm_init_heap:
    mov bx, HEAP_START
    mov word [bx + MCB_SIZE_OFF], HEAP_SIZE
    mov byte [bx + MCB_FLAGS_OFF], 0x00
    mov byte [bx + MCB_MAGIC_OFF], MCB_MAGIC
    hlt

    times 0x50 - ($ - $$) db 0x90              ; Pad to offset 0x50

; =============================================================================
; mm_alloc_impl — First-fit allocator
; Input: CX=requested size, DL=owner ID (bits 0-2)
; Output: BX=pointer to usable memory, CF=0 success / CF=1 failure
; =============================================================================
mm_alloc_impl:
    ; --- Validate request ---
    test cx, cx
    jz .alloc_fail

    ; --- Round up to even (word-aligned) ---
    mov ax, cx
    inc ax
    and ax, 0xFFFE

    ; --- Calculate total block size needed ---
    add ax, MCB_HDR_SIZE
    cmp ax, HEAP_SIZE
    ja .alloc_fail

    ; Enforce minimum block size
    cmp ax, MCB_MIN_BLOCK
    jae .alloc_size_ok
    mov ax, MCB_MIN_BLOCK
.alloc_size_ok:

    push di
    push si
    mov di, HEAP_START

.alloc_walk:
    cmp di, HEAP_END
    jae .alloc_oom

    cmp byte [di + MCB_MAGIC_OFF], MCB_MAGIC
    jne .alloc_oom

    test byte [di + MCB_FLAGS_OFF], MCB_FLAG_USED
    jnz .alloc_next

    cmp [di + MCB_SIZE_OFF], ax
    jae .alloc_found

.alloc_next:
    add di, [di + MCB_SIZE_OFF]
    jmp .alloc_walk

.alloc_found:
    mov si, [di + MCB_SIZE_OFF]     ; SI = current block size
    sub si, ax                       ; SI = remainder
    cmp si, MCB_MIN_BLOCK
    jb .alloc_no_split

    ; Split: create new free block after allocated portion
    push bx
    mov bx, di
    add bx, ax
    mov [bx + MCB_SIZE_OFF], si
    mov byte [bx + MCB_FLAGS_OFF], 0x00
    mov byte [bx + MCB_MAGIC_OFF], MCB_MAGIC
    pop bx

    mov [di + MCB_SIZE_OFF], ax
    jmp .alloc_mark

.alloc_no_split:

.alloc_mark:
    ; Mark as allocated with owner from DL
    push cx
    mov cl, dl
    and cl, 0x07
    shl cl, MCB_OWNER_SHIFT
    or  cl, MCB_FLAG_USED
    mov byte [di + MCB_FLAGS_OFF], cl
    pop cx

    ; Return pointer past header
    mov bx, di
    add bx, MCB_HDR_SIZE

    pop si
    pop di
    clc
    ret

.alloc_oom:
    pop si
    pop di
.alloc_fail:
    stc
    ret

; =============================================================================
; mm_free_impl — Free a previously allocated block + forward coalesce
; Input: BX=pointer (to user data, not MCB header)
; Output: CF=0 success / CF=1 failure
; =============================================================================
mm_free_impl:
    ; --- Validate pointer range ---
    cmp bx, HEAP_START + MCB_HDR_SIZE
    jb .free_fail
    cmp bx, HEAP_END
    jae .free_fail

    ; Step back to MCB header
    push di
    mov di, bx
    sub di, MCB_HDR_SIZE

    ; Validate magic
    cmp byte [di + MCB_MAGIC_OFF], MCB_MAGIC
    jne .free_fail_di

    ; Check allocated
    test byte [di + MCB_FLAGS_OFF], MCB_FLAG_USED
    jz .free_fail_di

    ; Mark free
    mov byte [di + MCB_FLAGS_OFF], 0x00

    ; Forward coalesce
    push ax
    push si

.free_coalesce:
    mov ax, [di + MCB_SIZE_OFF]
    mov si, di
    add si, ax

    cmp si, HEAP_END
    jae .free_done

    cmp byte [si + MCB_MAGIC_OFF], MCB_MAGIC
    jne .free_done

    test byte [si + MCB_FLAGS_OFF], MCB_FLAG_USED
    jnz .free_done

    ; Merge: add next block's size to current
    mov ax, [si + MCB_SIZE_OFF]
    add [di + MCB_SIZE_OFF], ax
    mov byte [si + MCB_MAGIC_OFF], 0x00    ; Invalidate merged block
    jmp .free_coalesce

.free_done:
    pop si
    pop ax
    pop di
    clc
    ret

.free_fail_di:
    pop di
.free_fail:
    stc
    ret

; =============================================================================
; mm_avail_impl — Report available memory
; Output: AX=largest free block (usable bytes), DX=total free (usable bytes)
; =============================================================================
mm_avail_impl:
    push di
    push cx

    xor ax, ax          ; largest
    xor dx, dx          ; total
    mov di, HEAP_START

.avail_walk:
    cmp di, HEAP_END
    jae .avail_done

    cmp byte [di + MCB_MAGIC_OFF], MCB_MAGIC
    jne .avail_done

    test byte [di + MCB_FLAGS_OFF], MCB_FLAG_USED
    jnz .avail_next

    ; Free block: usable = size - header
    mov cx, [di + MCB_SIZE_OFF]
    sub cx, MCB_HDR_SIZE
    add dx, cx

    cmp cx, ax
    jbe .avail_next
    mov ax, cx

.avail_next:
    add di, [di + MCB_SIZE_OFF]
    jmp .avail_walk

.avail_done:
    pop cx
    pop di
    ret

; =============================================================================
; mm_info_impl — Report heap statistics
; Output: AX=total heap, BX=used bytes, CX=free bytes, DX=block count
; =============================================================================
mm_info_impl:
    push di

    mov ax, HEAP_SIZE
    xor bx, bx         ; used
    xor cx, cx          ; free
    xor dx, dx          ; block count
    mov di, HEAP_START

.info_walk:
    cmp di, HEAP_END
    jae .info_done

    cmp byte [di + MCB_MAGIC_OFF], MCB_MAGIC
    jne .info_done

    inc dx              ; count blocks

    test byte [di + MCB_FLAGS_OFF], MCB_FLAG_USED
    jz .info_free

    add bx, [di + MCB_SIZE_OFF]
    jmp .info_next

.info_free:
    add cx, [di + MCB_SIZE_OFF]

.info_next:
    add di, [di + MCB_SIZE_OFF]
    jmp .info_walk

.info_done:
    pop di
    ret
