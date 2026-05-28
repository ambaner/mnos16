; =============================================================================
; MNOS16 Loader (LOADER.SYS) - Stage 2
;
; Loaded by the VBR into memory at 0x0800.  Responsibilities:
;   1. Enable the A20 gate (3 fallback methods)
;   2. Present a boot menu (release / debug kernel selection)
;   3. Find the selected kernel in the MNFS directory and load it at 0x5000
;   4. Transfer control to the kernel
;
; The Boot Info Block (BIB) at 0x0600 is populated by the VBR and extended
; by the loader:
;   0x0600: boot_drive  (1 byte)
;   0x0601: a20_status  (1 byte)  — we update this
;   0x0602: part_lba    (4 bytes)
;   0x0606: boot_mode   (1 byte)  — we set this (0=release, 1=debug)
;
; Header layout (first 6 bytes):
;   Offset 0: 'MNLD'   Magic identifier (4 bytes)
;   Offset 4: dw N     Loader size in sectors
;
; Assembled with:  nasm -f bin -o loader.sys src/loader/loader.asm
; =============================================================================

%include "bib.inc"
%include "memory.inc"
%include "mnfs.inc"

[BITS 16]
[ORG 0x0800]                        ; VBR loads us here

; =============================================================================
; LOADER HEADER
; =============================================================================
loader_magic    db 'MNLD'           ; Magic identifier
loader_sectors  dw 3                ; Loader size in sectors

; =============================================================================
; LOADER CODE
; =============================================================================
loader_start:

; =============================================================================
; A20 GATE ENABLEMENT
;
; Enable the A20 address line so the CPU can access memory above 1 MB.
; Without A20, addresses wrap at the 1 MB boundary (8086 compatibility).
;
; We try three methods in order of preference:
;   1. BIOS INT 15h AX=2401h  (cleanest, most portable)
;   2. Keyboard controller     (classic AT method, port 0x64/0x60)
;   3. Fast A20 via port 0x92  (quick but not universal)
;
; After each attempt we verify A20 is actually enabled.
; =============================================================================
enable_a20:
    ; --- Check if A20 is already enabled -------------------------------------
    call check_a20
    jnz .a20_ok                     ; Already enabled, skip everything

    ; --- Method 1: BIOS INT 15h AX=2401h ------------------------------------
    mov ax, 0x2401
    int 0x15
    call check_a20
    jnz .a20_ok

    ; --- Method 2: Keyboard controller (8042) --------------------------------
    call .a20_wait_cmd              ; Wait for input buffer empty
    mov al, 0xAD                    ; Disable keyboard
    out 0x64, al

    call .a20_wait_cmd
    mov al, 0xD0                    ; Command: read output port
    out 0x64, al

    call .a20_wait_data             ; Wait for data to be available
    in al, 0x60                     ; Read current output port value
    push ax                         ; Save it

    call .a20_wait_cmd
    mov al, 0xD1                    ; Command: write output port
    out 0x64, al

    call .a20_wait_cmd
    pop ax
    or al, 0x02                     ; Set A20 bit (bit 1)
    out 0x60, al                    ; Write new output port value

    call .a20_wait_cmd
    mov al, 0xAE                    ; Re-enable keyboard
    out 0x64, al
    call .a20_wait_cmd

    call check_a20
    jnz .a20_ok

    ; --- Method 3: Fast A20 (port 0x92) -------------------------------------
    in al, 0x92
    or al, 0x02                     ; Set A20 bit (bit 1)
    and al, 0xFE                    ; Clear bit 0 (avoid system reset!)
    out 0x92, al

    call check_a20
    jnz .a20_ok

    ; --- All methods failed --------------------------------------------------
    mov byte [BIB_A20], 0           ; Record failure
    mov si, msg_a20_warn            ; Print warning (non-fatal, boot continues)
    call puts
    jmp boot_menu

.a20_ok:
    mov byte [BIB_A20], 1           ; Record success
    jmp boot_menu

; --- A20 helper: wait for 8042 input buffer to be empty ----------------------
.a20_wait_cmd:
    in al, 0x64
    test al, 0x02                   ; Bit 1 = input buffer full
    jnz .a20_wait_cmd
    ret

; --- A20 helper: wait for 8042 output buffer to have data --------------------
.a20_wait_data:
    in al, 0x64
    test al, 0x01                   ; Bit 0 = output buffer has data
    jz .a20_wait_data
    ret

; =============================================================================
; check_a20 - Test if the A20 line is enabled (wrap-around method).
;
;   Writes different values to 0x0000:0x0500 and 0xFFFF:0x0510.
;   If A20 is disabled these map to the same physical byte (aliased).
;   Saves and restores the original memory contents.
;
;   Output:  ZF=0 (NZ) if A20 enabled, ZF=1 (Z) if disabled
;   Clobbers: AX, CL
; =============================================================================
check_a20:
    push ds
    push es

    xor ax, ax
    mov ds, ax                      ; DS = 0x0000
    mov ax, 0xFFFF
    mov es, ax                      ; ES = 0xFFFF

    ; Save original bytes at both test locations
    mov al, [ds:0x0500]
    push ax
    mov al, [es:0x0510]
    push ax

    ; Write different test patterns
    mov byte [es:0x0510], 0x13
    mov byte [ds:0x0500], 0x37

    ; Check: did writing to 0x0500 also change 0x0510?
    cmp byte [es:0x0510], 0x37
    je .chk_off
    mov cl, 1                       ; Different → A20 is enabled
    jmp .chk_restore
