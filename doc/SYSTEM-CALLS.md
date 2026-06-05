# System Calls — User ↔ Kernel Boundary

> How user-mode code talks to the kernel across all CPU modes, from the 8086
> interrupt vector table to the modern `syscall` instruction.

---

## 1. What Is a System Call?

A **system call** (syscall) is the mechanism by which user-mode code requests
a service from the kernel.  The user cannot access hardware directly — it asks
the kernel, and the kernel performs the operation on its behalf.

```
┌─────────────────────────────────────────────────────────┐
│  User Mode (Ring 3)                                     │
│                                                         │
│  Application / Shell                                    │
│    │                                                    │
│    │  "I want to read a file"                           │
│    │                                                    │
│    ▼                                                    │
│  syscall stub (thin wrapper)                            │
│    │  - Places function number in register              │
│    │  - Places arguments in registers/stack             │
│    │  - Triggers ring transition                        │
│    ▼                                                    │
├─────────────── PRIVILEGE BOUNDARY ──────────────────────┤
│    │                                                    │
│    ▼                                                    │
│  Kernel Mode (Ring 0)                                   │
│                                                         │
│  Syscall dispatcher                                     │
│    │  - Reads function number                           │
│    │  - Validates arguments                             │
│    │  - Calls internal kernel function                  │
│    │  - Returns result to user mode                     │
│    ▼                                                    │
│  Hardware access (disk, screen, keyboard, etc.)         │
│                                                         │
└─────────────────────────────────────────────────────────┘
```

This boundary exists for **protection** — a buggy or malicious user program
cannot crash the system or access another program's memory because the kernel
mediates all hardware and memory operations.

### 1.1 The Three Questions

Every syscall mechanism must answer:

1. **How does user code invoke the kernel?** (What instruction triggers the
   transition?)
2. **How does the CPU switch privilege levels?** (How does ring 3 become
   ring 0?)
3. **How does the kernel know what the user wants?** (How are function number
   and arguments passed?)

The answers change dramatically across CPU modes.

---

## 2. 16-bit Real Mode — Software Interrupts and the IVT

### 2.1 The Honest Truth: No Protection

Real mode has **no privilege rings**.  There is no ring 0 or ring 3 — all code
runs at the same privilege level with full access to all memory and all I/O
ports.  The "kernel" and "shell" are separated by **convention**, not by
hardware.

Despite this, we implement the syscall interface anyway because:

- It establishes the **architectural pattern** that carries forward to 32/64-bit
- It teaches the concept of indirection through an interrupt vector table
- The shell code is written to use syscalls, so when we switch to protected
  mode, the shell's calling convention stays the same — only the kernel-side
  mechanism changes

### 2.2 The Interrupt Vector Table (IVT)

The IVT is a 1 KB array at the very bottom of physical memory, from address
`0x0000:0x0000` to `0x0000:0x03FF`.  It contains 256 entries, one per
interrupt vector, each consisting of a 4-byte **far pointer** (offset:segment):

```
Address    Vector   Purpose
────────   ──────   ──────────────────────────────
0x0000     0x00     Divide by zero (CPU exception)
0x0004     0x01     Debug / single step
0x0008     0x02     NMI (non-maskable interrupt)
0x000C     0x03     Breakpoint (INT 3)
0x0010     0x04     Overflow (INTO)
  ...
0x0040     0x10     BIOS video services
0x0044     0x11     BIOS equipment list
0x0048     0x12     BIOS memory size
0x004C     0x13     BIOS disk services
0x0050     0x14     BIOS serial port
0x0054     0x15     BIOS misc services
0x0058     0x16     BIOS keyboard services
0x005C     0x17     BIOS printer
0x0060     0x18     BIOS ROM BASIC
0x0064     0x19     BIOS bootstrap loader
0x0068     0x1A     BIOS time of day
  ...
0x00C0     0x30     (available for OS use)
  ...      ...      (vectors 0x30–0x7F generally available)
  ...
0x0200     0x80     ← mini-os syscall vector
  ...
0x03FC     0xFF     Last vector
```

Each entry is laid out as:

```
Byte 0-1:  Offset  (16-bit, little-endian)
Byte 2-3:  Segment (16-bit, little-endian)
```

When the CPU executes `int N`, it:

1. Pushes FLAGS onto the stack
2. Clears IF and TF (disables interrupts and single-stepping)
3. Pushes CS (code segment) onto the stack
4. Pushes IP (instruction pointer) onto the stack
5. Reads the far pointer at address `N * 4`
6. Loads CS:IP from that pointer — execution jumps to the handler

When the handler executes `iret`, the CPU:

1. Pops IP from the stack
2. Pops CS from the stack
3. Pops FLAGS from the stack (restoring IF, etc.)
4. Execution resumes at the instruction after `int N`

### 2.3 Installing a Syscall Handler

The kernel installs its handler by writing directly to the IVT.  In real mode,
any code can write to any memory address — there is no protection.

```nasm
; ═══════════════════════════════════════════════════════════════════
; kernel_init_syscalls — Install the mini-os syscall handler
;
; Writes our handler's far pointer into IVT entry 0x80.
; Must be called with interrupts disabled (or wrapped in cli/sti).
; ═══════════════════════════════════════════════════════════════════
kernel_init_syscalls:
    cli                                 ; Disable interrupts while modifying IVT
    push es                             ; Save ES

    xor ax, ax
    mov es, ax                          ; ES = 0x0000 (IVT segment)

    ; IVT entry for vector 0x80 is at address 0x80 * 4 = 0x0200
    mov word [es:0x0200], syscall_handler   ; Offset of our handler
    mov word [es:0x0202], cs                ; Segment of our handler

    pop es                              ; Restore ES
    sti                                 ; Re-enable interrupts
    ret
```

After this runs, any `int 0x80` instruction anywhere in memory will jump to
`syscall_handler` in the kernel.

### 2.4 The Syscall Dispatcher

The dispatcher reads the function number from AH and branches to the
appropriate handler.  Arguments are passed in registers following a convention
defined by the kernel:

