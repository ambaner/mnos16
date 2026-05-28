; =============================================================================
; MNOS16 Kernel (KERNEL.SYS) - 16-bit Real-Mode Kernel
;
; Loaded by LOADER.SYS into memory at 0x5000.  This is the first component
; in MNOS16 that acts as a proper kernel:
;
;   1. Installs a syscall handler at INT 0x80 in the IVT
;   2. Finds and loads FS.SYS (filesystem module) from the MNFS directory
;   3. Calls FS.SYS init to install INT 0x81
;   4. Finds and loads SHELL.SYS (user-mode executable) from the MNFS directory
;   5. Transfers control to the shell
;
; The shell and all user-mode programs interact with hardware exclusively
; through the INT 0x80 syscall interface.  Filesystem operations use INT 0x81
; (provided by FS.SYS).
;
; Syscall convention:
;   AH = function number
;   Other registers = function-specific arguments
;   Return: function-specific (AX = result, CF = error)
;
; The Boot Info Block (BIB) at 0x0600 is populated by earlier boot stages:
;   0x0600: boot_drive  (1 byte)  — BIOS drive number
;   0x0601: a20_status  (1 byte)  — A20 gate result (1=enabled, 0=failed)
;   0x0602: part_lba    (4 bytes) — partition start LBA
;
; Header layout (first 6 bytes):
;   Offset 0: 'MNKN'   Magic identifier (4 bytes)
;   Offset 4: dw N     Kernel size in sectors
;
; Assembled with:  nasm -f bin -o kernel.sys src/kernel/kernel.asm
; =============================================================================

%include "bib.inc"
%include "memory.inc"
%include "mnfs.inc"
%include "syscalls.inc"
%include "version.inc"
%define ASSERT_HAS_SCREEN
%include "debug.inc"

[BITS 16]
[ORG 0x5000]                        ; Loader loads us here

; =============================================================================
; KERNEL HEADER
; =============================================================================
kernel_magic    db 'MNKN'           ; Magic identifier — kernel
%ifdef DEBUG
kernel_sectors  dw 14               ; Kernel size in sectors (debug build)
%else
kernel_sectors  dw 8                ; Kernel size in sectors (release build)
%endif

; =============================================================================
; KERNEL ENTRY POINT
; =============================================================================
kernel_start:
%ifdef DEBUG
    call serial_init
    DBG "KERNEL: serial debug active"
%endif

    ; --- Install syscall handler at INT 0x80 ----------------------------------
    call install_syscalls

    mov si, msg_syscall
    call boot_ok
    DBG "KERNEL: INT 0x80 installed"

    ; --- Install CPU exception fault handlers --------------------------------
    call install_fault_handlers
    DBG "KERNEL: PIC remapped, fault handlers installed (INT 0x00-0x08)"

    ; --- Plant stack canary at 0x6C00 -----------------------------------------
    ; Must be done BEFORE loading FS/SHELL (which use significant stack).
    ; In release builds, CANARY_INIT expands to nothing (0 bytes).
    CANARY_INIT

    ; --- Initialize next_base for dynamic module placement --------------------
    mov word [next_base], MODULE_FIRST_BASE

    ; =========================================================================
    ; LOAD FS.SYS — first relocatable module
    ; =========================================================================
    ; Use DIR_SCRATCH_BUF for directory read (below kernel, above module area).
    mov bx, DIR_SCRATCH_BUF
    cmp byte [BIB_BOOT_MODE], 1
    je .use_fsd
    mov si, fname_fs                ; "FS      SYS"
    jmp .do_find_fs
.use_fsd:
    mov si, fname_fsd               ; "FSD     SYS"
.do_find_fs:
    call find_file
    jc .fs_find_fail

    ; EAX = partition-relative start sector, CX = size in sectors
    mov bx, [next_base]             ; Load at current next_base
    mov ecx, 'MNFS'                 ; Expected magic signature
    mov dh, 32                      ; Maximum sector count
    call load_mnex
    jc .fs_load_fail

    ; Apply relocations to FS.SYS
    mov di, [next_base]             ; DI = module load base
    call apply_relocs
    jc .fs_load_fail

    ; Advance next_base past FS.SYS
    mov di, [next_base]
    mov cx, [di + 4]                ; sector_count from header
    shl cx, 9                       ; cx *= 512
    add [next_base], cx

    mov si, msg_fs
    call boot_ok
    DBG "KERNEL: FS.SYS loaded and relocated"

    ; --- Initialize FS.SYS (installs INT 0x81) --------------------------------
    ; Entry point is at header's entry_offset field (offset 10 in v2 header).
    mov di, [next_base]
    sub di, cx                      ; DI = FS load base again
    mov ax, [di + 10]               ; entry_offset from v2 header
    add ax, di                      ; AX = absolute entry address
    call ax
    jc .fs_init_fail

    mov si, msg_fs_init
    call boot_ok
    DBG "KERNEL: INT 0x81 filesystem ready"

    ; =========================================================================
    ; LOAD MM.SYS — second relocatable module
    ; =========================================================================
    mov bx, DIR_SCRATCH_BUF
    cmp byte [BIB_BOOT_MODE], 1
    je .use_mmd
    mov si, fname_mm                ; "MM      SYS"
    jmp .do_find_mm
