# The Journey from 16-bit to 64-bit

A detailed guide to x86 CPU modes, the transitions between them, what changes
at each step, and how firmware (BIOS vs UEFI) fits into the picture.  Written
in the context of mini-os, but applicable to any bare-metal x86 project.

---

## 1. The Three Modes of x86

Every x86 CPU since the 80386 (1985) supports three main operating modes.
The CPU always starts in the first one:

```
┌──────────────┐     set CR0.PE      ┌──────────────┐    set CR0.PG +     ┌──────────────┐
│  Real Mode   │ ──────────────────► │  Protected   │    EFER.LME        │  Long Mode   │
│  (16-bit)    │                     │  Mode (32-b) │ ─────────────────► │  (64-bit)    │
│              │ ◄────────────────── │              │                     │              │
│  8086 compat │     clear CR0.PE    │  80386+      │                     │  AMD64/x64   │
└──────────────┘                     └──────────────┘                     └──────────────┘
```

| Property | Real Mode | Protected Mode | Long Mode |
|----------|-----------|----------------|-----------|
| Introduced | 8086 (1978) | 80386 (1985) | Opteron (2003) |
| Register width | 16-bit (AX, BX...) | 32-bit (EAX, EBX...) | 64-bit (RAX, RBX...) |
| Address bus | 20-bit (1 MB) | 32-bit (4 GB) | 48-bit virtual (256 TB) |
| Addressing | Segment:Offset | Flat or segmented | Flat (segments ignored) |
| Memory protection | None | Per-segment + optional paging | Paging mandatory |
| Paging | Not available | Optional | **Mandatory** (4-level) |
| Privilege levels | None | Ring 0–3 | Ring 0–3 |
| BIOS interrupts | ✅ Available | ❌ Not available | ❌ Not available |
| Max instruction length | 15 bytes | 15 bytes | 15 bytes |
| Default operand size | 16-bit | 32-bit | 32-bit (REX prefix → 64) |

---

## 2. Real Mode — Where Everything Starts

### 2.1 Why Real Mode?

When you press the power button, the CPU resets to a state compatible with the
original 8086 processor from 1978.  This is real mode.  It doesn't matter if
you have a 64-core Ryzen — at power-on, it behaves like a 1 MHz 8086.

**The reason**: Backward compatibility.  The BIOS firmware is (traditionally)
16-bit code.  If the CPU started in 64-bit mode, the 16-bit BIOS couldn't run.
So Intel has maintained this "start in real mode" contract for 45+ years.

### 2.2 How Real Mode Works

**Addressing**: The CPU forms a 20-bit physical address from two 16-bit values:

```
Physical address = Segment × 16 + Offset

Example: 0x07C0:0x0000 = 0x07C0 × 16 + 0x0000 = 0x7C00
         0x0000:0x7C00 = 0x0000 × 16 + 0x7C00 = 0x7C00 (same!)
```

Maximum address: 0xFFFF × 16 + 0xFFFF = 0x10FFEF (~1 MB + 64 KB).  Without
A20 enabled, bit 20 wraps, limiting access to exactly 1 MB.

**Registers** (all 16-bit):

```
General:  AX, BX, CX, DX, SI, DI, BP, SP
Segment:  CS, DS, ES, SS, FS, GS
Flags:    FLAGS (16-bit)
IP:       Instruction Pointer (16-bit)
```

**No memory protection**: Any code can read/write any address.  A bug in your
shell can overwrite the IVT, the BIOS data area, or its own code.  There are
no privilege levels — everything runs at the same level.

**Interrupts**: The IVT at 0x0000–0x03FF contains 256 entries, each a 4-byte
segment:offset pointer.  `INT 10h` looks up entry 16 (0x10 × 4 = offset 0x40),
loads CS:IP from there, and jumps to the BIOS handler.

### 2.3 What mini-os Uses in Real Mode

| BIOS Service | INT | Function | mini-os Usage |
|---|---|---|---|
| Video | 0x10 | AH=0Eh: teletype output | `putc` — prints one character |
| Video | 0x10 | AH=00h: set mode | `cls` — clears screen (mode 3) |
| Video | 0x10 | AH=03h: get cursor | `sysinfo` — reads cursor position |
| Keyboard | 0x16 | AH=00h: read key | `readline` — keyboard input |
| Disk | 0x13 | AH=42h: extended read | MBR, VBR, LOADER — load sectors |
| Disk | 0x13 | AH=48h: get params | `sysinfo` — EDD drive info |
| Memory | 0x15 | AH=88h: extended size | `mem` — reports extended memory |
| Memory | 0x15 | EAX=E820h: memory map | `mem` — full memory map |
| Memory | 0x12 | conventional memory | `mem` — reports base memory |
| A20 | 0x15 | AX=2401h: enable A20 | LOADER — A20 gate method 1 |

