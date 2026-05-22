; =============================================================================
; MNOS16 Master Boot Record (MBR)
;
; This is the very first code that runs when the computer boots.  The BIOS
; loads this 512-byte sector from disk sector 0 into memory at 0x7C00, then
; jumps here.  Our job:
;
;   1. Set up a usable CPU environment (segments, stack).
;   2. Scan the MBR partition table (4 entries at offset 0x1BE).
;   3. Find the partition marked "active" (bootable).
;   4. Load the Volume Boot Record (VBR) from that partition into memory.
;   5. Transfer control to the VBR so it can continue booting.
;
; The 512-byte MBR layout is:
;   Bytes 0–445   : Our code + data (max 446 bytes — very tight!)
;   Bytes 446–509 : Partition table (4 × 16-byte entries)
;   Bytes 510–511 : Boot signature 0x55, 0xAA (BIOS checks this)
;
; The partition table is stamped into the binary at build time by
; tools/create-disk.ps1.  At assembly time it's all zeros.
;
; Assembled with:  nasm -f bin -o mbr.bin src/boot/mbr.asm
; =============================================================================

[BITS 16]                           ; We're in 16-bit real mode
[ORG 0x7C00]                        ; BIOS loads us at this address

; The BIOS passes the boot drive number in the DL register (e.g. 0x80 for
; the first hard disk).  We save it and pass it through to the VBR later.

start:
    ; --- Initialize CPU state ------------------------------------------------
    ; In real mode, memory addresses are computed as segment×16 + offset.
    ; We set all segment registers to 0 so addresses are just plain offsets.
    xor ax, ax                      ; AX = 0
    mov ds, ax                      ; Data segment = 0
    mov es, ax                      ; Extra segment = 0 (used by rep movsw)
    mov ss, ax                      ; Stack segment = 0
    mov sp, 0x7C00                  ; Stack grows downward from 0x7C00
                                    ; (just below our code — safe because
                                    ;  0x0500–0x7BFF is free memory)

    mov [boot_drive], dl            ; Save BIOS boot drive number for later

    ; --- Clear the screen ----------------------------------------------------
    ; INT 10h AH=00h: Set video mode.  Mode 3 = 80×25 text, 16 colours.
    ; Setting the mode has the side-effect of clearing the screen.
    mov ax, 0x0003
    int 0x10

    ; --- Print banner --------------------------------------------------------
    mov si, msg_banner              ; SI → "In MBR\r\n"
    call puts                       ; Print it via BIOS teletype

    ; =========================================================================
    ; PARTITION TABLE SCAN
    ;
    ; The MBR partition table lives at offset 0x1BE (byte 446) in this sector.
    ; It has room for exactly 4 entries, each 16 bytes:
    ;
    ;   Offset  Size  Field
    ;   ------  ----  -----
    ;   0       1     Status — 0x80 = active/bootable, 0x00 = inactive
    ;   1–3     3     CHS address of first sector (legacy, we ignore this)
    ;   4       1     Partition type (0x7F = MNOS16, 0x00 = empty/unused)
    ;   5–7     3     CHS address of last sector  (legacy, we ignore this)
    ;   8–11    4     LBA of first sector (32-bit little-endian)
    ;   12–15   4     Size in sectors     (32-bit little-endian)
    ;
    ; We scan all 4 entries, print info about each one, and remember which
    ; entry (if any) is marked active.
    ; =========================================================================

    mov si, msg_reading             ; "Partitions:\r\n"
    call puts

    mov cx, 4                       ; Loop counter: 4 partition entries
    mov di, part_table              ; DI → first partition entry (at 0x1BE)
    mov bl, '1'                     ; Printable partition number ('1'..'4')
    mov word [active_entry], 0      ; No active partition found yet

.scan:
    push cx                         ; Save loop counter on stack

    ; Print "  P#: " — the entry header
    mov al, ' '
    call putc                       ; Two spaces of indentation
    call putc
    mov al, 'P'
    call putc
    mov al, bl                      ; Partition number character
    call putc
    mov al, ':'
    call putc
    mov al, ' '
    call putc

    ; --- Check if this entry is empty (type byte = 0x00) ---------------------
    mov al, [di+4]                  ; Read partition type byte
    test al, al                     ; Is it zero?
    jz .empty                       ; Yes → print "--" and skip to next

    ; --- Check if this entry is the active (bootable) partition --------------
    cmp byte [di], 0x80             ; Status byte == 0x80?
    jne .not_active                 ; No → just print a space
    mov [active_entry], di          ; Yes → remember this entry's address
    mov al, '*'                     ; Print '*' to mark it as active
    call putc
    jmp .show_type
.not_active:
    mov al, ' '                     ; Not active — print a space placeholder
    call putc