```nasm
; ═══════════════════════════════════════════════════════════════════
; syscall_handler — mini-os 16-bit syscall dispatcher
;
; Calling convention:
;   AH = function number
;   Other registers = function-specific arguments
;   Returns: function-specific (typically AX = result, CF = error)
;
; The handler preserves all registers except those used for return
; values.  The caller should not assume any register is preserved
; unless documented for that specific function.
; ═══════════════════════════════════════════════════════════════════
syscall_handler:
    cmp ah, SYS_PRINT_STRING
    je .fn_print_string

    cmp ah, SYS_PRINT_CHAR
    je .fn_print_char

    cmp ah, SYS_READ_KEY
    je .fn_read_key

    cmp ah, SYS_READ_SECTOR
    je .fn_read_sector

    cmp ah, SYS_GET_VERSION
    je .fn_get_version

    ; Unknown function — set carry flag to indicate error
    stc
    iret

; ─── SYS_PRINT_STRING (AH=0x01) ──────────────────────────────────
; Input:  DS:SI = pointer to null-terminated string
; Output: none
; ──────────────────────────────────────────────────────────────────
.fn_print_string:
    push ax
    push si
.print_loop:
    lodsb                               ; AL = [DS:SI], SI++
    or al, al                           ; Null terminator?
    jz .print_done
    mov ah, 0x0E                        ; BIOS teletype output
    int 0x10                            ; → screen
    jmp .print_loop
.print_done:
    pop si
    pop ax
    iret

; ─── SYS_PRINT_CHAR (AH=0x02) ────────────────────────────────────
; Input:  AL = character to print
; Output: none
; ──────────────────────────────────────────────────────────────────
.fn_print_char:
    push ax
    mov ah, 0x0E                        ; BIOS teletype output
    int 0x10
    pop ax
    iret

; ─── SYS_READ_KEY (AH=0x03) ──────────────────────────────────────
; Input:  none
; Output: AH = scan code, AL = ASCII character (0 if special key)
; ──────────────────────────────────────────────────────────────────
.fn_read_key:
    xor ah, ah                          ; BIOS: wait for keypress
    int 0x16                            ; AH = scan code, AL = ASCII
    iret                                ; Return both in AX

; ─── SYS_READ_SECTOR (AH=0x04) ───────────────────────────────────
; Input:  EDI = LBA sector number (NOT EAX — avoids AH clobber)
;         ES:BX = buffer to read into
;         CL = number of sectors to read
; Output: CF clear = success, CF set = error
; Note:   Uses syscall_ret_cf (sti; retf 2) to preserve CF across
;         the return — iret would restore caller's FLAGS and lose CF.
; ──────────────────────────────────────────────────────────────────
.fn_read_sector:
    ; (Implementation uses BIOS int 13h extended read
    ;  with a DAP structure, similar to the current loader)
    ; ...
    syscall_ret_cf                      ; Preserve CF for caller

; ─── SYS_GET_VERSION (AH=0x05) ───────────────────────────────────
; Input:  none
; Output: AH = major version, AL = minor version
; ──────────────────────────────────────────────────────────────────
.fn_get_version:
    mov ax, 0x0600                      ; Version 6.0
    iret

; ─── Function number constants ────────────────────────────────────
SYS_PRINT_STRING  equ 0x01
SYS_PRINT_CHAR    equ 0x02
SYS_READ_KEY      equ 0x03
SYS_READ_SECTOR   equ 0x04
SYS_GET_VERSION   equ 0x05
```

### 2.5 Shell Using Syscalls

The shell never calls BIOS directly — it goes through `int 0x80`:

```nasm
; ═══════════════════════════════════════════════════════════════════
; Shell example: print a greeting using kernel syscall
; ═══════════════════════════════════════════════════════════════════
shell_main:
    ; Print welcome message
    mov ah, SYS_PRINT_STRING            ; Function: print string
    mov si, msg_welcome                 ; DS:SI → string
    int 0x80                            ; Kernel handles it

    ; Read a key
    mov ah, SYS_READ_KEY                ; Function: read key
    int 0x80                            ; Returns key in AL

    ; Print the key back
    mov ah, SYS_PRINT_CHAR             ; Function: print character
    ; AL already has the character from READ_KEY
    int 0x80

    jmp shell_main                      ; Loop forever

msg_welcome: db 'mnos:\> ', 0
```

### 2.6 What Happens Under the Hood

Here is the complete CPU-level trace when the shell calls `int 0x80`:

```
Shell code at CS=0x0300, IP=0x0042:
    mov ah, 0x01        ; AH = function number
    mov si, msg         ; SI = string pointer
    int 0x80            ; ← THIS INSTRUCTION

CPU executes INT 0x80:
  1. [PUSH]  FLAGS → stack     (save processor state)
  2. [CLEAR] IF=0, TF=0       (disable interrupts + tracing)
  3. [PUSH]  CS → stack        (save 0x0300)
  4. [PUSH]  IP → stack        (save return address, 0x0044)
  5. [READ]  IVT[0x80]         (read 4 bytes at 0x0000:0x0200)
  6. [LOAD]  CS = segment from IVT  (e.g., 0x0400 — kernel segment)
  7. [LOAD]  IP = offset from IVT   (e.g., 0x0100 — handler offset)
  8. [JUMP]  → 0x0400:0x0100   (kernel's syscall_handler)

Kernel handler runs:
  9. Reads AH (0x01 → print string)
 10. Reads DS:SI (string pointer from shell's data segment)
 11. Loops through string, calling BIOS int 0x10 for each character
 12. Executes IRET

CPU executes IRET:
 13. [POP]   IP ← stack        (restore 0x0044)
 14. [POP]   CS ← stack        (restore 0x0300)
 15. [POP]   FLAGS ← stack     (restore IF, etc.)
 16. [JUMP]  → 0x0300:0x0044   (shell resumes after int 0x80)
```

Total overhead: ~70 clock cycles for the interrupt + return, plus the handler
logic.  In real mode, this is negligible.

### 2.7 Why INT 0x80?

The choice of vector number is arbitrary — any unused vector works.  We use
0x80 because:

- **Tradition** — Linux used `int 0x80` for its 32-bit syscall interface,
  making it the most well-known syscall vector
- **Safe range** — vectors 0x00–0x1F are CPU exceptions, 0x10–0x1A are BIOS
  services, 0x20–0x2F are typically used by DOS.  Vector 0x80 is well clear
  of all these
- **Recognizable** — anyone reading the code will immediately understand
  `int 0x80` means "system call"

### 2.8 Limitations of the Real-Mode Approach

| Limitation | Consequence |
|-----------|-------------|
| No privilege rings | Shell can bypass `int 0x80` and call BIOS directly |
| No memory protection | Shell can overwrite kernel code or IVT |
| No separate address spaces | Shell can read/write kernel data |
| Shared stack | Kernel and shell use the same stack segment |
| No preemption | Kernel cannot forcibly interrupt a misbehaving shell |
| No C compiler support | Clang does not support 16-bit x86 code generation |

These limitations are inherent to real mode and are resolved by switching
to protected mode (§3).