Every single one of these stops working after the switch to protected mode.

---

## 3. The Switch to Protected Mode (32-bit)

### 3.1 Prerequisites

Before the CPU can enter protected mode, you must prepare several data
structures and satisfy hardware requirements:

#### 3.1.1 Global Descriptor Table (GDT)

The GDT defines **memory segments** that the CPU uses in protected mode.
Unlike real-mode segments (which are just base addresses), protected-mode
segments carry access permissions, size limits, and privilege levels.

```
GDT Entry (8 bytes each):
  ┌─────────────────────────────────────────────────────────────┐
  │ Bits 63–56: Base[31:24]                                     │
  │ Bits 55–52: Flags (G, D/B, L, AVL)                         │
  │ Bits 51–48: Limit[19:16]                                    │
  │ Bits 47–40: Access byte (P, DPL, S, Type)                  │
  │ Bits 39–16: Base[23:0]                                      │
  │ Bits 15–0:  Limit[15:0]                                     │
  └─────────────────────────────────────────────────────────────┘
```

**Minimum GDT** (3 entries = 24 bytes):

| Entry | Base | Limit | Type | Purpose |
|-------|------|-------|------|---------|
| 0 | — | — | Null | Required (CPU ignores) |
| 1 | 0x00000000 | 0xFFFFF (4 GB with G=1) | Code, Ring 0 | Kernel code segment |
| 2 | 0x00000000 | 0xFFFFF (4 GB with G=1) | Data, Ring 0 | Kernel data segment |

With base=0 and limit=4 GB, both segments cover the entire address space.
This is called a **flat memory model** — segments exist because the CPU
requires them, but they don't actually restrict anything.  The selector for
entry 1 is 0x08 (index 1 × 8 bytes), and for entry 2 is 0x10.

**In NASM**:
```nasm
gdt_start:
    dq 0                            ; Null descriptor (required)

gdt_code:
    dw 0xFFFF                       ; Limit 0:15
    dw 0x0000                       ; Base 0:15
    db 0x00                         ; Base 16:23
    db 10011010b                    ; Access: P=1, DPL=00, S=1, Type=1010 (code, read)
    db 11001111b                    ; Flags: G=1, D=1, L=0; Limit 16:19 = 0xF
    db 0x00                         ; Base 24:31

gdt_data:
    dw 0xFFFF                       ; Limit 0:15
    dw 0x0000                       ; Base 0:15
    db 0x00                         ; Base 16:23
    db 10010010b                    ; Access: P=1, DPL=00, S=1, Type=0010 (data, write)
    db 11001111b                    ; Flags: G=1, D=1, L=0; Limit 16:19 = 0xF
    db 0x00                         ; Base 24:31
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1     ; GDT size minus 1
    dd gdt_start                    ; GDT linear base address
```

#### 3.1.2 A20 Gate (Already Done)

The A20 line must be enabled before switching to protected mode.  Without it,
odd-numbered megabytes (1–2 MB, 3–4 MB, etc.) would be inaccessible due to
address line masking.  mini-os already enables A20 in LOADER.SYS (v0.3.0+).

#### 3.1.3 Disable Interrupts

Interrupts must be disabled (`cli`) before the switch because:
- The real-mode IVT is no longer valid in protected mode
- Hardware interrupts (keyboard, timer) would crash if delivered before the
  IDT is set up
- After setting up the IDT, interrupts are re-enabled with `sti`

#### 3.1.4 Gather BIOS Information (Before Switch)

Any information that requires BIOS calls must be collected **before** the
switch, because BIOS interrupts won't work afterward:

| Info needed | How to get it | When to call |
|---|---|---|
| Memory map (E820) | INT 15h EAX=E820h | Before switch |
| Conventional memory | INT 12h | Before switch |
| Extended memory | INT 15h AH=88h | Before switch |
| Video mode | INT 10h AH=0Fh | Before switch |
| Drive geometry | INT 13h AH=48h | Before switch |
| Boot drive number | DL from BIOS | Already in BIB |

### 3.2 The Switch Sequence