.chk_off:
    mov cl, 0                       ; Same → A20 is disabled (wrapped)

.chk_restore:
    ; Restore original bytes (reverse order of push)
    pop ax
    mov [es:0x0510], al
    pop ax
    mov [ds:0x0500], al

    pop es
    pop ds

    test cl, cl                     ; Set ZF: NZ if enabled, Z if disabled
    ret

; =============================================================================
; BOOT MENU — Present release/debug kernel selection
;
; Clears the screen, displays a two-entry menu, waits for the user to press
; '1' (Release) or '2' (Debug), stores the choice in BIB_BOOT_MODE, and
; sets SI to the correct kernel filename for load_kernel.
; =============================================================================
boot_menu:
    ; Ensure keyboard controller is enabled (defensive: A20 Method 2 disables
    ; the 8042 keyboard temporarily; re-enable here in case of timing issues)
    mov al, 0xAE                    ; 8042 command: enable keyboard
    out 0x64, al

    ; Clear screen (set video mode 3 = 80x25 color text)
    mov ax, 0x0003
    int 0x10

    ; Print menu
    mov si, msg_menu
    call puts

.menu_wait:
    ; Flush any stale keys in BIOS buffer before waiting
    mov ah, 0x01                    ; Check if key available (non-blocking)
    int 0x16
    jz .wait_key                    ; No key pending — go wait
    xor ah, ah                      ; Consume the stale key
    int 0x16
    jmp .menu_wait                  ; Check for more stale keys

.wait_key:
    ; Read keystroke (INT 16h AH=0: wait for key, AL=ASCII)
    xor ah, ah
    int 0x16

    cmp al, '1'
    je .menu_release
    cmp al, '2'
    je .menu_debug
    jmp .menu_wait                  ; Invalid key — try again

.menu_release:
    mov byte [BIB_BOOT_MODE], 0
    mov si, fname_kernel
    jmp load_kernel

.menu_debug:
    mov byte [BIB_BOOT_MODE], 1
    mov si, fname_kerneld
    jmp load_kernel

; =============================================================================
; LOAD KERNEL
;
; Find the selected kernel variant in the MNFS directory table, load it
; at 0x5000, then jump to it.  SI is set by boot_menu to the correct
; 11-byte 8.3 filename (fname_kernel or fname_kerneld).
; =============================================================================
load_kernel:
    ; Use DIR_SCRATCH_BUF as scratch buffer for directory read (all module memory is free)
    mov bx, DIR_SCRATCH_BUF         ; Scratch buffer
    ; SI already set by boot_menu to fname_kernel or fname_kerneld
    call find_file
    jc .kernel_fail

    ; EAX = partition-relative start sector, CX = size in sectors
    mov bx, KERNEL_OFF              ; Load address (segment 0x0000)
    mov ecx, 'MNKN'                 ; Expected magic signature
    mov dh, 16                      ; Maximum sector count
    call load_mnex
    jc .kernel_fail

    ; --- Boot status: kernel loaded successfully -----------------------------
    mov si, msg_kernel
    call boot_ok

    ; --- Transfer control to KERNEL.SYS --------------------------------------
    ; Skip the 6-byte MNEX header (magic + sector count) to reach code entry.
    jmp KERNEL_SEG:KERNEL_OFF + MNEX_HDR_SIZE

; --- Error handler (fatal — prints [FAIL] and halts) -------------------------
.kernel_fail:
    mov si, msg_kernel
    call boot_fail

; --- Shared subroutines (from src/include/) -----------------------------------
%include "find_file.inc"
%include "load_binary.inc"
%define BOOT_REGDUMP
%include "boot_msg.inc"
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

; =============================================================================
; DATA
; =============================================================================
msg_a20_warn db '[WARN] A20 gate not enabled', 13, 10, 0
msg_kernel   db 'Kernel loaded', 0

; 11-byte "8.3" filenames for directory lookup (8 name + 3 ext, space-padded)
fname_kernel  db 'KERNEL  SYS'
fname_kerneld db 'KERNELD SYS'

; Boot menu text
msg_menu     db 13, 10
             db '  MNOS Boot Manager', 13, 10
             db 13, 10
             db '  1) MNOS [Release]', 13, 10
             db '  2) MNOS [Debug]', 13, 10
             db 13, 10
             db '  Press 1 or 2: ', 0

; --- Disk Address Packet (DAP) for INT 13h AH=42h ---------------------------
dap:
    db 0x10, 0                      ; Size = 16, reserved = 0
dap_sectors:
    dw 0                            ; Sectors to read (set by load_mnex)
dap_buffer:
    dw 0, 0                         ; Buffer address (set by load_mnex)
dap_lba:
    dd 0, 0                         ; LBA (set by load_mnex)

; =============================================================================
; PADDING — fill to 3 sectors (1536 bytes)
; =============================================================================
times (3 * 512) - ($ - $$) db 0