> **Why no C in 16-bit?**  Clang (and GCC's cross-compiler) cannot emit
> 16-bit real-mode code.  The NASM assembler is the only option for the
> 16-bit kernel and shell.  Starting in Phase 2 (32-bit protected mode),
> both kernel and user-mode code can be written in C — the same `int 0x80`
> syscall interface works from inline assembly in C, and the syscall numbers
> remain stable across the transition (see §7.4).

---

## 3. 32-bit Protected Mode — Hardware-Enforced Privilege

### 3.1 The Privilege Ring Model

Protected mode introduces **4 privilege levels** (rings), though most
operating systems use only two:

```
┌──────────────────────────────────┐
│          Ring 0 (Kernel)         │  ← Full hardware access
│  ┌────────────────────────────┐  │
│  │       Ring 1 (unused)      │  │
│  │  ┌──────────────────────┐  │  │
│  │  │   Ring 2 (unused)    │  │  │
│  │  │  ┌────────────────┐  │  │  │
│  │  │  │  Ring 3 (User)  │  │  │  │  ← Restricted access
│  │  │  └────────────────┘  │  │  │
│  │  └──────────────────────┘  │  │
│  └────────────────────────────┘  │
└──────────────────────────────────┘
```

- **Ring 0** — Kernel mode.  Can execute any instruction (`in`, `out`, `cli`,
  `lgdt`, `mov cr0`, etc.), access any memory, and configure the CPU.
- **Ring 3** — User mode.  Cannot execute privileged instructions (CPU
  generates a General Protection Fault #GP if attempted), cannot access I/O
  ports (unless explicitly permitted via the I/O Permission Bitmap), and can
  only access memory pages marked as user-accessible.

The **Current Privilege Level** (CPL) is stored in the low 2 bits of the CS
register.  The CPU checks CPL on every instruction and memory access.

### 3.2 The Gate Mechanism (IDT)

In protected mode, the IVT is replaced by the **Interrupt Descriptor Table**
(IDT).  Unlike the IVT (which is a simple array of pointers), the IDT
contains **gate descriptors** that specify not just where to jump, but what
privilege level is required to use the gate.

Each IDT entry (interrupt gate) is 8 bytes:

```
Bits     Field              Purpose
───────  ─────────────────  ──────────────────────────────────────
0–15     Offset [15:0]      Low 16 bits of handler address
16–31    Segment selector   Code segment in GDT (e.g., 0x08 for ring 0 code)
32–39    Reserved           Must be zero
40–43    Type               0xE = 32-bit interrupt gate
44       Zero               Must be 0
45–46    DPL                Descriptor Privilege Level (who can invoke)
47       Present            1 = entry is valid
48–63    Offset [31:16]     High 16 bits of handler address
```

The critical field is **DPL** — this controls who is allowed to trigger this
interrupt:

- `DPL = 0` — Only ring 0 code can invoke it (hardware interrupts, CPU
  exceptions)
- `DPL = 3` — Ring 3 code can invoke it (this is what we set for the syscall
  vector)

If ring 3 code executes `int 0x80` and the IDT entry for 0x80 has `DPL = 0`,
the CPU generates a **General Protection Fault** (#GP, vector 13) instead
of executing the handler.

### 3.3 The Stack Switch (TSS)

When the CPU transitions from ring 3 to ring 0 via an interrupt gate, it must
switch to the **kernel stack** — the user's stack cannot be trusted (it could
be nearly full, corrupted, or maliciously crafted).

The CPU finds the kernel stack pointer in the **Task State Segment** (TSS),
a hardware-defined structure loaded via the `ltr` instruction:

```c
// TSS structure (simplified — 104 bytes total)
struct tss {
    uint32_t prev_tss;      // Unused in software task switching
    uint32_t esp0;          // ← Kernel stack pointer (Ring 0)
    uint32_t ss0;           // ← Kernel stack segment (Ring 0)
    uint32_t esp1;          // Ring 1 stack (unused)
    uint32_t ss1;
    uint32_t esp2;          // Ring 2 stack (unused)
    uint32_t ss2;
    // ... cr3, eip, eflags, general registers, segment selectors ...
    uint16_t iomap_base;    // Offset to I/O Permission Bitmap
};
```

The kernel initializes the TSS with `esp0` pointing to the top of the kernel
stack and `ss0` set to the kernel data segment selector.

### 3.4 The Complete Ring 3 → Ring 0 Transition

When ring 3 code executes `int 0x80`:

```
User mode (Ring 3)                     Kernel mode (Ring 0)
─────────────────                      ────────────────────

Shell executes:
  mov eax, 1          ; syscall number
  mov ebx, msg        ; argument 1
  int 0x80            ; ← triggers transition
       │
       ▼
CPU hardware (automatic, not software):
  1. Read IDT entry 0x80
  2. Check: CPL (3) ≤ DPL (3)?           → Yes, allowed
  3. Check: target segment DPL (0)?       → Ring switch needed
  4. Read TSS.esp0 and TSS.ss0            → Get kernel stack
  5. Save user stack:
     ┌──────────────────┐
     │  User SS         │  ← pushed by CPU onto KERNEL stack
     │  User ESP        │
     │  User EFLAGS     │
     │  User CS         │
     │  User EIP        │  ← return address
     └──────────────────┘
  6. Load SS:ESP = TSS.ss0:TSS.esp0       → Now on kernel stack
  7. Load CS:EIP = IDT[0x80] handler      → Now running ring 0 code
  8. Clear IF (interrupts disabled)
       │
       ▼
Kernel handler runs:                   ← Ring 0, kernel stack
  - Read EAX → dispatch table
  - Validate arguments
  - Perform operation (direct hardware access)
  - Place result in EAX
  - Execute IRET
       │
       ▼
CPU hardware (automatic, IRET):
  1. Pop EIP, CS, EFLAGS from kernel stack
  2. Detect CPL change (0 → 3)
  3. Pop ESP, SS from kernel stack        → Restore user stack
  4. Resume execution at user code
       │
       ▼
Shell continues at instruction after int 0x80
  - Result is in EAX
```

### 3.5 Setting Up the IDT Entry for Syscalls

```nasm
; ═══════════════════════════════════════════════════════════════════
; install_syscall_gate — Set up IDT entry 0x80 as a ring 3 callable
;                        interrupt gate pointing to ring 0 handler
; ═══════════════════════════════════════════════════════════════════
install_syscall_gate:
    ; IDT entry 0x80 is at offset 0x80 * 8 = 0x400 bytes into the IDT
    lea edi, [idt_table + 0x80 * 8]

    ; Low 16 bits of handler offset
    mov eax, syscall_handler_32
    mov word [edi + 0], ax              ; offset [15:0]

    ; Segment selector — 0x08 = first GDT entry after null (ring 0 code)
    mov word [edi + 2], 0x08

    ; Reserved byte
    mov byte [edi + 4], 0x00

    ; Type + DPL + Present
    ;   Type = 0xE (32-bit interrupt gate)
    ;   DPL  = 3  (ring 3 can invoke)
    ;   P    = 1  (present)
    ;   Byte = 1_11_0_1110 = 0xEE
    mov byte [edi + 5], 0xEE

    ; High 16 bits of handler offset
    shr eax, 16
    mov word [edi + 6], ax              ; offset [31:16]

    ret
```

### 3.6 The 32-bit Syscall Handler

```nasm
; ═══════════════════════════════════════════════════════════════════
; syscall_handler_32 — Protected-mode syscall dispatcher
;
; Calling convention:
;   EAX = syscall number
;   EBX = argument 1
;   ECX = argument 2
;   EDX = argument 3
;   ESI = argument 4
;   EDI = argument 5
;
; Returns:
;   EAX = return value (0 = success, negative = error)
;
; This handler runs in Ring 0 on the kernel stack.
; ═══════════════════════════════════════════════════════════════════
syscall_handler_32:
    push ds
    push es
    push fs
    push gs
    pushad                              ; Save all general-purpose registers

    ; Ensure kernel data segments are loaded
    mov cx, 0x10                        ; Kernel data segment selector
    mov ds, cx
    mov es, cx

    ; Dispatch based on EAX
    cmp eax, SYSCALL_MAX
    ja .invalid

    ; Call the handler from the dispatch table
    call [syscall_table + eax * 4]
    jmp .done

.invalid:
    mov eax, -1                         ; Error: unknown syscall

.done:
    ; EAX contains return value — write it into the saved register frame
    ; so it's restored by popad
    mov [esp + 28], eax                 ; Overwrite saved EAX in pushad frame

    popad
    pop gs
    pop fs
    pop es
    pop ds
    iret

; ─── Dispatch table ───────────────────────────────────────────────
syscall_table:
    dd sys_print_string                 ; 0
    dd sys_print_char                   ; 1
    dd sys_read_key                     ; 2
    dd sys_read_sector                  ; 3
    dd sys_get_version                  ; 4
SYSCALL_MAX equ ($ - syscall_table) / 4 - 1
```

### 3.7 The Same Syscalls, Now in C

In 32-bit protected mode, kernel syscall handlers are typically written in C.
The dispatch table calls C functions:

```c
/* ═══════════════════════════════════════════════════════════════ */
/* sys_print_string — Print a null-terminated string to VGA       */
/*                                                                */
/* In protected mode, BIOS int 10h is unavailable.  The kernel    */
/* writes directly to the VGA text-mode framebuffer at 0xB8000.   */
/* ═══════════════════════════════════════════════════════════════ */

#define VGA_BUFFER  ((volatile uint16_t *)0xB8000)
#define VGA_COLS    80
#define VGA_ROWS    25
#define VGA_WHITE   0x0F00

static int cursor_row = 0;
static int cursor_col = 0;

int sys_print_string(const char *str) {
    while (*str) {
        if (*str == '\n') {
            cursor_col = 0;
            cursor_row++;
        } else {
            int offset = cursor_row * VGA_COLS + cursor_col;
            VGA_BUFFER[offset] = VGA_WHITE | (uint8_t)*str;
            cursor_col++;
            if (cursor_col >= VGA_COLS) {
                cursor_col = 0;
                cursor_row++;
            }
        }
        /* TODO: scroll screen if cursor_row >= VGA_ROWS */
        str++;
    }
    return 0;
}

/* ═══════════════════════════════════════════════════════════════ */
/* sys_read_key — Read one keystroke from the keyboard            */
/*                                                                */
/* In protected mode, we read from the 8042 keyboard controller   */
/* via port I/O instead of BIOS int 16h.                          */
/* ═══════════════════════════════════════════════════════════════ */

#define KBD_DATA_PORT   0x60
#define KBD_STATUS_PORT 0x64
#define KBD_STATUS_OBF  0x01    /* Output buffer full — data available */

int sys_read_key(void) {
    uint8_t scancode;

    /* Wait for a key to be available */
    while (!(inb(KBD_STATUS_PORT) & KBD_STATUS_OBF))
        ;  /* spin */

    scancode = inb(KBD_DATA_PORT);

    /* TODO: translate scancode → ASCII using a keymap table */
    return scancode;
}
```

### 3.8 User-Mode Syscall Wrappers (C Library)

On the user side, syscalls are wrapped in C functions that set up registers
and invoke `int 0x80`:

```c
/* ═══════════════════════════════════════════════════════════════ */
/* User-mode syscall wrappers — mini-os libc                      */
/* These are the ONLY functions that execute the int 0x80          */
/* instruction.  All other user code calls these wrappers.         */
/* ═══════════════════════════════════════════════════════════════ */

static inline int syscall1(int num, int arg1) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)                     /* EAX = return value */
        : "a"(num), "b"(arg1)           /* EAX = syscall#, EBX = arg1 */
        : "memory"
    );
    return ret;
}

static inline int syscall0(int num) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num)
        : "memory"
    );
    return ret;
}

/* Public API */
int print(const char *str) { return syscall1(0, (int)str); }
int readkey(void)          { return syscall0(2); }
int version(void)          { return syscall0(4); }
```

### 3.9 Hardware Structures Summary

| Structure | Purpose | Set up by | Size |
|-----------|---------|-----------|------|
| **GDT** | Defines memory segments with privilege levels | Kernel (before mode switch) | 24+ bytes (3+ entries × 8) |
| **IDT** | Maps interrupt vectors to handlers with DPL gates | Kernel (after mode switch) | 2048 bytes (256 entries × 8) |
| **TSS** | Provides kernel stack pointer for ring transitions | Kernel (after mode switch) | 104 bytes |

The CPU knows where these structures are via dedicated registers:

```
GDTR ← loaded by LGDT instruction (base address + limit)
IDTR ← loaded by LIDT instruction (base address + limit)
TR   ← loaded by LTR instruction  (GDT selector for TSS)
```

---

## 4. 64-bit Long Mode — SYSCALL/SYSRET

### 4.1 Why Not INT 0x80?

The `int` instruction works in 64-bit mode, but it is **slow**.  On every
`int 0x80`, the CPU must:

1. Read the IDT entry (memory access)
2. Check the gate DPL (comparison)
3. Read the TSS for the kernel stack pointer (memory access)
4. Push 5 values onto the kernel stack (SS, RSP, RFLAGS, CS, RIP)
5. Load new CS:RIP from the IDT entry

This takes **~90–150 clock cycles** on modern CPUs — an eternity when syscalls
happen millions of times per second.

### 4.2 The SYSCALL Instruction

AMD introduced `syscall` in the AMD64 architecture specifically to replace
`int 0x80` for system calls.  It is a **dedicated, optimized instruction**
that eliminates the IDT and TSS lookups:

```
SYSCALL does:
  1. RCX ← RIP         (save return address — no stack push!)
  2. R11 ← RFLAGS      (save flags — no stack push!)
  3. RIP ← IA32_LSTAR  (load kernel entry point from MSR)
  4. CS  ← IA32_STAR[47:32]    (load kernel code segment)
  5. SS  ← IA32_STAR[47:32]+8  (load kernel data segment)
  6. RFLAGS &= ~IA32_FMASK     (mask out specified flags)
  7. CPL ← 0            (switch to ring 0)

SYSRET does (the reverse):
  1. RIP ← RCX         (restore return address)
  2. RFLAGS ← R11      (restore flags)
  3. CS ← IA32_STAR[63:48]+16  (load user code segment)
  4. SS ← IA32_STAR[63:48]+8   (load user data segment)
  5. CPL ← 3            (switch to ring 3)
```

This takes **~25–30 clock cycles** — roughly 4–5× faster than `int 0x80`.

### 4.3 MSR Setup

The kernel configures `syscall`/`sysret` by writing to Model-Specific
Registers (MSRs) during initialization:

```nasm
; ═══════════════════════════════════════════════════════════════════
; setup_syscall — Configure the SYSCALL/SYSRET MSRs
;
; MSRs used:
;   IA32_STAR   (0xC0000081) — Segment selectors for syscall/sysret
;   IA32_LSTAR  (0xC0000082) — Kernel entry point (RIP on SYSCALL)
;   IA32_FMASK  (0xC0000084) — RFLAGS bits to clear on SYSCALL
; ═══════════════════════════════════════════════════════════════════
setup_syscall:
    ; IA32_STAR: bits [47:32] = kernel CS (0x08), bits [63:48] = user CS base (0x18)
    ;   On SYSCALL: CS = 0x08 (kernel code), SS = 0x08+8 = 0x10 (kernel data)
    ;   On SYSRET:  CS = 0x18+16 = 0x28 (user code 64), SS = 0x18+8 = 0x20 (user data)
    mov ecx, 0xC0000081                 ; IA32_STAR
    xor edx, edx
    mov edx, 0x00180008                 ; [63:48]=0x0018 (user), [47:32]=0x0008 (kernel)
    xor eax, eax                        ; [31:0] = 0 (unused, reserved for 32-bit SYSCALL)
    wrmsr

    ; IA32_LSTAR: kernel entry point — where RIP goes on SYSCALL
    mov ecx, 0xC0000082                 ; IA32_LSTAR
    lea rax, [syscall_entry_64]
    mov rdx, rax
    shr rdx, 32                         ; High 32 bits
    wrmsr

    ; IA32_FMASK: clear IF on SYSCALL (disable interrupts on entry)
    mov ecx, 0xC0000084                 ; IA32_FMASK
    mov eax, 0x200                      ; Bit 9 = IF (interrupt flag)
    xor edx, edx
    wrmsr

    ret
```

### 4.4 The 64-bit Syscall Handler

```nasm
; ═══════════════════════════════════════════════════════════════════
; syscall_entry_64 — 64-bit syscall entry point
;
; On entry (set by CPU hardware):
;   RCX = user RIP (return address)
;   R11 = user RFLAGS
;   RAX = syscall number
;   RDI = arg1, RSI = arg2, RDX = arg3, R10 = arg4, R8 = arg5, R9 = arg6
;
; CRITICAL: RSP still points to user stack!  Must switch to kernel
;   stack immediately before doing anything else.
; ═══════════════════════════════════════════════════════════════════
syscall_entry_64:
    ; Save user stack and switch to kernel stack
    ; (kernel stack pointer stored in a per-CPU variable or the TSS)
    mov [gs:user_rsp], rsp              ; Save user RSP (gs: = per-CPU data)
    mov rsp, [gs:kernel_rsp]            ; Load kernel RSP

    ; Save user context on kernel stack
    push rcx                            ; User RIP
    push r11                            ; User RFLAGS
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15

    ; Arguments are already in the right registers for C calling convention:
    ;   RDI = arg1, RSI = arg2, RDX = arg3, R10 → RCX = arg4
    mov rcx, r10                        ; Fixup: SYSCALL clobbers RCX, arg4 is in R10

    ; Dispatch — RAX = syscall number
    cmp rax, SYSCALL_MAX_64
    ja .invalid

    lea rbx, [syscall_table_64]
    call [rbx + rax * 8]                ; Call handler (result in RAX)
    jmp .return

.invalid:
    mov rax, -1                         ; Error: unknown syscall

.return:
    ; Restore user context
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    pop r11                             ; User RFLAGS
    pop rcx                             ; User RIP

    ; Restore user stack
    mov rsp, [gs:user_rsp]

    ; Return to user mode
    sysretq                             ; RIP ← RCX, RFLAGS ← R11, CPL ← 3
```

### 4.5 Why SYSCALL Doesn't Switch the Stack

Unlike `int 0x80` (where the CPU reads the kernel stack from the TSS and
switches automatically), `syscall` does **not** switch the stack.  The kernel
entry point must do it manually — this is the very first thing the handler
does before touching any other memory.

This is intentional: it avoids the TSS memory read and makes the instruction
faster, but it puts the burden on the kernel to switch stacks immediately and
safely.

---

## 5. The Evolution of Syscall Mechanisms

### 5.1 Historical Timeline

```
Year  CPU           Mechanism       Speed        Notes
────  ────────────  ──────────────  ───────────  ─────────────────────────
1978  8086          INT n           ~50 cycles   Only option; no protection
1985  80386         INT n (prot.)   ~90 cycles   Now with ring transition via IDT/TSS
1996  Pentium Pro   SYSENTER/EXIT   ~40 cycles   Intel's fast syscall (32-bit only)
1997  AMD K6        SYSCALL/RET     ~30 cycles   AMD's fast syscall (32-bit, limited)
2003  AMD64         SYSCALL/RET     ~25 cycles   Redesigned for 64-bit; became standard
2005  Intel EM64T   SYSCALL/RET     ~25 cycles   Intel adopted AMD's design for x86-64
```

### 5.2 What Modern Operating Systems Use

| OS | 32-bit mode | 64-bit mode | Legacy fallback |
|----|-------------|-------------|-----------------|
| **Linux** | `sysenter` (preferred) or `int 0x80` | `syscall` | `int 0x80` via compat layer |
| **Windows** | `sysenter` (`KiFastSystemCall`) | `syscall` (`KiSystemCall64`) | `int 0x2E` (ancient, pre-XP) |
| **macOS** | N/A (64-bit only since Catalina) | `syscall` | — |
| **FreeBSD** | `int 0x80` or `sysenter` | `syscall` | `int 0x80` |

### 5.3 Windows Syscall Deep Dive

Windows is an interesting case because user code almost never invokes a
syscall directly.  The call chain:

```
Application (user code)
  │
  ├── Calls Win32 API:     ReadFile()          ← kernel32.dll
  │     │
  │     └── Calls Native:  NtReadFile()        ← ntdll.dll (still user-mode!)
  │           │
  │           │  This is the stub that actually transitions:
  │           │
  │           │  mov  r10, rcx            ; SYSCALL clobbers RCX
  │           │  mov  eax, 0x0006         ; Syscall number for NtReadFile
  │           │  syscall                  ; ← Ring 3 → Ring 0
  │           │
  │           ▼
  │       KiSystemCall64()                  ← ntoskrnl.exe (ring 0)
  │         │
  │         └── Dispatches to:  NtReadFile()  ← kernel implementation
  │               │
  │               └── Calls I/O manager → file system driver → disk driver
  │
  └── ReadFile() returns with data in the user's buffer
```

The **syscall numbers change with every Windows build** — they are not a
stable ABI.  This is why user code must go through ntdll.dll (which is
updated with each Windows version) rather than invoking `syscall` directly.

### 5.4 Linux Syscall Deep Dive

Linux takes the opposite approach — syscall numbers are a **stable ABI**
that never changes:

```
Application (user code)
  │
  ├── Calls libc:  write(fd, buf, len)      ← glibc / musl
  │     │
  │     └── Sets up registers:
  │           mov  rax, 1             ; __NR_write = 1 (stable, forever)
  │           mov  rdi, fd            ; arg1
  │           mov  rsi, buf           ; arg2
  │           mov  rdx, len           ; arg3
  │           syscall                 ; ← Ring 3 → Ring 0
  │           │
  │           ▼
  │       entry_SYSCALL_64            ← vmlinux (ring 0)
  │         │
  │         └── sys_call_table[1] → ksys_write() → vfs_write() → driver
  │
  └── write() returns number of bytes written (or -errno)
```

The **vDSO** (virtual Dynamic Shared Object) is a kernel-mapped page in
every process that provides optimized stubs.  Some "syscalls" like
`gettimeofday()` are handled entirely in user space via the vDSO, reading
kernel-maintained data without actually transitioning to ring 0.

---

## 6. Comparison of Mechanisms

### 6.1 Feature Matrix

| Feature | INT n (16-bit) | INT n (32-bit) | SYSENTER | SYSCALL |
|---------|---------------|----------------|----------|---------|
| Privilege transition | No | Yes (via IDT) | Yes (via MSR) | Yes (via MSR) |
| Stack switch | No | Automatic (TSS) | Automatic (MSR) | Manual (kernel code) |
| Saves RFLAGS | Yes (push) | Yes (push) | No (kernel must save) | Yes (→ R11) |
| Saves RIP | Yes (push) | Yes (push) | No (kernel must save) | Yes (→ RCX) |
| Lookup table | IVT (memory) | IDT (memory) | None (MSR) | None (MSR) |
| Vector number | Configurable | Configurable | Fixed (one entry) | Fixed (one entry) |
| Overhead | ~50 cycles | ~90 cycles | ~40 cycles | ~25 cycles |
| Bit depth | 16-bit | 32-bit | 32-bit only | 64-bit (native) |
| Return instruction | IRET | IRET | SYSEXIT | SYSRET |

### 6.2 Register Conventions

| | Syscall # | Arg 1 | Arg 2 | Arg 3 | Arg 4 | Arg 5 | Return |
|---|-----------|-------|-------|-------|-------|-------|--------|
| **16-bit (mini-os)** | AH | SI | DI | DX | BX | CX | AX |
| **32-bit Linux** | EAX | EBX | ECX | EDX | ESI | EDI | EAX |
| **32-bit mini-os** | EAX | EBX | ECX | EDX | ESI | EDI | EAX |
| **64-bit Linux** | RAX | RDI | RSI | RDX | R10 | R8 | RAX |
| **64-bit Windows** | RAX | RCX→R10 | RDX | R8 | R9 | stack | RAX |

---

## 7. mini-os Syscall Roadmap

### 7.1 Phase Summary

| Phase | CPU Mode | Mechanism | Dispatcher | Handlers | Protection |
|-------|----------|-----------|------------|----------|------------|
| 1 (16-bit) | Real mode | `int 0x80` → IVT | ASM branch chain | ASM (wrapping BIOS) | None (discipline) |
| 2 (32-bit) | Protected mode | `int 0x80` → IDT | ASM dispatch table | C functions | Hardware (ring 0/3) |
| 3 (64-bit) | Long mode | `syscall` → MSR | ASM dispatch table | C functions | Hardware (ring 0/3) |

### 7.2 Syscall Table

The same logical operations are available across all modes.  The calling
convention and mechanism change, but the **function numbers stay the same**:

| Number | Name | Description |
|--------|------|-------------|
| 0x01 | `SYS_PRINT_STRING` | Print null-terminated string |
| 0x02 | `SYS_PRINT_CHAR` | Print single character |
| 0x03 | `SYS_READ_KEY` | Wait for and return keystroke |
| 0x04 | `SYS_READ_SECTOR` | Read disk sector(s) |
| 0x05 | `SYS_GET_VERSION` | Return OS version |
| 0x06 | `SYS_CLEAR_SCREEN` | Clear screen and home cursor |
| 0x07 | `SYS_SET_CURSOR` | Set cursor position |
| 0x08 | `SYS_GET_CURSOR` | Get cursor position |
| 0x09 | `SYS_CHECK_A20` | Check A20 gate status |
| 0x0A | `SYS_GET_CONV_MEM` | Get conventional memory size |
| 0x0B | `SYS_GET_EXT_MEM` | Get extended memory size |
| 0x0C | `SYS_GET_E820` | Get E820 memory map |
| 0x0D | `SYS_REBOOT` | Reboot the system |
| 0x0E | `SYS_GET_DRIVE_INFO` | Get drive geometry/info |
| 0x0F | `SYS_GET_BIB` | Get BIOS Information Block |
| 0x10 | `SYS_PRINT_HEX8` | Print 8-bit hex value |
| 0x11 | `SYS_PRINT_HEX16` | Print 16-bit hex value |
| 0x12 | `SYS_PRINT_DEC16` | Print 16-bit decimal value |
| 0x13 | `SYS_WAIT_KEY` | Wait for keypress (no echo) |
| 0x14 | `SYS_GET_EQUIP` | Get equipment list |
| 0x15 | `SYS_GET_VIDEO` | Get video mode info |
| 0x16 | `SYS_GET_BDA_BYTE` | Read byte from BIOS Data Area |
| 0x17 | `SYS_GET_BDA_WORD` | Read word from BIOS Data Area |
| 0x18 | `SYS_CPUID` | Execute CPUID instruction |
| 0x19 | `SYS_CHECK_CPUID` | Check if CPUID is supported |
| 0x1A | `SYS_GET_EDD` | Get Enhanced Disk Drive info |
| 0x1B | `SYS_GET_IVT` | Get Interrupt Vector Table entry |
| 0x1C–0x1F | *(reserved)* | Reserved for future use |
| 0x20 | `SYS_DBG_PRINT` | Print tagged debug message (serial) |
| 0x21 | `SYS_DBG_HEX16` | Print tagged hex value (serial) |
| 0x22 | `SYS_DBG_REGS` | Dump registers with tag (serial) |
| 0x23 | `SYS_EXIT` | Terminate running program |
| 0x24 | `SYS_GET_ARGS` | Get command-line arguments (raw) |
| 0x25 | `SYS_GET_ARGC` | Get argument count |
| 0x26 | `SYS_GET_ARGV` | Get argument by index |
| 0x27 | `SYS_EXEC` | Execute program (overlay, no return) |
| 0x28 | `SYS_SPAWN` | Spawn child, reload parent on exit |

**Notes:**
- Debug syscalls (0x20–0x22) are no-ops in release builds. The caller passes
  a tag string (DS:BX) to identify the source module.
- Syscalls 0x1C–0x1F are reserved and will trap if called.
- See individual syscall documentation above for detailed calling conventions.

`SYS_EXEC` interface:
- **Input:** AH=0x27, DS:SI = 11-byte filename (8.3 padded uppercase), DS:DI = pointer to NUL-terminated args (0 = no args)
- **On success:** Does not return. New program replaces caller in TPA. New program's `ret` returns to shell.
- **On failure:** CF=1, AX = error code: 1=not found, 2=not executable, 3=too large, 4=read error, 5=bad header
- **Caller is safe on failure** — TPA is not modified until all validation passes.
- Post-load failures (disk error during read, corrupt header after load) cannot return to caller; they print an error and return to shell directly.

`SYS_SPAWN` interface:
- **Input:** AH=0x28, DS:SI = child's 11-byte filename, DS:DI = child's args (0 = no args), DS:BX = caller's own 11-byte filename
- **On success:** Does not return. Child program runs in TPA. When child exits (via `ret` or SYS_EXIT), the kernel reloads the parent from disk and restarts it fresh (no state preserved).
- **On failure:** CF=1, AX = error code (same codes as SYS_EXEC; 4 = nesting too deep)
- **Semantics:** The kernel pushes BX (parent filename) onto `spawn_parent_stack[spawn_depth]` and increments `spawn_depth`. On the outermost spawn (depth was 0), it saves the shell return address to `spawn_saved_ret` and installs a trampoline; nested spawns reuse the existing trampoline. It then executes the child identically to SYS_EXEC. When the child exits, SYS_EXIT decrements `spawn_depth`, reloads the parent at that index, and either restores the shell return (if outermost) or re-installs the trampoline (if still nested).
- **Nesting:** Up to `SPAWN_MAX_DEPTH` (4) levels supported (e.g., mnmon→mnmon→mnmon→edit). Exceeding the limit returns CF=1, AX=4.
- **Rollback on failure:** If the child file cannot be loaded (pre-load error), `spawn_rollback_if_pending` undoes the depth increment and trampoline so the caller can continue.
- **Use case:** MNMON's `x` command — launches a program, then MNMON is reloaded when the program finishes.

**Filesystem write syscalls (INT 0x81, AH=0x06–0x09):** Implemented in v0.9.11
(0x06–0x08) and v0.9.17 (0x09). These extend the filesystem module with write
support — creating, deleting, renaming, and atomically replacing files at
runtime.  All use `syscall_ret_cf` for CF propagation.  Error codes returned in
AL when CF=1 (see `doc/FILESYSTEM.md` §8.10).  All FS handlers obey the **FS ABI
Contract v1** documented at the top of `src/fs/fs.asm` and in `doc/FILESYSTEM.md`
§8.1: full 32-bit register preservation except documented outputs.

| 0x06 | `FS_WRITE_FILE`   | Create new file (fails on duplicate)            | DS:SI=name, ES:BX=data, ECX=size, DL=attr; CF+AL on error | INT 0x81 |
| 0x07 | `FS_DELETE_FILE`  | Delete file (tombstone)                         | DS:SI=name; CF+AL on error                                | INT 0x81 |
| 0x08 | `FS_RENAME_FILE`  | Rename file                                     | DS:SI=old, ES:DI=new; CF+AL on error                      | INT 0x81 |
| 0x09 | `FS_REPLACE_FILE` | Atomic create-or-replace (data-first, then dir) | DS:SI=name, ES:BX=data, ECX=size, DL=attr; CF+AL on error | INT 0x81 |

User-mode programs should prefer the named `mn_*` helpers in `src/include/mnoslib.inc`
(or just one of the split headers — `mnoslib_io.inc`, `mnoslib_sys.inc`,
`mnoslib_fs.inc`, `mnoslib_mm.inc`) over hand-rolled `mov ah, SYS_X / int 0xNN`
sequences.  The wrappers are pure 1:1 with the underlying syscalls (no register
contract change) and give the call site a self-documenting name.  See
`doc/MNOSLIB.md` for the full catalog.

### 7.3 Stability Guarantee

Following the Linux model (not Windows), **syscall numbers are a stable ABI**.
Once a number is assigned, it never changes.  New syscalls are added at the
end.  This means a 16-bit MNEX executable can be "conceptually" source-
compatible with its 32-bit counterpart — the function numbers are the same,
only the register convention and invocation instruction differ.

### 7.3.1 ABI Notes — The AH Register Overlap

In the 16-bit phase, AH carries the syscall function number.  Since AH is
bits 8-15 of AX (and EAX), any syscall that also passes data in AX or EAX
has a collision — `mov ah, SYS_xxx` silently clobbers the caller's value.

Three syscalls were affected and their input registers were changed in v0.6.0:

| Syscall | Old Input | New Input | Reason |
|---------|-----------|-----------|--------|
| `SYS_READ_SECTOR` (0x04) | EAX = LBA | **EDI** = LBA | AH clobbered bits 8-15 of LBA |
| `SYS_PRINT_HEX16` (0x11) | AX = value | **DX** = value | AH clobbered high byte of value |
| `SYS_PRINT_DEC16` (0x12) | AX = value | **DX** = value | AH clobbered high byte of value |

Syscalls that use AL only (e.g., `SYS_PRINT_CHAR`, `SYS_PRINT_HEX8`) are
unaffected because AL does not overlap AH.

### 7.3.2 CF Propagation — `syscall_ret_cf`

Syscall handlers that return a success/failure status via the carry flag (CF)
cannot use `iret` to return.  `IRET` pops the caller's saved FLAGS from the
stack, discarding the handler's CF.  Instead, these handlers use:

```nasm
%macro syscall_ret_cf 0
    sti                  ; Re-enable interrupts (INT clears IF)
    retf 2               ; Pop IP+CS, skip saved FLAGS (+2)
%endmacro                ; Caller sees handler's FLAGS (with CF)
```

This applies to: `.fn_read_sector`, `.fn_get_ext_mem`, `.fn_get_e820`,
`.fn_get_drive_info`, `.fn_get_edd`, `.fn_exec`, `.fn_spawn`, and `.sc_unknown`.

### 7.4 C Language Bindings

The 16-bit phase (Phase 1) requires NASM assembly because **Clang does not
support 16-bit x86 code generation**.  Starting in Phase 2 (32-bit protected
mode), both kernel handlers and user-mode executables can be written in C.
The `int 0x80` syscall mechanism works identically from C via inline assembly.

#### Why Clang Can't Target 16-bit

Clang's x86 backend targets `i686` (32-bit) and `x86_64` (64-bit) only.
There is no `--target=i8086-elf` or real-mode codegen.  GCC has the same
limitation.  The 16-bit kernel and shell must remain in NASM assembly.

This is actually a clean architectural boundary: 16-bit is the "bootstrap
and learning" phase where assembly is the natural language, while 32/64-bit
is where C becomes practical and necessary.

#### User-Mode Syscall Library (`mnos_syscall.h`)

When user-mode code transitions to C, every syscall gets a thin inline
wrapper.  Application code never writes `int $0x80` directly — it calls
typed C functions from this header:

```c
/* ═══════════════════════════════════════════════════════════════ */
/* mnos_syscall.h — mini-os user-mode syscall wrappers            */
/*                                                                */
/* This header is the ONLY place where 'int $0x80' (32-bit) or   */
/* 'syscall' (64-bit) appears in user code.  Every application    */
/* and the shell include this header — they never touch hardware  */
/* directly.                                                      */
/*                                                                */
/* The syscall numbers match the kernel's dispatch table and are  */
/* stable across versions (see §7.3 Stability Guarantee).         */
/* ═══════════════════════════════════════════════════════════════ */

#ifndef MNOS_SYSCALL_H
#define MNOS_SYSCALL_H

#include <stdint.h>

/* --- Syscall function numbers (must match kernel) --------------- */
#define SYS_PRINT_STRING   0x01
#define SYS_PRINT_CHAR     0x02
#define SYS_READ_KEY       0x03
#define SYS_READ_SECTOR    0x04
#define SYS_GET_VERSION    0x05
#define SYS_CLEAR_SCREEN   0x06
#define SYS_SET_CURSOR     0x07
#define SYS_GET_CURSOR     0x08
#define SYS_REBOOT         0x0D

/* --- Low-level syscall invokers --------------------------------- */

/* 32-bit: uses int $0x80, function number in EAX */
#ifdef __i386__

static inline int32_t _syscall0(uint32_t num) {
    int32_t ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(num)
        : "memory"
    );
    return ret;
}

static inline int32_t _syscall1(uint32_t num, uint32_t arg1) {
    int32_t ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(arg1)
        : "memory"
    );
    return ret;
}

static inline int32_t _syscall2(uint32_t num, uint32_t a1, uint32_t a2) {
    int32_t ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(a1), "c"(a2)
        : "memory"
    );
    return ret;
}

#endif /* __i386__ */

/* 64-bit: uses syscall instruction, function number in RAX */
#ifdef __x86_64__

static inline int64_t _syscall0(uint64_t num) {
    int64_t ret;
    __asm__ volatile(
        "syscall"
        : "=a"(ret)
        : "a"(num)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline int64_t _syscall1(uint64_t num, uint64_t arg1) {
    int64_t ret;
    __asm__ volatile(
        "syscall"
        : "=a"(ret)
        : "a"(num), "D"(arg1)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline int64_t _syscall2(uint64_t num, uint64_t a1, uint64_t a2) {
    int64_t ret;
    __asm__ volatile(
        "syscall"
        : "=a"(ret)
        : "a"(num), "D"(a1), "S"(a2)
        : "rcx", "r11", "memory"
    );
    return ret;
}

#endif /* __x86_64__ */

/* --- Public API ------------------------------------------------- */
/* These are the functions user code actually calls.               */

static inline void print(const char *s)  { _syscall1(SYS_PRINT_STRING, (uintptr_t)s); }
static inline void putchar(char c)       { _syscall1(SYS_PRINT_CHAR, (uint8_t)c); }
static inline int  readkey(void)         { return _syscall0(SYS_READ_KEY); }
static inline void cls(void)             { _syscall0(SYS_CLEAR_SCREEN); }
static inline void reboot(void)          { _syscall0(SYS_REBOOT); }
static inline int  version(void)         { return _syscall0(SYS_GET_VERSION); }
static inline void set_cursor(int r, int c) {
    _syscall2(SYS_SET_CURSOR, (uint32_t)r, (uint32_t)c);
}

#endif /* MNOS_SYSCALL_H */
```

#### Example: A User-Mode C Program

```c
/* hello.c — minimal MNEX user-mode executable */
#include "mnos_syscall.h"

void _start(void) {
    cls();
    print("Hello from C!\n");
    print("mini-os version: ");

    int ver = version();
    /* ver: high byte = major, low byte = minor */
    putchar('0' + (ver >> 8));
    putchar('.');
    putchar('0' + (ver & 0xFF));
    putchar('\n');

    print("Press any key to continue...\n");
    readkey();
    reboot();
}
```

#### Build Pipeline for C User-Mode Executables

```
hello.c ──── clang --target=i686-elf ──→ hello.o
                                            │
                                       ld.lld -T user.ld
                                            │
                                       hello.elf
                                            │
                                       llvm-objcopy -O binary
                                            │
                                       hello.raw (flat binary)
                                            │
                                       tools/wrap-mnex.ps1 -Magic MNEX -CpuMode 1
                                            │
                                       hello.bin (MNEX header + flat binary)
```

**Clang flags** (user-mode — same freestanding flags as the kernel):
```
clang                          \
    --target=i686-elf          \  32-bit x86, no OS
    -ffreestanding             \  No standard library assumptions
    -fno-builtin               \  Don't replace code with libc calls
    -nostdlib                  \  Don't link standard C library
    -nostdinc                  \  Don't search system include paths
    -fno-stack-protector       \  No stack canaries
    -fno-pic                   \  No position-independent code
    -O2 -Wall -Wextra          \  Optimise + warnings
    -c hello.c -o hello.o         Compile only
```

**Linker script** (`user.ld`):
```ld
OUTPUT_FORMAT(elf32-i386)
ENTRY(_start)

SECTIONS {
    /* Skip 32 bytes for MNEX header (added by wrap-mnex.ps1) */
    . = 0x20;

    .text : { *(.text) }
    .rodata : { *(.rodata*) }
    .data : { *(.data) }
    .bss : { *(.bss) }
}
```

#### Transition Strategy: 16-bit ASM → 32-bit C

The transition from Phase 1 to Phase 2 is designed to be smooth:

| Aspect | Phase 1 (16-bit) | Phase 2 (32-bit) |
|--------|-------------------|-------------------|
| **Kernel language** | NASM assembly | C + NASM entry stub |
| **Shell language** | NASM assembly | C (with `mnos_syscall.h`) |
| **Syscall mechanism** | `int 0x80` via IVT | `int 0x80` via IDT |
| **Syscall numbers** | AH register | EAX register |
| **Function numbers** | Same (0x01, 0x02, ...) | Same (0x01, 0x02, ...) |
| **Compiler** | NASM only | Clang + NASM |
| **Binary format** | MNEX 32-byte header | MNEX 32-byte header (identical) |

The key insight: **the syscall numbers are the stable contract**.  A `print`
call in 16-bit assembly (`mov ah, 0x01 / int 0x80`) and in 32-bit C
(`_syscall1(0x01, str)`) use the same function number.  Only the delivery
mechanism (register width, IVT vs IDT) changes — and that's handled
transparently by the inline assembly in `mnos_syscall.h`.

---

## 8. References

| Document | Relevance |
|----------|-----------|
| 📄 [CPU-MODES-AND-TRANSITIONS.md](CPU-MODES-AND-TRANSITIONS.md) | GDT, IDT, ring model, mode switch sequences |
| 📄 [MEMORY-LAYOUT.md](MEMORY-LAYOUT.md) | IVT location, stack layout, kernel memory regions |
| 📄 [MNEX-BINARY-FORMAT.md](MNEX-BINARY-FORMAT.md) | Binary format for kernel and executables |
| 📄 [DESIGN.md](DESIGN.md) | Overall architecture and boot flow |
| 📄 [BOOT-LAYOUT-RATIONALE.md](BOOT-LAYOUT-RATIONALE.md) | Why the loader exists between VBR and kernel |
| 📖 Intel SDM Vol. 3A, Ch. 6 | Interrupt and Exception Handling (IDT, gates) |
| 📖 Intel SDM Vol. 2, SYSCALL | SYSCALL/SYSRET instruction reference |
| 📖 AMD APM Vol. 2, Ch. 6 | SYSCALL/SYSRET architecture |
| 📖 OSDev Wiki: System Calls | https://wiki.osdev.org/System_Calls |