```nasm
[BITS 16]
; ... gather BIOS info, enable A20 (already done) ...

switch_to_pm:
    cli                             ; 1. Disable interrupts

    lgdt [gdt_descriptor]           ; 2. Load GDT register

    mov eax, cr0                    ; 3. Read control register 0
    or eax, 1                       ;    Set PE (Protection Enable) bit
    mov cr0, eax                    ;    Write it back — CPU is NOW in PM!

    jmp 0x08:protected_entry        ; 4. Far jump: loads CS with selector 0x08
                                    ;    and flushes the instruction pipeline

[BITS 32]
protected_entry:
    ; 5. Reload all data segment registers with data selector (0x10)
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; 6. Set up a new stack (32-bit, can be anywhere in 4 GB)
    mov esp, 0x90000                ; Example: stack at 576 KB

    ; 7. You're in 32-bit protected mode!
    ;    Call your kernel, set up IDT, enable interrupts...
```

**Why the far jump?**  After setting CR0.PE, the CPU is in protected mode but
CS still holds a real-mode value.  The far jump to `0x08:protected_entry` loads
CS with the code segment selector (GDT entry 1) and tells the CPU "from now
on, interpret CS as a GDT selector, not a real-mode segment base."  It also
flushes the instruction prefetch queue, which may contain instructions decoded
in 16-bit mode.

### 3.3 What You Gain

| Capability | Real Mode | Protected Mode |
|---|---|---|
| Addressable memory | 1 MB | 4 GB |
| Memory protection | None | Segment limits + paging |
| Privilege separation | None | Ring 0 (kernel) vs Ring 3 (user) |
| 32-bit registers | Prefix-accessible | Native |
| Virtual memory | No | Yes (with paging) |
| Task switching | No | Hardware TSS support |

### 3.4 What You Lose

| Lost capability | Replacement |
|---|---|
| INT 10h (screen) | Direct VGA framebuffer writes at 0xB8000 |
| INT 16h (keyboard) | 8042 controller via ports 0x60/0x64 + IRQ 1 |
| INT 13h (disk) | ATA PIO via ports 0x1F0–0x1F7 + IRQ 14 |
| INT 15h (memory) | Call before switch, save results |
| Simple interrupt setup | Must build IDT + reprogram PIC |

### 3.5 Interrupt Descriptor Table (IDT)

The IDT replaces the real-mode IVT.  It tells the CPU where to jump when an
interrupt or exception occurs.

```
IDT Entry (8 bytes each in 32-bit mode):
  ┌──────────────────────────────────────────┐
  │ Bits 63–48: Offset[31:16]   (handler hi) │
  │ Bits 47–40: Flags (P, DPL, Type)         │
  │ Bits 39–32: Reserved (0)                 │
  │ Bits 31–16: Selector (code segment)      │
  │ Bits 15–0:  Offset[15:0]   (handler lo)  │
  └──────────────────────────────────────────┘
```

**You need IDT entries for**:

| Vector | Name | Source | Priority |
|--------|------|--------|----------|
| 0 | Division by zero | CPU exception | Must handle |
| 6 | Invalid opcode | CPU exception | Must handle |
| 8 | Double fault | CPU exception | Critical |
| 13 | General protection fault | CPU exception | Critical |
| 14 | Page fault | CPU exception | Critical (if paging) |
| 32 | Timer (IRQ 0) | PIT via PIC | Needed for multitasking |
| 33 | Keyboard (IRQ 1) | 8042 via PIC | Needed for input |
| 46 | Primary ATA (IRQ 14) | Disk controller | Needed for disk |

#### PIC Remapping

The 8259 Programmable Interrupt Controller (PIC) maps hardware IRQs to
interrupt vectors.  By default, IRQs 0–7 map to vectors 8–15 — which
**collide** with CPU exceptions (double fault is vector 8!).  You must
remap the PIC to move hardware IRQs to vectors 32+:

```nasm
; Remap PIC: IRQ 0-7 → INT 32-39, IRQ 8-15 → INT 40-47
mov al, 0x11        ; ICW1: initialize + expect ICW4
out 0x20, al        ; Master PIC command port
out 0xA0, al        ; Slave PIC command port

mov al, 32          ; ICW2: master PIC vector offset
out 0x21, al
mov al, 40          ; ICW2: slave PIC vector offset
out 0xA1, al

mov al, 4           ; ICW3: slave on IRQ 2
out 0x21, al
mov al, 2           ; ICW3: slave cascade identity
out 0xA1, al

mov al, 0x01        ; ICW4: 8086 mode
out 0x21, al
out 0xA1, al
```

