; =============================================================================
; load_binary.inc — Shared MNEX binary loader subroutine
;
; This file is %included by VBR, LOADER, and KERNEL to provide a common
; binary-loading routine.  Each including file gets its own copy in the
; assembled output (unavoidable with flat binaries), but the source is
; maintained in one place.
;
; load_mnex — Load and verify an MNEX-format binary from the boot partition
;
; Reads a binary from a known partition-relative sector offset, verifies its
; 4-byte magic signature, validates the sector count from the header, and
; loads all sectors into memory.
;
; Input:
;   EAX = partition-relative sector offset (e.g., 4, 20, 36)
;   BX  = memory load offset (segment is always 0x0000)
;   ECX = expected 4-byte magic signature (e.g., 'MNLD', 'MNKN', 'MNEX')
;   DH  = maximum allowed sector count
;
; Output:
;   CF clear = success (binary loaded and verified)
;   CF set   = error (disk I/O failure, magic mismatch, or bad sector count)
;
; Clobbers: AX, CX, SI
; Preserves: BX, DX, DI, BP, ES, DS
;
; Requirements:
;   - The including file must define a DAP structure with these labels:
;       dap, dap_sectors, dap_buffer, dap_lba
;   - bib.inc must be included (provides BIB_DRIVE, BIB_PART_LBA)
; =============================================================================

load_mnex:
    push bp
    push di
    push dx

    ; --- Save parameters in local storage ------------------------------------
    mov [.lm_magic], ecx            ; Expected 4-byte magic
    movzx cx, dh                    ; CX = max sectors (zero-extended)
    mov [.lm_max], cx
    mov [.lm_off], bx               ; Load offset

    ; --- Calculate absolute LBA = partition_lba + relative offset ------------
    add eax, [BIB_PART_LBA]
    mov [.lm_lba], eax              ; Save for second read
    mov [dap_lba], eax

    ; --- First read: load sector 0 to inspect the binary header --------------
    mov word [dap_sectors], 1
    mov [dap_buffer], bx
    mov word [dap_buffer+2], 0x0000
    mov dl, [BIB_DRIVE]
    mov si, dap
    mov ah, 0x42
    int 0x13
    jc .lm_fail

    ; --- Verify the 4-byte magic at the load address -------------------------
    mov di, [.lm_off]
    mov eax, [di]                   ; Read first 4 bytes of loaded binary
    cmp eax, [.lm_magic]
    jne .lm_fail

    ; --- Read and validate sector count from header offset 4 -----------------
    mov cx, [di + 4]                ; CX = sector count from binary header
    test cx, cx
    jz .lm_fail                     ; Zero sectors = invalid
    cmp cx, [.lm_max]
    ja .lm_fail                     ; Exceeds maximum = invalid

    ; --- Second read: reload all sectors (including header) ------------------
    mov [dap_sectors], cx
    mov eax, [.lm_lba]
    mov [dap_lba], eax
    ; dap_buffer is unchanged from first read

    mov dl, [BIB_DRIVE]
    mov si, dap
    mov ah, 0x42
    int 0x13
    jc .lm_fail

    ; --- Success -------------------------------------------------------------
    pop dx
    pop di
    pop bp
    clc
    ret

.lm_fail:
    pop dx
    pop di
    pop bp
    stc
    ret

; --- Local storage for load_mnex (placed after ret, never executed) ----------
.lm_magic:  dd 0                    ; Expected magic signature
.lm_max:    dw 0                    ; Maximum sector count
.lm_off:    dw 0                    ; Memory load offset
.lm_lba:    dd 0                    ; Absolute LBA (saved for second read)
