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

    ; --- Load FS.SYS (filesystem module) at 0x0800 ---------------------------
    ; FS.SYS replaces LOADER.SYS in memory (LOADER's job is done).
    ; Use 0x3000 (shell area) as scratch buffer for directory read.
    ; Select filename based on boot mode (release=FS, debug=FSD).
    mov bx, SHELL_OFF               ; Scratch buffer (shell not loaded yet)
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
    mov bx, LOADER_OFF              ; Load FS.SYS at 0x0800 (LOADER's old slot)
    mov ecx, 'MNFS'                 ; Expected magic signature
    mov dh, 16                      ; Maximum sector count
    call load_mnex
    jc .fs_load_fail
    ASSERT_MAGIC LOADER_OFF, 'MNFS', "FS.SYS magic invalid after load"

    mov si, msg_fs
    call boot_ok
    DBG "KERNEL: FS.SYS loaded at 0x0800"

    ; --- Initialize FS.SYS (installs INT 0x81) --------------------------------
    ; FS.SYS's init entry point is at offset 6 (right after the 6-byte header).
    call LOADER_OFF + MNEX_HDR_SIZE
    jc .fs_init_fail
    ASSERT_CF_CLEAR "FS.SYS init returned error"

    mov si, msg_fs_init
    call boot_ok
    DBG "KERNEL: INT 0x81 filesystem ready"

    ; --- Load MM.SYS (memory manager) at 0x2800 ------------------------------
    ; MM.SYS provides dynamic heap allocation via INT 0x82.
    ; Use 0x2000 as scratch buffer for directory read (safe — above FS.SYS,
    ; below MM target at 0x2800).
    ; Select filename based on boot mode (release=MM, debug=MMD).
    mov bx, 0x2000                  ; Scratch buffer
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
    mov bx, MM_OFF                  ; Load address (0x2800)
    mov ecx, 'MNMM'                ; Expected magic signature
    mov dh, MM_MAX_SECTORS          ; Maximum sector count (4)
    call load_mnex
    jc .mm_load_fail
    ASSERT_MAGIC MM_OFF, 'MNMM', "MM.SYS magic invalid after load"

    mov si, msg_mm
    call boot_ok
    DBG "KERNEL: MM.SYS loaded at 0x2800"

    ; --- Initialize MM.SYS (installs INT 0x82) --------------------------------
    ; MM.SYS's init entry point is at offset 6 (right after the 6-byte header).
    call MM_OFF + MNEX_HDR_SIZE
    jc .mm_init_fail

    mov si, msg_mm_init
    call boot_ok
    DBG "KERNEL: INT 0x82 memory manager ready"

    ; --- Load SHELL.SYS at 0x3000 --------------------------------------------
    ; Use 0x2000 as scratch buffer for directory read (safe — between LOADER
    ; area and SHELL area, and FS.SYS at 0x0800 is only ~1 KB).
    ; Select filename based on boot mode (release=SHELL, debug=SHELLD).
    mov bx, 0x2000                  ; Scratch buffer
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
    mov bx, SHELL_OFF               ; Load address (segment 0x0000)
    mov ecx, 'MNEX'                 ; Expected magic signature
    mov dh, 32                      ; Maximum sector count
    call load_mnex
    jc .shell_load_fail
    ASSERT_MAGIC SHELL_OFF, 'MNEX', "SHELL.SYS magic invalid after load"

    mov si, msg_shell
    call boot_ok
    DBG "KERNEL: SHELL.SYS loaded, jumping to shell"

    ; --- Transfer control to shell --------------------------------------------
    ; The shell is a user-mode executable.  When it calls INT 0x80, the CPU
    ; jumps to our syscall_handler via the IVT entry we installed above.
    ; Skip the 6-byte MNEX header (magic + sector count) to reach shell code.
    jmp SHELL_SEG:SHELL_OFF + MNEX_HDR_SIZE

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