---

## 4. Hardware Access Without BIOS

### 4.1 How Hardware Communication Works

There are exactly two ways the CPU talks to hardware devices:

**Port-mapped I/O** — The x86 CPU has a 64 KB I/O address space (0x0000–0xFFFF),
separate from memory.  The `in` and `out` instructions read/write these ports:

```nasm
in al, 0x60         ; Read one byte from port 0x60 (keyboard data)
out 0x20, al        ; Write one byte to port 0x20 (PIC command)
```

The port numbers are **hardwired by the PC architecture**.  Port 0x60 has been
the keyboard data port since the IBM AT (1984).  This never changes.

**Memory-mapped I/O** — Some devices expose registers as memory addresses.
The chipset intercepts reads/writes to those addresses and routes them to the
device instead of RAM:

```nasm
mov byte [0xB8000], 'H'    ; VGA text buffer — character
mov byte [0xB8001], 0x0F   ; VGA text buffer — attribute (white on black)
```

### 4.2 VGA Text Mode (Screen Output)

The VGA text-mode framebuffer lives at physical address **0xB8000**.  It is
an 80×25 grid (2000 cells), each cell being 2 bytes:

```
Byte 0: ASCII character code
Byte 1: Attribute byte
         Bits 7:    Blink (or bright background)
         Bits 6–4:  Background colour (0–7)
         Bits 3–0:  Foreground colour (0–15)
```

**Colour values**: 0=black, 1=blue, 2=green, 3=cyan, 4=red, 5=magenta,
6=brown, 7=light grey, 8=dark grey, 9=light blue, ..., 15=white.

To print "Hello" at row 0, column 0:
```nasm
mov edi, 0xB8000
mov ah, 0x07                ; Light grey on black
mov al, 'H' | mov [edi], ax | add edi, 2
mov al, 'e' | mov [edi], ax | add edi, 2
mov al, 'l' | mov [edi], ax | add edi, 2
mov al, 'l' | mov [edi], ax | add edi, 2
mov al, 'o' | mov [edi], ax
```

**Cursor control** uses VGA I/O ports:
```nasm
; Set cursor position to row R, column C
; Position = R × 80 + C
mov bx, position
mov dx, 0x3D4           ; VGA index port
mov al, 0x0F            ; Cursor location low register
out dx, al
inc dx                  ; 0x3D5 — VGA data port
mov al, bl
out dx, al
dec dx
mov al, 0x0E            ; Cursor location high register
out dx, al
inc dx
mov al, bh
out dx, al
```

### 4.3 Keyboard (Input)

The 8042 keyboard controller uses two ports:

| Port | Direction | Purpose |
|------|-----------|---------|
| 0x60 | Read | Scancode from keyboard (or mouse data) |
| 0x60 | Write | Send command to keyboard |
| 0x64 | Read | Status register (bit 0 = output buffer full) |
| 0x64 | Write | Send command to controller |

**Polling** (simple, no IRQ):
```nasm
.wait_key:
    in al, 0x64
    test al, 1              ; Bit 0: output buffer full?
    jz .wait_key            ; No data yet — keep polling
    in al, 0x60             ; Read the scancode
```

**Interrupt-driven** (proper): Set up IDT entry 33 (IRQ 1) to point to your
keyboard handler.  When a key is pressed, the PIC fires IRQ 1, your handler
reads port 0x60, stores the scancode in a buffer, sends EOI to the PIC
(`out 0x20, 0x20`), and returns.  The main loop checks the buffer.

**Scancodes**: The keyboard sends raw scancodes, not ASCII.  You need a
**scancode-to-ASCII translation table**.  For example, scancode 0x1E = 'A',
0x1F = 'S', 0x20 = 'D'.  Key releases send scancode + 0x80 (e.g., 0x9E for
'A' released).

### 4.4 ATA/IDE Disk (Storage)

The primary ATA controller uses ports 0x1F0–0x1F7:

| Port | Read | Write |
|------|------|-------|
| 0x1F0 | Data (16-bit) | Data (16-bit) |
| 0x1F1 | Error | Features |
| 0x1F2 | Sector count | Sector count |
| 0x1F3 | LBA low (bits 0–7) | LBA low |
| 0x1F4 | LBA mid (bits 8–15) | LBA mid |
| 0x1F5 | LBA high (bits 16–23) | LBA high |
| 0x1F6 | Drive/head | Drive/head (+ LBA bits 24–27) |
| 0x1F7 | Status | Command |