.use_mmd:
    mov si, fname_mmd               ; "MMD     SYS"
.do_find_mm:
    call find_file
    jc .mm_find_fail

    ; EAX = partition-relative start sector, CX = size in sectors
    mov bx, [next_base]             ; Load right after FS.SYS
    mov ecx, 'MNMM'                ; Expected magic signature
    mov dh, 32                      ; Maximum sector count
    call load_mnex
    jc .mm_load_fail

    ; Apply relocations to MM.SYS
    mov di, [next_base]             ; DI = module load base
    call apply_relocs
    jc .mm_load_fail

    ; Advance next_base past MM.SYS
    mov di, [next_base]
    mov cx, [di + 4]                ; sector_count from header
    shl cx, 9                       ; cx *= 512
    add [next_base], cx

    mov si, msg_mm
    call boot_ok
    DBG "KERNEL: MM.SYS loaded and relocated"

    ; --- Initialize MM.SYS (installs INT 0x82) --------------------------------
    mov di, [next_base]
    sub di, cx                      ; DI = MM load base
    mov ax, [di + 10]               ; entry_offset from v2 header
    add ax, di                      ; AX = absolute entry address
    call ax
    jc .mm_init_fail

    mov si, msg_mm_init
    call boot_ok
    DBG "KERNEL: INT 0x82 memory manager ready"

    ; =========================================================================
    ; LOAD SHELL.SYS — third relocatable module
    ; =========================================================================
    mov bx, DIR_SCRATCH_BUF
    cmp byte [BIB_BOOT_MODE], 1
    je .use_shelld
    mov si, fname_shell             ; "SHELL   SYS"
    jmp .do_find_shell
.use_shelld:
    mov si, fname_shelld            ; "SHELLD  SYS"
.do_find_shell:
    call find_file
    jc .shell_find_fail

    ; EAX = partition-relative start sector, CX = size in sectors
    mov bx, [next_base]             ; Load right after MM.SYS
    mov ecx, 'MNEX'                 ; Expected magic signature
    mov dh, 32                      ; Maximum sector count
    call load_mnex
    jc .shell_load_fail

    ; Validate: shell end must not overlap kernel
    mov cx, [bx + 4]               ; sector_count (BX still = load addr from load_mnex)
    shl cx, 9                       ; bytes
    mov ax, [next_base]
    add ax, cx
    cmp ax, KERNEL_OFF
    ja .shell_load_fail             ; Overlap! Fatal error

    ; Apply relocations to SHELL.SYS
    mov di, [next_base]
    call apply_relocs
    jc .shell_load_fail

    ; Get shell entry point
    mov di, [next_base]
    mov ax, [di + 10]               ; entry_offset from v2 header
    add ax, di                      ; AX = absolute entry address
    mov [.shell_entry], ax          ; Save for jump

    ; Advance next_base past SHELL.SYS (for canary placement)
    mov cx, [di + 4]                ; sector_count
    shl cx, 9
    add [next_base], cx

    mov si, msg_shell
    call boot_ok
    DBG "KERNEL: SHELL.SYS loaded, jumping to shell"

    ; --- Transfer control to shell --------------------------------------------
    jmp word [.shell_entry]

.fs_find_fail:
    mov si, msg_fs_find
    call boot_fail

.fs_load_fail:
    mov si, msg_fs_load
    call boot_fail

.fs_init_fail:
    mov si, msg_fs_initf
    call boot_fail

.shell_find_fail:
    mov si, msg_sh_find
    call boot_fail

.shell_load_fail:
    mov si, msg_sh_load
    call boot_fail

.mm_find_fail:
    mov si, msg_mm_find
    call boot_fail

.mm_load_fail:
    mov si, msg_mm_load
    call boot_fail

.mm_init_fail:
    mov si, msg_mm_initf
    call boot_fail

; Local storage
.shell_entry:   dw 0