.show_type:
    ; Print partition details in the format: T=XX L=XXXXXXXX S=XXXXXXXX
    ;   T = partition type byte (hex)
    ;   L = LBA start sector (hex, 32-bit)
    ;   S = size in sectors (hex, 32-bit)

    ; -- Type --
    mov al, 'T'
    call putc
    mov al, '='
    call putc
    mov al, [di+4]                  ; Partition type byte
    call puthex8                    ; Print as 2 hex digits (e.g. "7F")

    ; -- LBA start (4 bytes, big-endian print order) --
    mov al, ' '
    call putc
    mov al, 'L'
    call putc
    mov al, '='
    call putc
    mov al, [di+11]                 ; Most significant byte first
    call puthex8
    mov al, [di+10]
    call puthex8
    mov al, [di+9]
    call puthex8
    mov al, [di+8]                  ; Least significant byte last
    call puthex8

    ; -- Size in sectors (4 bytes, big-endian print order) --
    mov al, ' '
    call putc
    mov al, 'S'
    call putc
    mov al, '='
    call putc
    mov al, [di+15]
    call puthex8
    mov al, [di+14]
    call puthex8
    mov al, [di+13]
    call puthex8
    mov al, [di+12]
    call puthex8
    jmp .eol                        ; Done with this entry → print newline

.empty:
    mov si, msg_none                ; "--\r\n" — marks an empty slot
    call puts
    jmp .next                       ; Skip the newline (msg_none has one)

.eol:
    mov si, msg_crlf                ; Print carriage-return + line-feed
    call puts