**Reading one sector via PIO (Programmed I/O)**:
```nasm
; Read 1 sector from LBA 2048 on drive 0
mov dx, 0x1F6
mov al, 0xE0            ; Drive 0, LBA mode, bits 24-27 = 0
or al, 0                ; LBA bits 24-27
out dx, al

mov dx, 0x1F2
mov al, 1               ; Sector count = 1
out dx, al

mov dx, 0x1F3
mov al, 0x00            ; LBA bits 0-7 (2048 & 0xFF = 0)
out dx, al

mov dx, 0x1F4
mov al, 0x08            ; LBA bits 8-15 (2048 >> 8 = 8)
out dx, al

mov dx, 0x1F5
mov al, 0x00            ; LBA bits 16-23
out dx, al

mov dx, 0x1F7
mov al, 0x20            ; Command: READ SECTORS
out dx, al

; Wait for data ready
.wait:
    in al, dx           ; Read status from 0x1F7
    test al, 0x08       ; Bit 3: DRQ (data request)
    jz .wait

; Read 256 words (512 bytes = 1 sector)
mov ecx, 256
mov dx, 0x1F0
mov edi, buffer
rep insw                ; Read CX words from port DX to ES:EDI
```

This replaces the entire INT 13h AH=42h flow that mini-os currently uses.

### 4.5 Programmable Interval Timer (PIT)

The PIT generates periodic interrupts (IRQ 0) at a configurable frequency.
It's the system clock — needed for timing, delays, and multitasking.

| Port | Purpose |
|------|---------|
| 0x40 | Channel 0 data (system timer) |
| 0x43 | Mode/command register |

```nasm
; Set PIT to ~1000 Hz (1193182 / 1193 ≈ 1000)
mov al, 00110110b       ; Channel 0, lobyte/hibyte, mode 3 (square wave)
out 0x43, al
mov ax, 1193            ; Divisor for ~1000 Hz
out 0x40, al            ; Low byte
mov al, ah
out 0x40, al            ; High byte
```

---

## 5. The Switch to Long Mode (64-bit)

### 5.1 Prerequisites (Beyond Protected Mode)

Long mode requires everything that protected mode requires, **plus**:

#### 5.1.1 CPU Support Check

Not all x86 CPUs support 64-bit mode.  You must check:

```nasm
; Step 1: Check if CPUID supports extended functions
mov eax, 0x80000000
cpuid
cmp eax, 0x80000001     ; Must support at least this
jb .no_long_mode

; Step 2: Check for Long Mode bit
mov eax, 0x80000001
cpuid
test edx, (1 << 29)     ; Bit 29: Long Mode available
jz .no_long_mode
```

#### 5.1.2 Paging (Mandatory)

In protected mode, paging is optional.  In long mode, **paging is mandatory**.
The CPU uses 4-level page tables to translate virtual addresses to physical
addresses:

```
Virtual Address (48-bit, sign-extended to 64-bit):
┌──────┬──────┬──────┬──────┬──────────────┐
│ PML4 │ PDPT │  PD  │  PT  │    Offset    │
│ 9bit │ 9bit │ 9bit │ 9bit │    12 bit    │
└──┬───┴──┬───┴──┬───┴──┬───┴──────┬───────┘
   │      │      │      │          │
   ▼      ▼      ▼      ▼          ▼
 PML4    PDPT    PD     PT      Physical
 Table → Table → Table → Table → Page (4 KB)
```

Each table has 512 entries of 8 bytes each = 4 KB per table.

**Minimum page tables** (identity-map first 2 MB using 2 MB large pages):

```nasm
; PML4 — Page Map Level 4 (1 entry used)
align 4096
pml4:
    dq pdpt + 0x03      ; Entry 0: present + writable, points to PDPT
    times 511 dq 0

; PDPT — Page Directory Pointer Table (1 entry used)
align 4096
pdpt:
    dq pd + 0x03         ; Entry 0: present + writable, points to PD
    times 511 dq 0

; PD — Page Directory (1 entry, 2 MB large page)
align 4096
pd:
    dq 0x00000083        ; Entry 0: present + writable + PS (2MB page) → phys 0
    times 511 dq 0
```

This maps virtual 0x0000000000000000–0x00000000001FFFFF to physical
0x00000000–0x001FFFFF (first 2 MB, 1:1 identity mapping).