; =============================================================================
; apply_relocs — Patch absolute references in a loaded MNEX v2 module
;
; Reads the relocation table from the module's v2 header and adds the load
; base address to each referenced 16-bit word in the module.
;
; Input:
;   DI = module load base address (also the value added to each reloc)
;
; Output:
;   CF clear = success (relocations applied, or no relocs needed)
;   CF set   = error (invalid header — flags indicate reloc but count is 0)
;
; Clobbers: AX, BX, CX, SI
; Preserves: DI, DX, BP, ES, DS
; =============================================================================
apply_relocs:
    ; Check flags field for MNEX_V2_FLAG_RELOC
    mov ax, [di + 6]               ; flags
    test ax, MNEX_V2_FLAG_RELOC
    jz .ar_done                    ; No relocations — nothing to do

    mov cx, [di + 8]               ; reloc_count
    test cx, cx
    jz .ar_fail                    ; Has flag but zero relocs = invalid

    ; SI = start of relocation table (at offset 12 from module base)
    lea si, [di + MNEX_V2_HDR_BASE]

.ar_patch_loop:
    lodsw                          ; AX = file-relative offset of word to patch
    mov bx, di
    add bx, ax                     ; BX = absolute address of the word
    add word [bx], di              ; Add load base to the value at that offset
    loop .ar_patch_loop

.ar_done:
    clc
    ret

.ar_fail:
    stc
    ret

; =============================================================================
; install_syscalls — Install the INT 0x80 handler into the IVT
;
; The IVT is a 256-entry array of far pointers at 0x0000:0x0000.
; Each entry is 4 bytes: [offset_lo, offset_hi, segment_lo, segment_hi].
; Vector 0x80 is at address 0x80 * 4 = 0x0200.
; =============================================================================
install_syscalls:
    cli                             ; Disable interrupts while modifying IVT
    push es

    xor ax, ax
    mov es, ax                      ; ES = 0x0000 (IVT segment)

    ; Install our handler at vector 0x80
    mov word [es:0x80*4],   syscall_handler  ; Offset
    mov word [es:0x80*4+2], cs               ; Segment

    pop es
    sti                             ; Re-enable interrupts
    ret

; =============================================================================
; syscall_handler — INT 0x80 dispatcher (O(1) jump table)
;
; Routes syscalls based on AH function number using a jump table instead of
; a linear comparison chain.  Each handler returns via IRET.
; All handlers preserve registers except documented return values.
;
; Dispatch uses BX as a scratch register for the table lookup.  BX is
; saved/restored via a kernel-local memory word, and the handler address
; is stored there for the indirect jump.  This leaves all registers intact
; when the handler begins executing.
;
; CF propagation: Handlers that return CF as a status indicator MUST use
; syscall_ret_cf instead of iret.  Plain iret restores the caller's
; original FLAGS, silently discarding any CF changes made by the handler.
; syscall_ret_cf uses retf 2 to preserve the handler's FLAGS.
; =============================================================================

; Macro: return from INT 0x80 handler preserving current FLAGS (including CF).
; Plain iret pops the caller's saved FLAGS, discarding the handler's CF.
; retf 2 pops IP and CS, then skips the saved FLAGS (SP += 2), so the
; current FLAGS register (with the handler's CF) remains in effect.
; sti re-enables interrupts (the CPU clears IF on INT).
%macro syscall_ret_cf 0
%ifdef DEBUG
    dec byte [cs:BIB_INT_DEPTH]
%endif
    sti
    retf 2
%endmacro

; syscall_iret — normal IRET exit with depth tracking
%macro syscall_iret 0
%ifdef DEBUG
    dec byte [cs:BIB_INT_DEPTH]
%endif
    iret
%endmacro


%include "kernel_syscall.inc"

; =============================================================================
; Shared subroutines (from src/include/)
; =============================================================================
%include "find_file.inc"
%include "load_binary.inc"
%define BOOT_REGDUMP
%include "boot_msg.inc"

; =============================================================================
; puts — Direct BIOS print (used by boot messages and kernel)
; =============================================================================
puts:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0E
    xor bh, bh
    int 0x10
    jmp puts
.done:
    ret

%include "kernel_data.inc"

%include "kernel_fault.inc"

%include "kernel_stack.inc"

; =============================================================================
; Serial I/O functions (debug build only — placed after kernel code to avoid
; polluting the header at offset 0)
; =============================================================================
%include "serial.inc"

; =============================================================================
; PADDING — fill to sector boundary
; =============================================================================
%ifdef DEBUG
times (14 * 512) - ($ - $$) db 0
%else
times (8 * 512) - ($ - $$) db 0
%endif