.next:
    add di, 16                      ; Advance DI to the next 16-byte entry
    inc bl                          ; Next partition number character
    pop cx                          ; Restore loop counter
    dec cx                          ; One fewer entry to scan
    jnz .scan                       ; Loop until all 4 are done

    ; =========================================================================
    ; VBR LOADING — Multi-sector, self-describing
    ;
    ; The VBR has a header (see vbr.asm) that tells us how many sectors the
    ; boot area occupies.  We do a two-phase load:
    ;
    ;   Phase 1: Load just sector 0 of the partition (512 bytes) into a
    ;            temporary buffer at 0x7E00 so we can read the header.
    ;
    ;   Phase 2: Read the sector count from the VBR header (offset 7),
    ;            then reload ALL boot-area sectors to 0x7E00.
    ;
    ;   Phase 3: Copy the loaded boot area from 0x7E00 down to 0x7C00
    ;            (overwriting this MBR — we're done with it) and jump.
    ;
    ; We use INT 13h AH=42h "Extended Read" which takes an LBA address
    ; via a 16-byte Disk Address Packet (DAP) instead of legacy CHS.
    ; =========================================================================

    mov si, [active_entry]          ; Did we find an active partition?
    test si, si
    jz .no_active                   ; No → print error and halt

    ; --- Phase 1: Load first VBR sector to read the header -------------------
    mov si, [active_entry]          ; SI → partition table entry
    mov eax, [si+8]                 ; EAX = LBA of partition's first sector
    mov [dap_lba], eax              ; Store in DAP (sectors 1 is already set)

    mov dl, [boot_drive]            ; DL = drive number (BIOS convention)
    mov si, dap                     ; SI → Disk Address Packet
    mov ah, 0x42                    ; AH = 42h: Extended Read Sectors
    int 0x13                        ; Call BIOS disk services
    jc .disk_err                    ; CF set on error → halt

    ; --- Phase 2: Read sector count from VBR header, reload all sectors ------
    ; The VBR header layout (see vbr.asm):
    ;   Offset 0:  EB xx    jmp short (skip header)
    ;   Offset 2:  90       NOP
    ;   Offset 3:  'MNOS'   Magic identifier (4 bytes)
    ;   Offset 7:  dw N     Boot area size in sectors
    ;
    ; We loaded the VBR to 0x7E00, so the sector count is at 0x7E00 + 7.

    mov cx, [0x7E00 + 7]            ; CX = number of sectors to load
    test cx, cx                     ; Zero sectors? That's invalid.
    jz .disk_err
    cmp cx, 128                     ; Sanity limit: max 128 sectors = 64 KB
    ja .disk_err                    ; (prevents loading absurd amounts)

    ; Update the DAP sector count and reload (LBA is unchanged from Phase 1)
    mov [dap_sectors], cx           ; DAP now requests all N sectors
    mov dl, [boot_drive]
    mov si, dap
    mov ah, 0x42
    int 0x13                        ; Read all boot-area sectors to 0x7E00
    jc .disk_err

    ; --- Phase 3: Copy boot area to 0x7C00 and jump to VBR ------------------
    ; We copy the loaded data from 0x7E00 down to 0x7C00.  This overwrites
    ; the MBR (us!), but that's fine — we're done executing MBR code.
    ; Since destination (0x7C00) < source (0x7E00), a forward copy is safe
    ; even though the regions overlap.
    ;
    ; The VBR is assembled with [ORG 0x7C00], so it expects to run there.

    ; Save boot drive in DL before the copy overwrites our data section.
    ; The rep movsw below does not touch DX, so DL is preserved across it.
    mov dl, [boot_drive]            ; DL = boot drive (safe in register)

    mov cx, [0x7E00 + 7]            ; Re-read sector count from header
    shl cx, 8                       ; Multiply by 256 (512 bytes / 2 = 256
                                    ; words per sector) → total word count
    cld                             ; Clear direction flag (copy forward)
    mov si, 0x7E00                  ; Source: where we loaded the data
    mov di, 0x7C00                  ; Destination: where VBR expects to run
    rep movsw                       ; Copy CX words (N × 512 bytes)

    ; DL still has boot drive from above — rep movsw doesn't clobber DX.
    jmp 0x0000:0x7C00               ; Far jump to VBR (also sets CS = 0)

    ; --- Error handlers ------------------------------------------------------
.no_active:
    mov si, msg_noact               ; "No active" — no bootable partition
    call puts
    jmp .halt
.disk_err:
    mov si, msg_derr                ; "Disk err" — INT 13h failed
    call puts
.halt:
    cli                             ; Disable interrupts
    hlt                             ; Halt the CPU — nothing more we can do

; =============================================================================
; SUBROUTINES
; =============================================================================

; puts — Print a NUL-terminated string.
;   Input:  DS:SI → string
;   Output: SI advanced past the NUL terminator
;   Uses BIOS INT 10h AH=0Eh (teletype output) for each character.
puts:
    lodsb                           ; AL = [DS:SI], SI++
    test al, al                     ; End of string? (NUL byte)
    jz .d                           ; Yes → return
    mov ah, 0x0E                    ; AH = 0Eh: teletype output
    xor bh, bh                     ; BH = page 0
    int 0x10                        ; Print character in AL
    jmp puts                        ; Next character
.d: ret

; putc — Print a single character.
;   Input:  AL = character to print
putc:
    mov ah, 0x0E                    ; Teletype output
    xor bh, bh                     ; Page 0
    int 0x10
    ret

; puthex8 — Print the value in AL as two hexadecimal digits.
;   Example: AL = 0x7F → prints "7F"
;   Works by printing the high nibble, then the low nibble.
puthex8:
    push ax                         ; Save AL (we need both nibbles)
    shr al, 4                       ; Shift high nibble into low nibble
    call .nib                       ; Print it
    pop ax                          ; Restore original AL
    and al, 0x0F                    ; Isolate low nibble
.nib:
    add al, '0'                     ; Convert 0–9 → '0'–'9'
    cmp al, '9'
    jbe putc                        ; If ≤ '9', it's a digit → print it
    add al, 7                       ; Convert 10–15 → 'A'–'F'
    jmp putc                        ;   ('9' + 1 + 7 = 'A' in ASCII)

; =============================================================================
; DATA — String constants and runtime variables
; =============================================================================
msg_banner  db 'In MBR', 13, 10, 0
msg_reading db 'Partitions:', 13, 10, 0
msg_none    db '--', 13, 10, 0      ; Printed for empty partition entries
msg_crlf    db 13, 10, 0            ; Carriage return + line feed
msg_noact   db 'No active', 0       ; Error: no active partition found
msg_derr    db 'Disk err', 0        ; Error: BIOS disk read failed

boot_drive  db 0                    ; Saved BIOS boot drive number (from DL)
active_entry dw 0                   ; Address of the active partition entry
                                    ; (0 = none found)

; --- Disk Address Packet (DAP) for INT 13h AH=42h ---------------------------
; This 16-byte structure tells the BIOS what to read:
;   Byte 0:    Packet size (always 0x10 = 16)
;   Byte 1:    Reserved (0)
;   Bytes 2–3: Number of sectors to read
;   Bytes 4–7: Buffer address (offset:segment) where data is loaded
;   Bytes 8–15: Starting LBA (64-bit, we only use the low 32 bits)
dap:
    db 0x10, 0                      ; Size = 16 bytes, reserved = 0
dap_sectors:
    dw 1                            ; Sectors to read (updated at runtime)
    dw 0x7E00, 0x0000               ; Load to 0x0000:0x7E00 (linear 0x7E00)
dap_lba:
    dd 0, 0                         ; LBA — filled at runtime from partition
                                    ; table entry

; =============================================================================
; PADDING + PARTITION TABLE + BOOT SIGNATURE
;
; The NASM `times` directive pads with zeros from the current position up to
; offset 0x1BE (446), where the partition table must begin.  If our code+data
; exceeds 446 bytes, NASM will emit an error — that's our size limit.
; =============================================================================

times 0x1BE - ($ - $$) db 0        ; Zero-fill to partition table offset

; Partition table — 4 × 16 bytes.  All zeros here; tools/create-disk.ps1
; stamps the real entries at build time.
part_table:
times 64 db 0

; Boot signature — the BIOS checks for 0x55 at byte 510 and 0xAA at byte 511.
; Without this, the BIOS won't recognize the sector as bootable.
dw 0xAA55