#### 5.1.3 Updated GDT

Long mode requires a 64-bit code segment.  The GDT needs at least:

| Entry | Type | Key flag |
|-------|------|----------|
| 0 | Null | Required |
| 1 | 64-bit code | **L=1** (long mode), D=0 |
| 2 | Data | Same as 32-bit |

The critical difference: the code segment's **L bit** (bit 53) must be set to 1,
and the **D bit** (bit 54) must be 0.  This tells the CPU "this is a 64-bit
code segment."

### 5.2 The Switch Sequence

Starting from protected mode (which you've already entered):

```nasm
[BITS 32]

switch_to_long:
    ; 1. Disable paging (if it was enabled)
    mov eax, cr0
    and eax, ~(1 << 31)     ; Clear PG bit
    mov cr0, eax

    ; 2. Load page tables
    mov eax, pml4            ; Physical address of PML4 table
    mov cr3, eax             ; CR3 = page table root

    ; 3. Enable PAE (Physical Address Extension)
    mov eax, cr4
    or eax, (1 << 5)        ; Set PAE bit
    mov cr4, eax

    ; 4. Enable Long Mode in EFER MSR
    mov ecx, 0xC0000080      ; EFER MSR number
    rdmsr                    ; Read current value into EDX:EAX
    or eax, (1 << 8)        ; Set LME (Long Mode Enable) bit
    wrmsr                    ; Write it back

    ; 5. Enable paging (activates long mode)
    mov eax, cr0
    or eax, (1 << 31)       ; Set PG bit
    mov cr0, eax             ; CPU is NOW in long mode!

    ; 6. Far jump to 64-bit code segment
    jmp 0x08:long_entry      ; Selector 0x08 must be a 64-bit code segment

[BITS 64]
long_entry:
    ; 7. Reload segment registers
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax

    ; 8. Set up 64-bit stack
    mov rsp, 0x90000

    ; You're in 64-bit long mode!
```

### 5.3 What You Gain (Over 32-bit)

| Capability | 32-bit | 64-bit |
|---|---|---|
| Registers | 8 × 32-bit (EAX...) | 16 × 64-bit (RAX..., R8–R15) |
| Address space | 4 GB | 256 TB virtual, depends on physical |
| Instruction pointer | EIP (32-bit) | RIP (64-bit) + RIP-relative addressing |
| Calling convention | Stack-based (cdecl) | Register-based (System V / MS x64) |
| SSE | Optional | **Mandatory** (SSE2 baseline) |
| NX bit | Requires PAE | Always available (per-page no-execute) |

### 5.4 What Changes From 32-bit

| Aspect | 32-bit Protected | 64-bit Long |
|---|---|---|
| IDT entry size | 8 bytes | **16 bytes** |
| Paging | Optional | **Mandatory** (4-level) |
| Segmentation | Functional | Mostly ignored (FS/GS kept for TLS) |
| Default operand size | 32-bit | 32-bit (use REX prefix for 64-bit) |
| Push/pop size | 4 bytes | 8 bytes |
| Pointer size | 4 bytes | 8 bytes |
| BIOS calls | No | No (same as 32-bit) |

---

## 6. BIOS vs. UEFI

### 6.1 Why Does Legacy BIOS Exist?

The original IBM PC (1981) used an Intel 8088 — a 16-bit CPU with a 20-bit
address bus.  The BIOS was ROM-resident 16-bit code that initialized hardware
and provided a standard interface for OS bootloaders.

Every subsequent x86 CPU maintained backward compatibility by starting in
real mode.  The BIOS interface (INT 10h, INT 13h, etc.) became a de facto
standard that every OS depended on.  **Changing it would break everything.**

This continued for 25+ years until UEFI.

### 6.2 What Is UEFI?

UEFI (Unified Extensible Firmware Interface) is the modern replacement for
legacy BIOS.  Intel began developing it in the mid-1990s (originally called
EFI) and it became mainstream around 2010.

```
Legacy BIOS                          UEFI
──────────                          ────
CPU powers on in 16-bit real mode   CPU powers on in 16-bit real mode
BIOS runs POST in 16-bit            UEFI firmware switches to 32/64-bit
BIOS loads 512-byte MBR             UEFI loads EFI application from ESP
MBR is raw machine code             EFI app is a PE32+ executable
OS must switch modes itself         OS is already in 32/64-bit mode
```

### 6.3 Detailed Comparison

| Feature | Legacy BIOS | UEFI |
|---------|-------------|------|
| **Boot code format** | 512-byte flat binary (MBR) | PE32+ executable (any size) |
| **Disk partitioning** | MBR (4 partitions, 2 TB max) | GPT (128 partitions, 9.4 ZB max) |
| **Boot partition** | Active partition in MBR table | EFI System Partition (ESP, FAT32) |
| **CPU mode at handoff** | 16-bit real mode | 32-bit or 64-bit (matches firmware) |
| **Interface style** | Software interrupts (INT xxh) | C function call tables (protocols) |
| **Driver model** | ROM at fixed addresses | Loadable .efi driver binaries |
| **Max boot code size** | 446 bytes (MBR code area) | Limited only by ESP size |
| **Secure Boot** | Not supported | Cryptographic signature verification |
| **Graphics** | VGA text mode (80×25) | GOP (Graphics Output Protocol) |
| **Network boot** | PXE (16-bit) | HTTP boot, PXE (32/64-bit) |
| **Multi-boot** | Requires boot manager in MBR | Built-in boot manager |
| **Configuration** | CMOS/NVRAM (limited) | NVRAM variables (rich) |
| **Services after boot** | None (all gone after mode switch) | Runtime Services persist |
| **Development language** | Assembly (16-bit) | C (typically, with EDK II SDK) |

### 6.4 UEFI Boot Flow

```
Power on
    │
    ▼
┌─────────────────────┐
│ SEC (Security)      │  CPU starts in real mode
│ Minimal init, CAR   │  Switches to protected/long mode
└──────────┬──────────┘
           ▼
┌─────────────────────┐
│ PEI (Pre-EFI Init)  │  Initialize memory controller
│ Discover memory     │  Enable RAM
└──────────┬──────────┘
           ▼
┌─────────────────────┐
│ DXE (Driver Exec)   │  Load UEFI drivers
│ Initialize devices  │  Enumerate hardware
└──────────┬──────────┘
           ▼
┌─────────────────────┐
│ BDS (Boot Device    │  Find ESP partition (FAT32)
│     Selection)      │  Load \EFI\BOOT\BOOTx64.EFI
└──────────┬──────────┘
           ▼
┌─────────────────────┐
│ OS Loader (EFI app) │  Uses UEFI Boot Services:
│ (e.g., GRUB, Windows│  - File I/O, memory alloc,
│  Boot Manager)      │    graphics, network
│                     │  Calls ExitBootServices()
└──────────┬──────────┘
           ▼
┌─────────────────────┐
│ OS Kernel           │  Only UEFI Runtime Services
│ (already in 64-bit) │  remain (clock, variables)
└─────────────────────┘
```

### 6.5 UEFI Boot Services (Available Before ExitBootServices)

| Service | Purpose | Legacy BIOS equivalent |
|---------|---------|----------------------|
| `ConOut->OutputString()` | Print to screen | INT 10h AH=0Eh |
| `BS->ReadDisk()` | Read disk blocks | INT 13h AH=42h |
| `BS->AllocatePages()` | Allocate physical memory | None (manual) |
| `BS->GetMemoryMap()` | Get memory layout | INT 15h E820 |
| `BS->LoadImage()` | Load an EFI binary | Manual sector loading |
| `BS->LocateProtocol()` | Find device drivers | None |
| `GOP->Blt()` | Draw to screen (graphical) | INT 10h AH=00h (text only) |

These are called via **function pointers in a system table**, not interrupts.
The OS bootloader receives a pointer to the system table from the firmware.

### 6.6 Why mini-os Uses Legacy BIOS

| Reason | Explanation |
|--------|-------------|
| **Simplicity** | MBR is 512 bytes of flat code; EFI apps need PE headers, protocol discovery, and the EDK II SDK |
| **Education** | You learn how hardware *actually* works when BIOS goes away at mode switch |
| **Hyper-V Gen 1** | Gen 1 VMs use legacy BIOS (Gen 2 requires UEFI) |
| **Toolchain** | NASM + flat binary; UEFI typically needs a C compiler + EDK II build system |
| **Community** | Most hobby OS tutorials assume legacy BIOS |

A future version of mini-os *could* add UEFI support (Hyper-V Gen 2), but
that would be a separate boot path, not a replacement for the current one.

---

## 7. The Complete Journey for mini-os

### 7.1 Milestone Map

```
v0.1.0–v0.4.0 (DONE)              Future M5                Future M6+
━━━━━━━━━━━━━━━━━━━              ━━━━━━━━━━               ━━━━━━━━━━
16-bit Real Mode                  32-bit Protected          64-bit Long
                                  Mode                      Mode
┌────────────────┐          ┌────────────────┐        ┌────────────────┐
│ MBR (16-bit)   │          │ MBR (16-bit)   │        │ MBR (16-bit)   │
│ VBR (16-bit)   │          │ VBR (16-bit)   │        │ VBR (16-bit)   │
│ LOADER (16-bit)│          │ LOADER (16-bit)│        │ LOADER (16-bit)│
│ SHELL (16-bit) │          │   ┌────────┐   │        │   ┌────────┐   │
│                │          │   │ GDT    │   │        │   │ GDT    │   │
│ Uses BIOS for  │          │   │ Switch │   │        │   │ Paging │   │
│ everything     │          │   │ to PM  │   │        │   │ Switch │   │
│                │          │   └───┬────┘   │        │   │ to LM  │   │
└────────────────┘          │       ▼        │        │   └───┬────┘   │
                            │ KERNEL (32-bit)│        │       ▼        │
                            │ - VGA driver   │        │ KERNEL (64-bit)│
                            │ - KB driver    │        │ - Full 64-bit  │
                            │ - ATA driver   │        │ - 16 registers │
                            │ - IDT + PIC    │        │ - > 4 GB RAM   │
                            │ - Timer        │        │ - Paging + NX  │
                            └────────────────┘        └────────────────┘
```

### 7.2 What Changes at Each Step

| Component | Current (16-bit) | Add for 32-bit | Add for 64-bit |
|-----------|-----------------|----------------|----------------|
| **MBR** | No change needed | No change | No change |
| **VBR** | No change needed | No change | No change |
| **LOADER** | A20 + load shell | + GDT setup, + mode switch, + load kernel above 1 MB | + page tables, + CPUID long mode check, + EFER MSR, + 64-bit GDT |
| **SHELL** | 16-bit, uses BIOS | Might stay 16-bit (run before switch) or merge into kernel | Same |
| **New: KERNEL** | — | 32-bit binary at 0x100000+, VGA/KB/ATA/PIC/PIT drivers, IDT | 64-bit binary, 16-byte IDT entries, mandatory paging, APIC optional |
| **Build system** | `nasm -f bin` | Same (add `[BITS 32]` in kernel source) | Same (add `[BITS 64]`) |
| **Disk layout** | VBR + LOADER + SHELL | + KERNEL.SYS at new partition offset | Same |
| **BIB** | 6 bytes used | + E820 results, + video mode info | + paging root pointer |

### 7.3 Recommended Implementation Order

**Phase 1 — 32-bit "Hello World" (smallest step)**:
1. Add GDT to LOADER.SYS (24 bytes of data)
2. Add mode switch code to LOADER.SYS (~15 instructions)
3. Create KERNEL.SYS with `[BITS 32]` — just writes "Hello from 32-bit!" to
   VGA framebuffer at 0xB8000
4. Verify it boots in Hyper-V

**Phase 2 — 32-bit drivers**:
5. Keyboard driver (8042 polling first, IRQ later)
6. VGA text driver (putc, puts, cls, scroll)
7. IDT + PIC + timer (basic interrupt handling)
8. ATA PIO driver (read sectors without BIOS)
9. Recreate the shell in 32-bit

**Phase 3 — 64-bit (if desired)**:
10. CPUID long mode check
11. Page table setup (identity-map first 2–4 MB)
12. EFER MSR + mode switch
13. 64-bit kernel + 64-bit IDT entries
14. Extend page tables for all available RAM

---

## 8. Summary: What Mode Should mini-os Target?

| Target | Effort | Educational value | Practical use |
|--------|--------|-------------------|---------------|
| **Stay 16-bit** | None | Already learned real mode fully | Limited (1 MB) |
| **32-bit** | Medium | High — GDT, IDT, drivers, paging basics | Functional OS possible |
| **64-bit** | High | Highest — all of 32-bit + paging, long mode | Modern but complex |

The three-stage boot chain (v0.4.0) was designed specifically to support this
journey.  LOADER.SYS is the natural place for the mode switch, and the
architecture supports adding a KERNEL.SYS without restructuring anything.

---

*Document created: 2026-05-12*
*Relates to: DESIGN.md §9 (Roadmap), MEMORY-LAYOUT.md §8 (Future Beyond 1 MB)*
