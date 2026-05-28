# Debugging Infrastructure — Design Document

**Version:** 1.4
**Status:** Partially implemented — v0.7.0 (serial, tracing, build mode), v0.7.1 (user-mode debug syscalls), v0.7.2 (assert macros), v0.7.4 (fault handlers), v0.8.1 (stack canary), v0.9.6 (INT depth, DAP diagnostics)
**Audience:** mini-os developers

---

## 1. Motivation

The v0.6.0 development cycle exposed a class of bugs that were extremely
difficult to diagnose:

| Bug | Symptom | Root Cause | Debugging Time |
|-----|---------|------------|----------------|
| AH register overlap | FS init silently failed | `mov ah, SYS_xxx` clobbered LBA bits 8-15 | Hours |
| CF propagation | Error flag silently lost | `iret` restored caller's FLAGS, not handler's | Hours |
| Wrong `dir` values | All files showed 4610 sectors | Same AH overlap in print functions | 30+ min |

Each of these bugs had the same profile:

1. **Silent corruption** — no crash, no visible error, just wrong behavior.
2. **Distant effect** — the corruption happened in the kernel, but the symptom
   appeared in user-mode code.
3. **No trace** — with only screen output available, there was no way to see
   what registers contained at the moment of the bug.

Real operating systems solve this with layered debugging infrastructure.  This
document designs seven facilities for mini-os, ordered by impact.

---

## 2. Overview of Facilities

```
┌──────────────────────┬──────────────────────────────────────────┬───────────────┐
│                      │  Debugging Facilities                   │ Status        │
├──────────────────────┼──────────────────────────────────────────┼───────────────┤
│ § 3  Serial Debug Log│ COM1 (0x3F8) output for all debug msgs  │ ✅ v0.7.0     │
│ § 4  Syscall Tracing │ Log every INT 0x80/0x81 with names      │ ✅ v0.7.0     │
│ § 4b User-Mode Debug │ SYS_DBG_PRINT/HEX16/REGS with tags     │ ✅ v0.7.1     │
│ § 5  Assert Macros   │ Compile-time condition checks           │ ✅ v0.7.2     │
│ § 6  Fault Handlers  │ Trap CPU exceptions with state dump     │ ✅ v0.7.4     │
│ § 7  Machine Monitor │ WinDbg-style memory monitor (MNMON.MNX)  │ ✅ v0.9.7     │
│ § 8  Debug Build Mode│ %ifdef DEBUG conditional assembly       │ ✅ v0.7.0     │
│ § 9  Stack Canary    │ Corruption sentinel at stack floor      │ ✅ v0.8.1     │
└──────────────────────┴──────────────────────────────────────────┴───────────────┘
```

**Dependency graph:**

```
Debug Build Mode (§ 8) ──→ controls all others via %ifdef DEBUG
        │
        ├── Serial Debug Log (§ 3)  ←── foundation for all logging
        │       │
        │       ├── Syscall Tracing (§ 4)   ←── uses serial output
        │       ├── Assert Macros (§ 5)     ←── uses serial output
        │       ├── Fault Handlers (§ 6)    ←── uses serial output
        │       └── Stack Canary (§ 9)      ←── uses serial output
        │
        └── Machine Monitor (§ 7)  ←── always-on (uses screen, not serial)
```

Serial logging (§3) is the foundation — every other logging facility writes
its output through the serial port so it doesn't interfere with screen output.

---

## 3. Serial Debug Log *(implemented in v0.7.0)*

### 3.1 Why Serial?

Screen output (INT 10h / BIOS teletype) is the only output channel mini-os
currently has.  This creates two problems:

1. **Debug messages corrupt the user experience** — boot messages, syscall
   traces, and assert failures clutter the screen and scroll important output
   away.
2. **Output is ephemeral** — once text scrolls off the 25-row VGA screen,
   it's gone.  There's no scrollback, no log file, no history.

Serial port output solves both:

- **Invisible to the user** — serial data goes to COM1 (I/O port 0x3F8),
  completely independent of the VGA display.
- **Capturable** — Hyper-V can connect a VM's COM1 to a named pipe on the
  host.  Any terminal emulator (PuTTY, screen, PowerShell) can read the pipe
  and save it to a file.  You get a permanent, complete log of everything
  the OS did.
- **Works before everything** — serial output requires no BIOS, no INT 10h,
  no memory manager.  It's direct port I/O.  It works from the very first
  instruction of the MBR.

### 3.2 x86 Serial Port (UART 8250/16550) Primer

Every PC-compatible system has at least one UART (Universal Asynchronous
Receiver/Transmitter) mapped to I/O ports.  The standard assignments are:

| Port | Name | I/O Base | IRQ |
|------|------|----------|-----|
| COM1 | Serial Port 1 | 0x3F8 | IRQ 4 |
| COM2 | Serial Port 2 | 0x2F8 | IRQ 3 |
| COM3 | Serial Port 3 | 0x3E8 | IRQ 4 |
| COM4 | Serial Port 4 | 0x2E8 | IRQ 3 |

Each UART has 8 registers at consecutive I/O ports:

```
Offset  Register (DLAB=0)         Register (DLAB=1)
──────  ────────────────────────  ─────────────────────────
+0      THR (Transmit Hold)       DLL (Divisor Latch Low)
+1      IER (Interrupt Enable)    DLM (Divisor Latch High)
+2      IIR (Interrupt Identify)  FCR (FIFO Control)
+3      LCR (Line Control)
+4      MCR (Modem Control)
+5      LSR (Line Status)         ← Bit 5 = TX buffer empty
+6      MSR (Modem Status)
+7      Scratch Register
```

**DLAB** (Divisor Latch Access Bit) is bit 7 of the LCR register.  When set,
ports +0 and +1 become the baud rate divisor registers instead of THR/IER.

### 3.3 Initialization Sequence

Before sending any data, the UART must be configured.  The initialization
sequence:

```nasm
; =============================================================================
; serial_init — Initialize COM1 at 115200 baud, 8N1
;
; Must be called once during early boot (LOADER or KERNEL init).
; No BIOS calls needed — pure port I/O.
; =============================================================================

COM1_BASE   equ 0x3F8
COM1_THR    equ COM1_BASE + 0      ; Transmit Holding Register
COM1_IER    equ COM1_BASE + 1      ; Interrupt Enable Register
COM1_FCR    equ COM1_BASE + 2      ; FIFO Control Register
COM1_LCR    equ COM1_BASE + 3      ; Line Control Register
COM1_MCR    equ COM1_BASE + 4      ; Modem Control Register
COM1_LSR    equ COM1_BASE + 5      ; Line Status Register

serial_init:
    ; Step 1: Disable all UART interrupts
    ;   We're polling, not using IRQs.  Clear IER to prevent
    ;   spurious interrupts from the UART.
    mov dx, COM1_IER
    xor al, al                          ; 0x00 = no interrupts
    out dx, al

    ; Step 2: Set DLAB to access baud rate divisor
    ;   Writing 0x80 to LCR sets the DLAB bit.  Now ports +0/+1
    ;   become the divisor latch instead of THR/IER.
    mov dx, COM1_LCR
    mov al, 0x80                        ; DLAB = 1
    out dx, al

    ; Step 3: Set baud rate to 115200
    ;   The UART clock runs at 1.8432 MHz.  The divisor is:
    ;     divisor = 1843200 / (16 × baud_rate)
    ;   For 115200 baud: divisor = 1843200 / 1843200 = 1
    ;   Write low byte (1) to DLL, high byte (0) to DLM.
    mov dx, COM1_BASE                   ; DLL (port +0, DLAB=1)
    mov al, 1                           ; Divisor low byte = 1
    out dx, al
    mov dx, COM1_BASE + 1              ; DLM (port +1, DLAB=1)
    xor al, al                          ; Divisor high byte = 0
    out dx, al

    ; Step 4: Configure line format: 8 data bits, no parity, 1 stop bit (8N1)
    ;   LCR bits:
    ;     [1:0] = 11  → 8 data bits
    ;     [2]   = 0   → 1 stop bit
    ;     [5:3] = 000 → no parity
    ;     [7]   = 0   → clear DLAB (back to normal THR/IER)
    mov dx, COM1_LCR
    mov al, 0x03                        ; 8N1, DLAB = 0
    out dx, al

    ; Step 5: Enable and clear FIFOs
    ;   FCR bits:
    ;     [0] = 1 → enable FIFOs
    ;     [1] = 1 → clear receive FIFO
    ;     [2] = 1 → clear transmit FIFO
    ;     [7:6] = 11 → 14-byte trigger level
    mov dx, COM1_FCR
    mov al, 0xC7                        ; Enable + clear FIFOs, 14-byte trigger
    out dx, al

    ; Step 6: Configure modem control
    ;   MCR bits:
    ;     [0] = DTR (Data Terminal Ready)
    ;     [1] = RTS (Request To Send)
    ;     [3] = OUT2 (required for interrupts on some UARTs)
    ;   We set DTR + RTS + OUT2 even though we're polling — some
    ;   virtual COM implementations check DTR/RTS.
    mov dx, COM1_MCR
    mov al, 0x0B                        ; DTR + RTS + OUT2
    out dx, al

    ret
```

### 3.4 Sending a Character

Sending a byte requires waiting for the transmit buffer to be empty (bit 5
of the Line Status Register), then writing the byte to the Transmit Holding
Register:

```nasm
; =============================================================================
; serial_putc — Send one character to COM1
;
; Input:  AL = character to send
; Output: none
; Clobbers: DX (saved/restored if caller needs it preserved)
; =============================================================================
serial_putc:
    push dx
    push ax                             ; Save the character

    ; Wait for transmit buffer empty (LSR bit 5)
    mov dx, COM1_LSR
.wait_tx:
    in al, dx                           ; Read Line Status Register
    test al, 0x20                       ; Bit 5: Transmit Holding Register Empty?
    jz .wait_tx                         ; Spin until ready

    ; Send the character
    pop ax                              ; Restore character
    mov dx, COM1_THR
    out dx, al                          ; Write byte to transmit register

    pop dx
    ret
```

### 3.5 Higher-Level Logging Functions

Building on `serial_putc`, we create a family of helpers:

```nasm
; serial_puts — Send NUL-terminated string to COM1
;   Input: SI = pointer to string
serial_puts:
    push ax
    push si
.loop:
    lodsb                               ; AL = [SI], SI++
    test al, al
    jz .done
    call serial_putc
    jmp .loop
.done:
    pop si
    pop ax
    ret

; serial_hex16 — Send AX as 4-digit hex string to COM1
;   Input: AX = 16-bit value
serial_hex16:
    push cx
    push ax
    mov cx, 4
.hex_loop:
    rol ax, 4                           ; Rotate highest nibble into lowest
    push ax
    and al, 0x0F
    add al, '0'
    cmp al, '9'
    jbe .digit
    add al, 7                           ; 'A'-'9'-1 = 7
.digit:
    call serial_putc
    pop ax
    dec cx
    jnz .hex_loop
    pop ax
    pop cx
    ret

; serial_hex8 — Send AL as 2-digit hex string to COM1
;   Input: AL = 8-bit value
serial_hex8:
    push ax
    push cx
    mov cx, 2
    rol al, 4                           ; High nibble first
.hex8_loop:
    push ax
    and al, 0x0F
    add al, '0'
    cmp al, '9'
    jbe .hex8_digit
    add al, 7
.hex8_digit:
    call serial_putc
    pop ax
    rol al, 4
    dec cx
    jnz .hex8_loop
    pop cx
    pop ax
    ret

; serial_crlf — Send CR+LF to COM1
serial_crlf:
    push ax
    mov al, 13
    call serial_putc
    mov al, 10
    call serial_putc
    pop ax
    ret
```

### 3.6 The `DBG` Macro

All debug logging should go through a single macro so it can be compiled
out in release builds:

```nasm
; --- In debug.inc ---

%ifdef DEBUG

; DBG — Print a debug message to serial (compile-time string)
;   Usage:  DBG "Loading FS.SYS"
;   Output: [DBG] Loading FS.SYS\r\n    (to COM1)
;
; This macro generates an inline string constant and calls serial_puts.
; The string is embedded in the code stream with a jmp to skip over it.
%macro DBG 1
    jmp %%after
    %%msg: db '[DBG] ', %1, 13, 10, 0
%%after:
    push si
    mov si, %%msg
    call serial_puts
    pop si
%endmacro

; DBG_REG — Print a register name and its hex value to serial
;   Usage:  DBG_REG "AX", ax
;   Output: AX=1234    (to COM1, no newline)
%macro DBG_REG 2
    jmp %%after
    %%lbl: db %1, '=', 0
%%after:
    push si
    push ax
    mov si, %%lbl
    call serial_puts
    mov ax, %2
    call serial_hex16
    mov al, ' '
    call serial_putc
    pop ax
    pop si
%endmacro

; DBG_REGS — Dump all general-purpose registers to serial
;   Usage:  DBG_REGS
;   Output: AX=xxxx BX=xxxx CX=xxxx DX=xxxx SI=xxxx DI=xxxx\r\n
%macro DBG_REGS 0
    DBG_REG "AX", ax
    DBG_REG "BX", bx
    DBG_REG "CX", cx
    DBG_REG "DX", dx
    DBG_REG "SI", si
    DBG_REG "DI", di
    call serial_crlf
%endmacro

%else
; Release build — all debug macros expand to nothing
%macro DBG 1
%endmacro
%macro DBG_REG 2
%endmacro
%macro DBG_REGS 0
%endmacro
%endif
```

### 3.7 Hyper-V COM1 Setup

COM1 is automatically configured by `setup-vm.bat` — no manual steps needed.
The setup script runs `Set-VMComPort` to map COM1 to `\\.\pipe\minios-serial`
on both new and existing VMs.

#### Reading serial output

Use the included `read-serial.bat` (requires admin):

```cmd
read-serial.bat              :: uses VM name "mini-os"
read-serial.bat my-vm        :: custom VM name
```

The script:
1. Stops the VM (if running)
2. Starts the VM fresh
3. Immediately connects to the COM1 pipe — capturing boot messages from the
   very first byte
4. Auto-reconnects on VM reboot or reset (waits up to 30 seconds)
5. Press Ctrl+C to stop

> **Note:** The Hyper-V Manager thumbnail may appear gray while
> `read-serial.bat` is connected.  The VM is running normally — open the
> console with `vmconnect localhost mini-os` (or double-click the VM in
> Hyper-V Manager) to see the display output alongside the serial log.

> **Note:** PuTTY cannot connect to Windows named pipes.  Use
> `read-serial.bat` or the PowerShell snippet below.

For manual use without the helper script:

```powershell
# Read the pipe in real-time (VM must already be running)
$pipe = [System.IO.Pipes.NamedPipeClientStream]::new(".", "minios-serial",
    [System.IO.Pipes.PipeDirection]::In)
$pipe.Connect(5000)
$reader = [System.IO.StreamReader]::new($pipe)
while (-not $reader.EndOfStream) { $reader.ReadLine() }
```

**Actual output** from a debug boot (v0.7.0):

```
[DBG] KERNEL: serial debug active       ← serial_init completed, COM1 ready
[DBG] KERNEL: INT 0x80 installed        ← syscall jump table wired into IVT
[SYS] READ_SECTOR AX=0400 BX=0983      ← FS.SYS loading directory sector
[DBG] KERNEL: FS.SYS loaded at 0x0800   ← filesystem module in memory
[DBG] KERNEL: INT 0x81 filesystem ready ← FS INT 0x81 handler installed
[DBG] KERNEL: SHELL.SYS loaded          ← shell binary loaded at 0x3000
[SYS] CLEAR_SCREEN AX=060C BX=0000     ← shell clearing screen
[SYS] PRINT_STRING AX=010C BX=0000     ← shell printing banner
[SYS] PRINT_STRING AX=010C BX=0000     ← shell printing prompt
[SYS] READ_KEY AX=030C BX=0000         ← shell waiting for keypress
```

### 3.8 Size Budget

The serial infrastructure adds code to every binary that includes it:

| Component | Size |
|-----------|------|
| `serial_init` | ~40 bytes |
| `serial_putc` | ~20 bytes |
| `serial_puts` | ~16 bytes |
| `serial_hex16` | ~30 bytes |
| `serial_hex8` | ~24 bytes |
| `serial_crlf` | ~12 bytes |
| Per `DBG` call | ~20 bytes (jmp + string + push/call/pop) |

Under `%ifdef DEBUG`, all of this exists.  In release builds, it compiles to
exactly 0 bytes.

---

## 4. Syscall Tracing *(implemented in v0.7.0; §4.7 in v0.7.1)*

### 4.1 The Problem

When the shell calls `INT 0x80` with `AH=0x04` (read sector), the kernel's
handler executes.  If the handler receives wrong input (as happened with the
AH clobber bug), there is no record of what was passed.  By the time the
caller notices something is wrong, the original register values are lost.

### 4.2 The Solution: Named Trace on Entry

The syscall dispatcher (`syscall_handler` in kernel.asm) is the single point
through which every syscall passes.  Adding a trace here captures every call
with human-readable function names:

```nasm
syscall_handler:
    mov [cs:.sc_temp], bx               ; Save BX (existing)

%ifdef DEBUG
    ; Look up syscall name from pointer table, print:
    ;   [SYS] PRINT_STRING AX=010C BX=0000
    ; Falls back to [SYS] AH=xx for unknown function numbers.
    ;
    ; Name table: 35 dw pointers to NUL-terminated strings,
    ; indexed by AH value (0x00–0x22).
%endif

    movzx bx, ah                        ; BX = function number (existing)
    cmp bx, SYSCALL_MAX
    ja .sc_unknown
    ; ... rest of dispatch ...
```

The name table adds ~440 bytes in debug builds (30 name strings + 35-entry
pointer table) but makes output immediately readable without a reference card.

### 4.3 Trace Output — Boot Sequence Reference

A debug build produces the following serial output during a normal boot.
Each line is explained:

```
[DBG] KERNEL: serial debug active       ← serial_init completed, COM1 ready
[DBG] KERNEL: INT 0x80 installed        ← syscall jump table wired into IVT
[SYS] READ_SECTOR AX=0400 BX=0983      ← FS.SYS loading directory sector
[DBG] KERNEL: FS.SYS loaded at 0x0800   ← filesystem module in memory
[FS]  LIST_FILES                        ← FS caching directory during init
[DBG] KERNEL: INT 0x81 filesystem ready ← FS INT 0x81 handler installed
[DBG] KERNEL: SHELL.SYS loaded          ← shell binary loaded at 0x3000
[SYS] CLEAR_SCREEN AX=060C BX=0000     ← shell clearing screen
[SYS] PRINT_STRING AX=010C BX=0000     ← shell printing banner
[SYS] PRINT_STRING AX=010C BX=0000     ← shell printing prompt
[SYS] READ_KEY AX=030C BX=0000         ← shell waiting for keypress
```

**Trace format:**

| Prefix | Source | Format | Example |
|--------|--------|--------|---------|
| `[DBG]` | `DBG` macro | `[DBG] <message>` | `[DBG] KERNEL: INT 0x80 installed` |
| `[SYS]` | INT 0x80 dispatcher | `[SYS] <NAME> AX=xxxx BX=xxxx` | `[SYS] READ_SECTOR AX=0400 BX=2000` |
| `[FS]`  | INT 0x81 dispatcher | `[FS] <NAME>` | `[FS] FIND_FILE` |

**INT 0x80 syscall name reference (AH → name):**

| AH | Name | Description |
|----|------|-------------|
| 0x01 | `PRINT_STRING` | Print NUL-terminated string at DS:SI |
| 0x02 | `PRINT_CHAR` | Print character in AL |
| 0x03 | `READ_KEY` | Wait for keypress, returns AH=scan AL=ASCII |
| 0x04 | `READ_SECTOR` | Read disk sector (EDI=LBA, ES:BX=buf, CL=count) |
| 0x05 | `GET_VERSION` | Returns AH=major, AL=minor |
| 0x06 | `CLEAR_SCREEN` | Clear display |
| 0x07 | `SET_CURSOR` | Set cursor position (DH=row, DL=col) |
| 0x08 | `GET_CURSOR` | Get cursor position |
| 0x09 | `CHECK_A20` | Returns AL=1 if A20 enabled |
| 0x0A | `GET_CONV_MEM` | Returns AX=KB conventional memory |
| 0x0B | `GET_EXT_MEM` | Returns AX=KB extended memory |
| 0x0C | `GET_E820` | BIOS memory map enumeration |
| 0x0D | `REBOOT` | Warm reboot (does not return) |
| 0x0E | `GET_DRIVE_INFO` | Returns drive geometry |
| 0x0F | `GET_BIB` | Returns ES:BX = Boot Info Block address |
| 0x10 | `PRINT_HEX8` | Print AL as 2-digit hex |
| 0x11 | `PRINT_HEX16` | Print DX as 4-digit hex |
| 0x12 | `PRINT_DEC16` | Print DX as decimal |
| 0x13 | `WAIT_KEY` | Print prompt, wait, clear screen |
| 0x14 | `GET_EQUIP` | Returns AX = BIOS equipment word |
| 0x15 | `GET_VIDEO` | Returns AL=mode, AH=cols, BH=page |
| 0x16 | `GET_BDA_BYTE` | Read byte from BDA (BX=offset) |
| 0x17 | `GET_BDA_WORD` | Read word from BDA (BX=offset) |
| 0x18 | `CPUID` | Execute CPUID (EDI=leaf) |
| 0x19 | `CHECK_CPUID` | Returns AL=1 if CPUID supported |
| 0x1A | `GET_EDD` | EDD drive info (DL=drive) |
| 0x1B | `GET_IVT` | Read IVT entry (CL=vector#) |
| 0x1C–0x1F | *(reserved)* | — |
| 0x20 | `DBG_PRINT` | Print tagged debug message (DS:SI=msg, DS:BX=tag) |
| 0x21 | `DBG_HEX16` | Print tagged hex value (DX=value, DS:BX=tag) |
| 0x22 | `DBG_REGS` | Dump all registers with tag (DS:BX=tag) |

**INT 0x81 filesystem name reference (AH → name):**

| AH | Name | Description |
|----|------|-------------|
| 0x01 | `LIST_FILES` | Copy cached directory to ES:BX buffer |
| 0x02 | `FIND_FILE` | Search for file by 8.3 name (DS:SI) |
| 0x03 | `READ_FILE` | Read file contents into ES:BX buffer |
| 0x04 | `GET_INFO` | Return filesystem metadata |

### 4.3.1 The AH Overlap Bug — How Tracing Found It

Consider the AH overlap bug that caused FS init to fail.  The FS module calls:

```nasm
    mov eax, [partition_lba]            ; EAX = 0x00000802 (LBA 2050)
    mov ah, SYS_READ_SECTOR             ; AH = 0x04 → EAX becomes 0x00000402!
    int 0x80
```

**Without tracing:** FS init fails.  `[FAIL] FS.SYS`.  Why?  No idea.

**With tracing (serial output):**

```
[SYS] READ_SECTOR AX=0402 BX=0800
                      ^^^^
                      Expected 0802, got 0402.  AH=04 clobbered bits 8-15.
                      Bug found in seconds.
```

### 4.4 INT 0x81 (Filesystem) Tracing

The same technique applies to the FS.SYS dispatcher, with its own 4-entry
name table:

```nasm
; In fs.asm, INT 0x81 handler:
fs_handler:
%ifdef DEBUG
    ; Look up FS function name and print:
    ;   [FS] LIST_FILES
    ;   [FS] FIND_FILE
    ; Falls back to [FS] AH=xx for unknown function numbers.
%endif
    ; ... existing dispatch ...
```

### 4.5 Selective Tracing with Verbosity Levels

Full syscall tracing can be noisy (every keypress, every character printed).
A verbosity level lets you control the volume:

```nasm
; In debug.inc:
%ifdef DEBUG
    DBG_LEVEL equ 2                     ; 0=off, 1=errors only,
                                        ; 2=syscalls, 3=everything
%endif
```

Level 1 only logs when CF is set on return (errors).
Level 2 logs all syscall entries.
Level 3 adds entry AND exit (with return values).

### 4.6 How This Would Have Caught Each Bug

| Bug | What the trace would show | Time to find |
|-----|---------------------------|--------------|
| AH/LBA overlap | `[SYS] READ_SECTOR AX=0402` — expected `AX=0802` | Seconds |
| CF propagation | No `[SYS] ERROR: CF set` after failed INT 13h | Minutes |
| Print value clobber | `[SYS] PRINT_DEC16 AX=1202` — expected `AX=0002` | Seconds |

### 4.7 User-Mode Debug Syscalls (v0.7.1)

#### 4.7.1 The Problem

Kernel and FS tracing (§4.2–4.4) instrument the kernel's *side* of every
syscall, but they reveal nothing about the user-mode program's internal
decisions.  If the shell takes the wrong branch in command dispatch, or
passes the wrong value to a syscall, the kernel trace shows *what* was
called but not *why*.

Modern operating systems solve this by routing all debug output through
the kernel:

| OS | User-mode API | Kernel role |
|----|---------------|-------------|
| **Windows** | `OutputDebugString()` | Catches exception → debugger via shared memory |
| **Linux** | `write(2, msg, len)` (stderr) | Kernel-managed file descriptor |
| **macOS** | `os_log()` | Unified logging subsystem |

The common pattern: **user mode never talks to hardware directly** — it
asks the kernel to emit debug output.  This gives:

- **Serialization** — no garbled output when multiple modules log
- **Privilege separation** — only the kernel touches COM1
- **Unified format** — kernel adds tags, timestamps, filtering

#### 4.7.2 The Solution: Debug Syscalls (INT 0x80, AH=0x20–0x22)

Three new syscalls provide kernel-mediated debug output for user-mode code.
All three accept an optional **caller tag** via DS:BX — a short NUL-terminated
string (e.g., `"SHL"`, `"FS"`) that identifies the source module.  If BX=0,
the kernel defaults to `"USR"`.

| AH | Name | Input | Serial Output |
|----|------|-------|---------------|
| 0x20 | `SYS_DBG_PRINT` | DS:SI = message string, DS:BX = tag | `[TAG] message` |
| 0x21 | `SYS_DBG_HEX16` | DX = 16-bit value, DS:BX = tag | `[TAG] NNNN` |
| 0x22 | `SYS_DBG_REGS`  | DS:BX = tag (all regs are dumped) | `[TAG] AX=xxxx BX=xxxx CX=xxxx DX=xxxx SI=xxxx DI=xxxx` |

**Release builds:** All three handlers are no-ops (immediate `iret`).
The syscall numbers are still valid — they just do nothing.

**Example usage (shell.asm):**
```nasm
; At shell init:
dbg_tag     db 'SHL', 0
dbg_init    db 'shell starting', 0

    mov bx, dbg_tag         ; DS:BX = "SHL"
    mov si, dbg_init        ; DS:SI = "shell starting"
    mov ah, SYS_DBG_PRINT
    int 0x80                ; → [SHL] shell starting

; After readline:
    mov bx, dbg_tag
    mov si, cmd_buf         ; DS:SI = whatever the user typed
    mov ah, SYS_DBG_PRINT
    int 0x80                ; → [SHL] help
```

#### 4.7.3 Kernel-Side Implementation

The handlers live in kernel.asm as regular syscall entries.  The tag
emission logic is shared via a `.dbg_emit_tag` subroutine:

```nasm
.dbg_emit_tag:           ; Print "[TAG] " to serial
    mov al, '['          ;
    call serial_putc     ; Uses BX=0 check to default to "USR"
    test bx, bx          ;
    jz .dbg_default_tag  ;
    mov si, bx           ; User-supplied tag
    call serial_puts     ;
    ...                  ; Close with '] '
    ret                  ;
```

The `SYS_DBG_REGS` handler saves CX, DX, SI, DI to kernel-local storage
before clobbering them for serial output, then prints each value.  BX is
recovered from `.sc_temp` (where the dispatcher saved it).

#### 4.7.4 Architecture: Why Syscalls, Not Direct Port I/O

An alternative design would include `serial.inc` in shell.asm and call
`serial_puts` directly.  This was rejected for three reasons:

1. **Privilege violation** — user-mode programs should not do port I/O.
   Even in real mode (where there's no hardware protection), maintaining
   the discipline prepares the codebase for protected mode.

2. **Code duplication** — serial.inc emits ~160 bytes of functions.
   Including it in every binary wastes space and creates multiple copies.

3. **Serialization** — if both the kernel and shell write to COM1
   simultaneously (e.g., a syscall trace fires while the shell is mid-
   message), the output interleaves.  Routing through the kernel ensures
   each message is atomic.

#### 4.7.5 Expected Debug Output (v0.7.1 Boot Sequence)

```
[DBG] KERNEL: serial debug active       ← serial_init completed
[DBG] KERNEL: INT 0x80 installed        ← syscall handler ready
[SYS] READ_SECTOR AX=0400 BX=0983      ← kernel loading FS.SYS
[DBG] KERNEL: FS.SYS loaded at 0x0800
[DBG] KERNEL: INT 0x81 filesystem ready
[DBG] KERNEL: SHELL.SYS loaded
[SYS] CLEAR_SCREEN AX=060C BX=0000     ← shell clearing screen
[SYS] DBG_PRINT AX=2000 BX=xxxx        ← shell calling SYS_DBG_PRINT
[SHL] shell starting                    ← user-mode debug message!
[SYS] PRINT_STRING AX=010C BX=xxxx     ← shell printing banner
[SYS] PRINT_STRING AX=010C BX=xxxx     ← shell printing prompt
[SYS] READ_KEY AX=030C BX=xxxx         ← waiting for input
... user types "help" and presses Enter ...
[SYS] DBG_PRINT AX=2000 BX=xxxx        ← shell logging command
[SHL] help                              ← the command that was typed
```

The `[SHL]` lines are new in v0.7.1 — they show the shell's internal
state alongside the kernel's syscall trace.

### 4.8 INT Nesting Depth & DAP Diagnostics *(implemented in v0.9.6)*

#### 4.8.1 The Problem

In v0.9.6 development, a program loader bug produced `INT 0x13 AH=09`
(DMA boundary error) when loading user programs.  The existing traces showed
which syscall failed, but could not reveal:

1. **How deeply nested** the interrupt was — FS calls INT 0x81, which
   internally calls INT 0x80 (READ_SECTOR), which calls INT 0x13.  A triple-
   nested interrupt chain was invisible in the flat trace.
2. **What the DAP (Disk Address Packet) contained** — the BIOS error code
   said "DMA boundary crossed" but without seeing the actual sector count
   and buffer address, it was impossible to tell what was wrong.
3. **Root cause** — turned out to be a register clobber (`mov edi, ...`
   destroyed DI, corrupting a subsequent `mov cx, [es:di + ...]`) that wrote
   248 sectors into the DAP instead of 1.

#### 4.8.2 Solution: Shared INT Depth Counter

A single byte counter at `BIB_INT_DEPTH` (absolute address 0x0607, in the
Boot Info Block) tracks total interrupt nesting depth across both INT 0x80
and INT 0x81 handlers:

```nasm
; Entry (both kernel and FS handlers):
    inc byte [cs:BIB_INT_DEPTH]

; Exit (via syscall_iret macro or fs_iret_cf_*):
    dec byte [cs:BIB_INT_DEPTH]
```

The depth value is appended to every trace line as `D=xx`:

```
[SYS] READ_SECTOR AX=0400 BX=9000 CF=0 IF=1 D=01
[FS]  READ_FILE                                D=02
[SYS] READ_SECTOR AX=0443 BX=9000 CF=0 IF=1 D=02
```

This immediately shows that the second READ_SECTOR is nested inside FS's
READ_FILE handler (depth 2), not a direct shell call (depth 1).

#### 4.8.3 Solution: DAP Hex Dump

Before every INT 0x13 call (both in kernel's READ_SECTOR handler and FS's
direct path), the full 16-byte DAP is dumped to serial:

```
[SYS] DAP: 10 00 01 00 00 90 00 00 45 08 00 00 00 00 00 00
            ^^    ^^^^  ^^^^^^^^^  ^^^^^^^^^^^^^^^^^^^^^^^
            size  cnt   buf addr   LBA (64-bit)
```

When the EDI-clobbers-DI bug was active, the dump showed:

```
[FS] DAP: 10 00 F8 00 00 90 00 00 45 08 00 00 00 00 00 00
                 ^^^^
                 Sector count = 0xF8 (248) — should be 0x01!
                 248 × 512 = 124 KB, crosses 64 KB DMA boundary → AH=09
```

The bug was identified in seconds from this single line.

#### 4.8.4 `syscall_iret` Macro

All 25 `iret` instructions in `kernel_syscall.inc` were replaced with
`syscall_iret`, which decrements the depth counter before returning:

```nasm
%macro syscall_iret 0
    dec byte [cs:BIB_INT_DEPTH]
    iret
%endmacro
```

Similarly, `syscall_ret_cf` (used for CF-returning syscalls via `retf 2`)
decrements depth before the far return.

#### 4.8.5 FS Error Traces

The FS module now emits additional diagnostics:

| Trace | Meaning |
|-------|---------|
| `[FS] DAP: xx xx ...` | 16-byte DAP dump before INT 0x13 |
| `[FS] INT13 ERR AH=xx` | BIOS returned error, AH = status code |
| `[FS] RF: not_found` | File not found during FS_READ_FILE |

---

## 5. Assert Macros *(implemented in v0.7.2)*

### 5.1 Concept

An assertion is a compile-time or runtime check that says "this condition
MUST be true here — if it's not, something is fundamentally wrong and we
should stop immediately rather than continue with corrupted state."

In C:
```c
assert(magic == 0x4D4E4653);   // "MNFS"
```

In mini-os assembly, we build the same concept as macros.

### 5.2 ASSERT — General Condition Check

```nasm
; ASSERT — Halt with message if condition is false
;
; Usage:
;   ASSERT <reg>, <op>, <value>, "message"
;
; Example:
;   ASSERT ax, e, 0x4D4E, "MNFS magic mismatch (first word)"
;   → If AX != 0x4D4E, print message + dump registers + halt.
;
; The <op> maps to a conditional jump:
;   e  → je  (equal)         ne → jne (not equal)
;   b  → jb  (below/less)    a  → ja  (above/greater)
;   z  → jz  (zero)          nz → jnz (not zero)

%ifdef DEBUG
%macro ASSERT 4
    cmp %1, %3
    j%2 %%ok                            ; Jump OVER the failure path if true
    ; Assertion failed — log and halt
    jmp %%failmsg_after
    %%failmsg: db '[ASSERT FAIL] ', %4, 13, 10, 0
%%failmsg_after:
    push si
    mov si, %%failmsg
    call serial_puts                    ; Log to serial
    call puts                           ; Also print to screen
    pop si
    DBG_REGS                            ; Dump all registers to serial
    cli
    hlt
%%ok:
%endmacro
%else
%macro ASSERT 4
%endmacro
%endif
```

### 5.3 ASSERT_CF — Check Carry Flag

Many operations in mini-os signal errors via CF (carry flag).  After a disk
read or BIOS call, you want to assert that CF is clear:

```nasm
%ifdef DEBUG
%macro ASSERT_CF_CLEAR 1
    jnc %%ok
    jmp %%failmsg_after
    %%failmsg: db '[ASSERT FAIL] CF set: ', %1, 13, 10, 0
%%failmsg_after:
    push si
    mov si, %%failmsg
    call serial_puts
    call puts
    pop si
    DBG_REGS
    cli
    hlt
%%ok:
%endmacro
%else
%macro ASSERT_CF_CLEAR 1
%endmacro
%endif
```

**Usage:**

```nasm
    ; Read MNFS directory sector
    mov edi, [part_lba]
    add edi, MNFS_DIR_SECTOR
    mov ah, SYS_READ_SECTOR
    mov cl, 1
    int 0x80
    ASSERT_CF_CLEAR "Failed to read MNFS directory sector"

    ; Verify magic
    cmp dword [es:bx], 'MNFS'
    ASSERT ax, e, ax, "MNFS magic not found in directory header"
    ; (above is a trivial always-true to demonstrate; the real check is:)
    ; We need a different form for memory comparisons — see ASSERT_MEM below
```

### 5.4 ASSERT_MAGIC — Verify a 4-byte Magic Value

Magic number validation is so common in mini-os (MNOS, MNLD, MNKN, MNEX,
MNFS) that it deserves its own macro:

```nasm
%ifdef DEBUG
%macro ASSERT_MAGIC 3
    ; %1 = segment:offset of magic location (e.g., es:bx)
    ; %2 = expected 4-byte magic (e.g., 'MNFS')
    ; %3 = message string
    push eax
    mov eax, [%1]
    cmp eax, %2
    je %%ok
    ; Failed — log expected vs actual
    jmp %%failmsg_after
    %%failmsg: db '[ASSERT FAIL] Magic mismatch: ', %3, 13, 10, 0
%%failmsg_after:
    push si
    mov si, %%failmsg
    call serial_puts
    call puts
    pop si
    ; Print expected and actual
    push si
    jmp %%exp_after
    %%exp_lbl: db '  Expected: ', 0
%%exp_after:
    mov si, %%exp_lbl
    call serial_puts
    mov eax, %2
    call serial_hex16                   ; High word
    shr eax, 16
    call serial_hex16                   ; Low word
    call serial_crlf
    jmp %%act_after
    %%act_lbl: db '  Actual:   ', 0
%%act_after:
    mov si, %%act_lbl
    call serial_puts
    mov eax, [%1]
    call serial_hex16
    shr eax, 16
    call serial_hex16
    call serial_crlf
    pop si
    DBG_REGS
    pop eax
    cli
    hlt
%%ok:
    pop eax
%endmacro
%else
%macro ASSERT_MAGIC 3
%endmacro
%endif
```

**Usage (in kernel after loading FS.SYS):**

```nasm
    ; Load FS.SYS to 0x0800
    call load_mnex
    ASSERT_MAGIC es:bx, 'MNFS', "FS.SYS header"
```

### 5.5 Real-World Example: How Asserts Would Have Caught v0.6.0 Bugs

**Bug 1 — FS init reading wrong sector:**

```nasm
; In fs_init:
    mov edi, [part_lba]
    add edi, MNFS_DIR_SECTOR            ; EDI = correct LBA
    DBG_REG "EDI", edi                  ; Serial: "EDI=00000802"

    mov ah, SYS_READ_SECTOR             ; ← THIS CLOBBERS EDI... wait, no.
                                        ;    EDI is safe.  But EAX isn't.
    ; With the old ABI (EAX = LBA):
    ;   DBG_REG "EAX", eax              ; Would show "EAX=00000402" ← CAUGHT!
    int 0x80
    ASSERT_CF_CLEAR "FS: directory read failed"
    ASSERT_MAGIC es:bx, 'MNFS', "FS: directory sector"  ; ← Would fire!
```

**Bug 2 — CF not propagated:**

```nasm
; In the kernel's .fn_read_sector:
    int 0x13                            ; BIOS disk read
    ASSERT_CF_CLEAR "Disk read INT 13h failed"
    ; Even if we don't assert, the serial trace would show:
    ;   [SYS] AH=04 AX=0402 → returned with CF=1
    ; And the assert would catch it before the error propagates.
```

---

## 6. Fault Handlers *(implemented in v0.7.3, extended to release in v0.7.4)*

### 6.1 The Problem

When the CPU encounters an error condition (divide by zero, invalid opcode,
general protection fault), it triggers an exception through the IVT.  In
mini-os, these IVT entries still point to BIOS default handlers which
typically just `IRET` back to the faulting instruction — causing an infinite
silent loop with no user-visible indication of the error.

### 6.2 Exception Vectors

The first 32 IVT entries (INT 0x00 through INT 0x1F) are reserved for CPU
exceptions.  The most relevant ones for mini-os:

| Vector | Name | Common Cause |
|--------|------|--------------|
| 0x00 | Divide Error (#DE) | `div` by zero, quotient overflow |
| 0x01 | Debug/Single Step (#DB) | TF flag set (debuggers use this) |
| 0x04 | Overflow (#OF) | `INTO` when OF=1 |
| 0x05 | Bound Range Exceeded (#BR) | `BOUND` instruction fails |
| 0x06 | Invalid Opcode (#UD) | CPU encounters undefined instruction |
| 0x07 | Device Not Available (#NM) | FPU instruction without FPU |
| 0x08 | Double Fault (#DF) | Exception during exception handling |

> **PIC Remap:** In real mode, the IBM PC BIOS maps IRQ 0–7 to INT 0x08–0x0F,
> which conflicts with CPU exceptions.  mini-os remaps the master PIC (8259A)
> so IRQ 0–7 fire INT 0x20–0x27 instead, freeing INT 0x08 for #DF.

### 6.3 Design: Both Builds Get Fault Handlers

Unlike assert macros (debug-only, zero cost in release), fault handlers are
installed in **both** release and debug builds.  Rationale:

- A silent hang is never acceptable — users deserve a crash screen
- The handler code adds only ~400 bytes to the release kernel (6→7 sectors)
- This mimics Linux's approach: a kernel panic always prints diagnostics

**Release output** (screen only via BIOS INT 0x10):
```
*** FAULT: #DE Divide Error at 1000:0142
AX=0000 BX=0000 CX=0005 DX=0000
SI=3500 DI=0800 BP=FFF0 SP=FFE4
DS=1000 ES=0800 SS=1000 FL=0246
Stack: 3142 1000 0246 0000
System halted.
```

**Debug output** (adds serial logging before the screen dump):
- Same screen output as release, PLUS
- Exception name, CS:IP, and DBG_REGS macro output to COM1 serial

### 6.4 PIC Remap (IRQ 0–7 → INT 0x20–0x27)

In real mode, the IBM PC BIOS maps hardware IRQ 0–7 to INT 0x08–0x0F.  This
conflicts with CPU exception vectors #DF (INT 0x08) through #PF (INT 0x0E).
To safely trap #DF, mini-os remaps the master 8259A PIC during kernel init:

```nasm
; Step 1: Copy BIOS ISRs from IVT[0x08-0x0F] to IVT[0x20-0x27]
;         (so hardware IRQs still reach their original handlers)
mov cx, 8
mov si, 0x08 * 4               ; Source: old IRQ vectors
mov di, 0x20 * 4               ; Dest: new IRQ vectors
.copy_loop:
    movsw                      ; Copy offset
    movsw                      ; Copy segment
    loop .copy_loop

; Step 2: Reprogram master PIC
mov al, 0x11                   ; ICW1: init + ICW4 needed
out 0x20, al
mov al, 0x20                   ; ICW2: new base vector = 0x20
out 0x21, al
mov al, 0x04                   ; ICW3: slave on IRQ2
out 0x21, al
mov al, 0x01                   ; ICW4: 8086 mode
out 0x21, al
```

After remapping:
- Timer (IRQ0) fires INT 0x20 → BIOS timer handler (copied from old INT 0x08)
- Keyboard (IRQ1) fires INT 0x21 → BIOS keyboard handler
- INT 0x08 is now free for the #DF exception handler

### 6.5 Installing Exception Vectors

Called unconditionally during kernel init (after INT 0x80 syscall setup):

```nasm
; In kernel_start (always, not conditional):
    call install_fault_handlers

install_fault_handlers:
    cli
    ; ... PIC remap (see §6.4) ...

    ; Install exception handlers in now-free IVT slots
    mov word [es:0x00*4],   fault_de    ; #DE
    mov word [es:0x00*4+2], cs
    ; ... (same pattern for 0x01, 0x04-0x08)

    pop ds
    pop es
    sti
    ret
```

### 6.6 Fault Handler Implementation

Each stub pushes its name string, then jumps to `fault_common`:

```nasm
fault_de:
    push word .de_name
    jmp fault_common
.de_name: db '#DE Divide Error', 0
```

The `fault_common` handler:
1. Saves all 7 GP registers (AX, BX, CX, DX, SI, DI, BP)
2. (Debug only) Logs exception info to serial via `serial_puts`/`serial_hex16`
3. Prints exception banner + name to screen via `puts`
4. Prints faulting CS:IP via inline hex formatter
5. Prints all registers + FLAGS from the saved stack frame
6. Prints top 4 words from the original (pre-fault) stack
7. Prints "System halted." and enters `cli; hlt` loop

Stack frame layout after register saves:
```
SP+0  = BP   SP+2  = DI   SP+4  = SI   SP+6  = DX
SP+8  = CX   SP+10 = BX   SP+12 = AX   SP+14 = name ptr
SP+16 = IP   SP+18 = CS   SP+20 = FLAGS
SP+22 = original stack top (pre-fault)
```

### 6.7 Example Output (Release Build)

If the shell accidentally executes `div bx` when BX=0:

```
*** FAULT: #DE Divide Error at 3000:0142
AX=0000 BX=0000 CX=0005 DX=0000
SI=3500 DI=0800 BP=FFF0 SP=FFE4
DS=3000 ES=0800 SS=3000 FL=0246
Stack: 3142 3000 0246 0000
System halted.
```

This immediately tells you: division by zero at shell address 0x0142,
with full register and stack context for debugging.  Without the fault
handler, the system would silently loop on the faulting instruction forever.

---

## 7. Machine Monitor (`mnmon`) *(implemented as MNMON.MNX — see [doc/MNMON.md](MNMON.md))*

### 7.1 Heritage: The Woz Monitor

In 1976, Steve Wozniak wrote the **Woz Monitor** (Wozmon) for the Apple I.
In just 256 bytes of 6502 assembly, it provided everything a developer needed
to interact with bare hardware: examine memory, write bytes, and run code.
No assembler, no OS, no file system — just a prompt and hex.

The Apple I shipped with Wozmon in ROM.  When you powered on, you saw:

```
\
```

That backslash was the entire user interface.  From there, you could:

```
FF00            ← Examine: show byte at address FF00
FF00.FF0F       ← Range:   show bytes from FF00 through FF0F
0300: A9 01     ← Deposit: write A9 then 01 starting at 0300
0300R           ← Run:     jump to address 0300 and execute
```

That's it.  Four operations.  Enough to bootstrap an entire computer.

### 7.2 Why a Monitor Instead of `dump`

A simple `dump` command is read-only — you can look but not touch.  A
monitor gives you superpowers:

| Capability | `dump` command | Wozmon-style monitor |
|------------|---------------|---------------------|
| Read memory | ✓ | ✓ |
| Read a range | ✓ | ✓ |
| Write memory | ✗ | ✓ (deposit bytes) |
| Execute code | ✗ | ✓ (run at address) |
| Patch live bugs | ✗ | ✓ (write new opcodes) |
| Test hardware | ✗ | ✓ (write to I/O-mapped memory) |
| Enter programs | ✗ | ✓ (type in machine code) |

With a monitor, you can:
- **Inspect the BIB** to verify boot_drive and partition LBA
- **Read the IVT** to confirm INT 0x80/0x81 vectors are installed correctly
- **Examine the MNFS directory** to verify file entries
- **Patch a byte** in a kernel handler to test a fix without rebuilding
- **Write a small test program** directly into unused memory and run it
- **Verify stack contents** to debug calling convention issues

### 7.3 mini-os Monitor Design: `mnmon`

Our monitor — `mnmon` (Mini-OS Monitor) — adapts Wozmon's syntax for x86-16.
It's entered via the `mnmon` shell command and has its own prompt:

```
mnos:\> mnmon

mnmon v1.0 — type ? for help, q to quit

*
```

The `*` prompt (matching Wozmon's `\` — we use `*` because it's more visible)
indicates the monitor is ready for input.

### 7.4 Command Syntax

The monitor accepts four operations, all hex-based:

#### 7.4.1 Examine — Show a Single Address

```
*0600
0600: 80
```

Type a hex address, press Enter.  The monitor displays the byte at that
address.  The **current address** advances to 0x0601, so pressing Enter
again shows the next byte:

```
*
0601: 01
*
0602: 00
```

This "sticky address" behavior lets you walk through memory by just pressing
Enter repeatedly — exactly like Wozmon.

#### 7.4.2 Range — Show a Block of Memory

```
*0600.060F
0600: 80 01 00 08 00 00 00 00 00 00 00 00 00 00 00 00
```

A period separates start and end addresses.  The monitor displays all bytes
in the range, 16 per line.  For larger ranges, the output continues with
address prefixes:

```
*0800.083F
0800: 4D 4E 46 53 01 04 17 00 FE 77 00 00 00 00 00 00
0810: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0820: 4C 4F 41 44 45 52 20 20 42 49 4E 01 03 00 00 00
0830: 02 00 00 04 00 00 00 00 00 00 00 00 00 00 00 00
```

That's the MNFS directory header + first file entry, visible byte-by-byte.
You can read 'LOADER  SYS' at 0x0820 in the ASCII values (4C=L, 4F=O, ...).

#### 7.4.3 Deposit — Write Bytes to Memory

```
*0700: 48 65 6C 6C 6F 00
```

A colon after the address switches to deposit mode.  All subsequent hex
bytes are written starting at the given address.  In this example:

- 0x0700 ← 0x48 ('H')
- 0x0701 ← 0x65 ('e')
- 0x0702 ← 0x6C ('l')
- 0x0703 ← 0x6C ('l')
- 0x0704 ← 0x6F ('o')
- 0x0705 ← 0x00 (NUL terminator)

You can deposit on multiple lines.  After a deposit, the current address
is updated, so a bare colon continues writing:

```
*0700: 48 65 6C
*: 6C 6F 00
```

This writes the same 6 bytes — the second line continues from 0x0703.

#### 7.4.4 Run — Execute at Address

```
*0700R
```

The `R` suffix jumps to the specified address using a `call`, so if the
code at that address ends with `ret`, control returns to the monitor.

**Safety note**: Running arbitrary addresses can crash the system.  That's
expected — the monitor is a power tool, not a safe sandbox.  If the code
hangs or crashes, reboot and try again.  This is exactly how Wozniak
intended it.

#### 7.4.5 Help and Quit

```
*?           ← Show command summary
*q           ← Return to shell prompt
```

### 7.5 Full Command Reference

```
┌─────────────────────────────────────────────────────────────────┐
│                    mnmon Command Reference                      │
├─────────────┬───────────────────────────────────────────────────┤
│ Command     │ Description                                      │
├─────────────┼───────────────────────────────────────────────────┤
│ XXXX        │ Examine byte at address XXXX                     │
│ (Enter)     │ Examine next byte (auto-increment)               │
│ XXXX.YYYY   │ Show bytes from XXXX through YYYY                │
│ XXXX: BB .. │ Write bytes BB ... starting at XXXX              │
│ : BB ..     │ Continue writing from current address            │
│ XXXXR       │ Call address XXXX (ret returns to monitor)       │
│ ?           │ Show help                                        │
│ q           │ Quit monitor, return to shell                    │
└─────────────┴───────────────────────────────────────────────────┘

  XXXX = 1-4 hex digits (case-insensitive)
  BB   = 1-2 hex digits per byte
```

### 7.6 Implementation

The monitor is a self-contained routine within `shell.asm`.  It uses
only INT 0x80 syscalls (no direct BIOS calls) — true user-mode code.

#### 7.6.1 Data Structures

```nasm
; Monitor state
mon_addr:   dw 0                        ; Current address (sticky)
mon_buf:    times 80 db 0               ; Input line buffer
mon_len:    db 0                        ; Current input length
```

#### 7.6.2 Main Loop

```nasm
; ─── cmd_mon — Enter the machine monitor ─────────────────────────
cmd_mon:
    ; Print banner
    mov ah, SYS_PRINT_STRING
    mov si, .mon_banner
    int 0x80

.mon_loop:
    ; Print prompt
    mov ah, SYS_PRINT_CHAR
    mov al, '*'
    int 0x80

    ; Read a line of input into mon_buf
    call mon_readline                   ; Returns: mon_buf filled, CX = length

    ; Empty line → examine next byte (auto-increment)
    test cx, cx
    jz .mon_next_byte

    ; Parse the line
    mov si, mon_buf

    ; Check for '?' (help)
    cmp byte [si], '?'
    je .mon_help

    ; Check for 'q' (quit)
    cmp byte [si], 'q'
    je .mon_quit

    ; Check for ':' at start (continue deposit)
    cmp byte [si], ':'
    je .mon_cont_deposit

    ; Must start with a hex digit — parse address
    call parse_hex16                    ; AX = address, SI advanced
    mov [mon_addr], ax                  ; Update current address

    ; What follows the address?
    cmp byte [si], 0                    ; End of line → examine
    je .mon_examine

    cmp byte [si], '.'                  ; Period → range examine
    je .mon_range

    cmp byte [si], ':'                  ; Colon → deposit
    je .mon_deposit

    ; Check for 'R' or 'r' (run)
    mov al, [si]
    or al, 0x20                         ; To lowercase
    cmp al, 'r'
    je .mon_run

    ; Unknown syntax — show error
    mov ah, SYS_PRINT_STRING
    mov si, .mon_err
    int 0x80
    jmp .mon_loop

; ─── Examine single byte ────────────────────────────────────────
.mon_examine:
    call mon_show_addr                  ; Print "XXXX: "
    mov di, [mon_addr]
    mov al, [di]
    mov ah, SYS_PRINT_HEX8
    int 0x80
    call mon_newline
    inc word [mon_addr]                 ; Auto-increment
    jmp .mon_loop

; ─── Examine next byte (Enter on empty line) ────────────────────
.mon_next_byte:
    jmp .mon_examine

; ─── Range examine ──────────────────────────────────────────────
.mon_range:
    inc si                              ; Skip '.'
    call parse_hex16                    ; AX = end address
    mov bx, ax                          ; BX = end address
    mov di, [mon_addr]                  ; DI = start address

.mon_range_line:
    ; Print address prefix
    mov dx, di
    mov ah, SYS_PRINT_HEX16
    int 0x80
    mov ah, SYS_PRINT_CHAR
    mov al, ':'
    int 0x80

    ; Print up to 16 bytes per line
    mov cx, 16
.mon_range_byte:
    cmp di, bx
    ja .mon_range_done                  ; Past end address

    mov ah, SYS_PRINT_CHAR
    mov al, ' '
    int 0x80

    mov al, [di]
    mov ah, SYS_PRINT_HEX8
    int 0x80

    inc di
    dec cx
    jnz .mon_range_byte

    call mon_newline
    jmp .mon_range_line

.mon_range_done:
    call mon_newline
    mov [mon_addr], di                  ; Update current address
    jmp .mon_loop

; ─── Deposit bytes ──────────────────────────────────────────────
.mon_deposit:
    inc si                              ; Skip ':'
    mov di, [mon_addr]

.mon_dep_loop:
    ; Skip spaces
    call mon_skip_spaces
    cmp byte [si], 0                    ; End of line?
    je .mon_dep_done

    ; Parse hex byte (1-2 hex digits)
    call parse_hex8                     ; AL = byte value
    mov [di], al                        ; Write to memory
    inc di
    jmp .mon_dep_loop

.mon_dep_done:
    mov [mon_addr], di                  ; Update current address
    jmp .mon_loop

; ─── Continue deposit (line starts with ':') ────────────────────
.mon_cont_deposit:
    jmp .mon_deposit                    ; mon_addr already set from last deposit

; ─── Run at address ─────────────────────────────────────────────
.mon_run:
    mov ax, [mon_addr]
    ; We use an indirect call so 'ret' returns to us
    mov [.mon_run_addr], ax
    call far [.mon_run_addr]            ; Far call to address
                                        ; (if code does 'ret', we continue here)
    jmp .mon_loop

.mon_run_addr:
    dw 0                                ; Offset (filled at runtime)
    dw 0x0000                           ; Segment (always 0 for flat real mode)

; ─── Help ───────────────────────────────────────────────────────
.mon_help:
    mov ah, SYS_PRINT_STRING
    mov si, .mon_help_text
    int 0x80
    jmp .mon_loop

; ─── Quit ───────────────────────────────────────────────────────
.mon_quit:
    call mon_newline
    ret                                 ; Return to shell command loop

; ─── Helpers ────────────────────────────────────────────────────

mon_show_addr:
    mov dx, [mon_addr]
    mov ah, SYS_PRINT_HEX16
    int 0x80
    mov ah, SYS_PRINT_STRING
    mov si, .mon_colon_sp
    int 0x80
    ret

mon_newline:
    mov ah, SYS_PRINT_STRING
    mov si, .mon_crlf
    int 0x80
    ret

mon_skip_spaces:
.mss_loop:
    cmp byte [si], ' '
    jne .mss_done
    inc si
    jmp .mss_loop
.mss_done:
    ret

; ─── parse_hex8 — Parse 1-2 hex digits into AL ─────────────────
;   Input:  SI = pointer to hex chars
;   Output: AL = byte value, SI advanced past digits
parse_hex8:
    push bx
    xor ax, ax
    ; First digit (required)
    call .ph8_digit
    jc .ph8_done                        ; Not a hex digit — return 0
    mov al, bl
    ; Second digit (optional)
    call .ph8_digit
    jc .ph8_done                        ; Only one digit
    shl al, 4
    or al, bl
.ph8_done:
    pop bx
    ret

.ph8_digit:
    movzx bx, byte [si]
    cmp bl, '0'
    jb .ph8_not_hex
    cmp bl, '9'
    jbe .ph8_d09
    or bl, 0x20                         ; lowercase
    cmp bl, 'a'
    jb .ph8_not_hex
    cmp bl, 'f'
    ja .ph8_not_hex
    sub bl, 'a' - 10
    inc si
    clc
    ret
.ph8_d09:
    sub bl, '0'
    inc si
    clc
    ret
.ph8_not_hex:
    stc
    ret

; ─── parse_hex16 — Parse 1-4 hex digits into AX ────────────────
;   Input:  SI = pointer to hex chars
;   Output: AX = 16-bit value, SI advanced past digits
parse_hex16:
    xor ax, ax
    push bx
.ph16_loop:
    movzx bx, byte [si]
    cmp bl, '0'
    jb .ph16_done
    cmp bl, '9'
    jbe .ph16_d09
    or bl, 0x20                         ; lowercase
    cmp bl, 'a'
    jb .ph16_done
    cmp bl, 'f'
    ja .ph16_done
    sub bl, 'a' - 10
    jmp .ph16_add
.ph16_d09:
    sub bl, '0'
.ph16_add:
    shl ax, 4
    or al, bl
    inc si
    jmp .ph16_loop
.ph16_done:
    pop bx
    ret

; ─── String constants ───────────────────────────────────────────

.mon_banner:
    db 13, 10, 'mnmon v1.0', 13, 10
    db 'Type ? for help, q to quit', 13, 10, 13, 10, 0

.mon_help_text:
    db 'Commands:', 13, 10
    db '  XXXX        Examine byte at address', 13, 10
    db '  (Enter)     Show next byte', 13, 10
    db '  XXXX.YYYY   Show range of bytes', 13, 10
    db '  XXXX: BB .. Write bytes at address', 13, 10
    db '  : BB ..     Continue writing', 13, 10
    db '  XXXXR       Run code at address', 13, 10
    db '  q           Quit to shell', 13, 10, 0

.mon_err:       db '?', 13, 10, 0      ; Classic monitor error: just '?'
.mon_colon_sp:  db ': ', 0
.mon_crlf:      db 13, 10, 0
```

#### 7.6.3 Monitor Input Routine

The monitor needs its own readline that's simpler than the shell's — no
auto-lowercase (hex addresses are case-insensitive, but we need uppercase
for 'R'):

```nasm
; ─── mon_readline — Read a line into mon_buf ────────────────────
;   Output: mon_buf filled, CX = character count
;   Handles: printable chars, backspace, Enter
mon_readline:
    xor cx, cx                          ; CX = character count
    mov di, mon_buf

.mrl_key:
    mov ah, SYS_READ_KEY
    int 0x80                            ; AL = ASCII character

    ; Enter → done
    cmp al, 13
    je .mrl_done

    ; Backspace
    cmp al, 8
    je .mrl_bs

    ; Printable character (buffer full?)
    cmp cx, 78                          ; Max line length
    jae .mrl_key                        ; Ignore if full

    ; Store and echo
    mov [di], al
    inc di
    inc cx
    mov ah, SYS_PRINT_CHAR
    int 0x80
    jmp .mrl_key

.mrl_bs:
    test cx, cx
    jz .mrl_key                         ; Nothing to delete
    dec di
    dec cx
    ; Erase on screen: backspace + space + backspace
    mov ah, SYS_PRINT_CHAR
    mov al, 8
    int 0x80
    mov al, ' '
    int 0x80
    mov al, 8
    int 0x80
    jmp .mrl_key

.mrl_done:
    mov byte [di], 0                    ; NUL-terminate
    call mon_newline                    ; Echo newline
    ret
```

### 7.7 Complete Interactive Session Example

Here's a realistic debugging session using `mnmon` to diagnose the AH
overlap bug that we hit during v0.6.0 development:

```
mnos:\> mnmon

mnmon v1.0
Type ? for help, q to quit

*0600.060F
0600: 80 01 00 08 00 00 00 00 00 00 00 00 00 00 00 00
                                        ← BIB: drive=0x80, A20=yes,
                                           part_lBA=0x00000800 (LE)

*0200.0207
0200: 78 50 00 00 00 00 00 00
                                        ← IVT[0x80]: handler at 0000:5078
                                           (kernel syscall_handler)

*0204.0207
0204: 78 08 00 00
                                        ← IVT[0x81]: handler at 0000:0878
                                           (FS.SYS fs_handler)

*0800.081F
0800: 4D 4E 46 53 01 04 17 00 FE 77 00 00 00 00 00 00
0810: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
                                        ← MNFS directory header:
                                           Magic='MNFS', ver=1, 4 files,
                                           23 sectors used, 30718 capacity

*0820.085F
0820: 4C 4F 41 44 45 52 20 20 42 49 4E 01 03 00 00 00
0830: 02 00 00 04 00 00 00 00 00 00 00 00 00 00 00 00
0840: 46 53 20 20 20 20 20 20 42 49 4E 01 05 00 00 00
0850: 02 00 00 04 00 00 00 00 00 00 00 00 00 00 00 00
                                        ← Entry 0: "LOADER  SYS" sector 3
                                           Entry 1: "FS      SYS" sector 5

Now let's enter a tiny test program at an unused address and run it.
This program prints 'X' to the screen and returns:

*0700: B4 02 B0 58 CD 80 C3
                                        ← Deposited 7 bytes:
                                           B4 02     mov ah, 0x02 (PRINT_CHAR)
                                           B0 58     mov al, 'X'  (0x58)
                                           CD 80     int 0x80
                                           C3        ret

*0700.0706
0700: B4 02 B0 58 CD 80 C3
                                        ← Verify: bytes are correct

*0700R
X                                       ← It printed 'X' and returned!

*q

mnos:\>
```

### 7.8 Safety Considerations

The monitor gives you unrestricted memory access.  This is intentional —
it's a debugging tool, not a user application.  Some things to be aware of:

| Action | Risk | Result |
|--------|------|--------|
| Write to IVT (0x0000-0x03FF) | Redirects interrupts | System may hang |
| Write to kernel (0x5000+) | Corrupts syscall handlers | Undefined behavior |
| Write to VGA (0xB8000+) | Modifies display directly | Garbled screen |
| Run at random address | Executes unknown code | Crash, hang, or reboot |
| Write to BIB (0x0600) | Changes boot parameters | Future disk I/O may fail |

**Recovery**: If the monitor crashes the system, just reboot.  No persistent
damage can occur — all changes are in RAM only.  The disk image is read-only
after boot.

### 7.9 Educational Value

The Wozmon-style monitor is not just a debugging tool — it's a teaching
instrument.  It demonstrates:

1. **Memory-mapped I/O** — students can read the IVT, BDA, VGA memory
2. **Machine code** — deposit and run raw opcodes, see exactly what the
   CPU executes
3. **Data structures in memory** — browse the MNFS directory, BIB, stack
4. **How an OS organizes memory** — walk from 0x0000 to 0xFFFFF and see
   what's where
5. **Historical computing** — this is how all programs were entered in the
   1970s, before assemblers and editors existed

### 7.10 Size Budget

The monitor is compact — true to Wozmon's spirit:

| Component | Size |
|-----------|------|
| Main loop + dispatch | ~120 bytes |
| Examine + range | ~80 bytes |
| Deposit | ~60 bytes |
| Run | ~20 bytes |
| parse_hex16 + parse_hex8 | ~80 bytes |
| mon_readline | ~60 bytes |
| String constants | ~200 bytes |
| **Total** | **~620 bytes** |

Wozniak's original was 256 bytes on the 6502.  Our x86 version is larger
(x86 instructions are longer, we have more features, and we include help
text), but still fits comfortably within SHELL.SYS's growth room.

### 7.11 Always-On vs Debug-Only

**Recommended: Always available.**

The monitor is not conditional (`%ifdef DEBUG`).  Reasons:

1. It's educational — the whole point of mini-os is learning
2. It's useful in release builds — inspect memory without rebuilding
3. It's compact (~620 bytes) — well within the shell's 2 KB growth room
4. Wozmon was in ROM — always available on every Apple I, no opt-in needed

The only debug-only addition would be optional serial echo: in debug builds,
all monitor I/O is also logged to COM1 for capture.

---

## 8. Debug Build Mode *(implemented in v0.7.0)*

### 8.1 The `%ifdef DEBUG` Pattern

All debugging facilities are conditionally assembled.  The build system
controls whether `DEBUG` is defined:

```nasm
; In build.ps1, when assembling any binary:
if ($DebugBuild) {
    $nasmFlags += '-dDEBUG'             ; -d defines a preprocessor symbol
}
```

This passes `-dDEBUG` to NASM, which is equivalent to writing `%define DEBUG`
at the top of every source file.

### 8.2 Build System Integration

```powershell
# tools/build.ps1 — uses -DebugBuild switch
# (can't use -Debug — conflicts with CmdletBinding common parameter)
param(
    [switch]$DebugBuild
)

# Build each binary with optional DEBUG define
function Build-Binary {
    param([string]$Source, [string]$Output)

    $flags = @('-f', 'bin', '-I', 'src/include/', '-o', $Output, $Source)
    if ($DebugBuild) {
        $flags = @('-dDEBUG') + $flags
        Write-Host "[mini-os] DEBUG build: $Source"
    }
    & $NasmPath @flags
}
```

```batch
:: build.bat — builds both release and debug variants (v0.8.0+)
@echo off
pwsh -ExecutionPolicy Bypass -File tools\build.ps1
```

As of v0.8.0, `build.bat` always builds **both** release and debug variants
into a single unified VHD.  The boot menu lets the user select at startup.

| Build | Raw image | VHD |
|-------|-----------|-----|
| Unified | `build/boot/mini-os.img` | `build/boot/mini-os.vhd` |

The unified VHD contains 7 MNFS files: LOADER (shared), FS + KERNEL + SHELL
(release), and FSD + KERNELD + SHELLD (debug).

### 8.3 What Each Mode Includes

| Facility | Release (menu option 1) | Debug (menu option 2) |
|----------|----------------------|-------------------|
| Serial init + putc/puts | ✗ | ✓ |
| `DBG` macros | ✗ (expand to nothing) | ✓ |
| Syscall tracing | ✗ | ✓ |
| Assert macros | ✗ (expand to nothing) | ✓ |
| Fault handlers | ✗ | ✓ |
| Stack canary | ✗ | ✓ |
| `mnmon` command (monitor) | ✓ (always on) | ✓ |
| Boot messages `[OK]/[FAIL]` | ✓ (always on) | ✓ |
| Register dump on `[FAIL]` | ✓ (always on) | ✓ |

Boot messages are NOT conditional — they're lightweight and valuable in all
builds.  Only the heavy instrumentation is debug-only.

### 8.4 Binary Size Impact

Measured size increase with DEBUG enabled (v0.8.0):

| Binary | Release | Debug | Increase | Max region |
|--------|---------|-------|----------|------------|
| LOADER.SYS | 1.5 KB (3 sec) | *(shared)* | — | 8 KB |
| FS.SYS | 1 KB (2 sec) | 2 KB (4 sec) | +1 KB (serial funcs + FS tracing + asserts) | 8 KB |
| MM.SYS | 0.5 KB (1 sec) | 1 KB (2 sec) | +0.5 KB (serial + MM call tracing) | 2 KB |
| KERNEL.SYS | 4 KB (8 sec) | 6 KB (12 sec) | +2 KB (serial + tracing + debug syscalls) | 8 KB |
| SHELL.SYS | 7 KB (14 sec) | 7 KB (14 sec) | 0 B | 8 KB |

All binaries remain well within their 8 KB maximum allocation.  The sector
counts in each binary's header are conditional (`%ifdef DEBUG`), so the loader
and kernel read the correct size at runtime.

### 8.5 Memory Layout: Identical Across Build Types

**Important**: The runtime memory layout is the same for release and debug
builds.  Every component loads at its hardcoded address regardless of build
type:

```
Component    Load address    Release size    Debug size     Region end
─────────────────────────────────────────────────────────────────────
FS.SYS       0x0800          1 KB (2 sec)    2 KB (4 sec)   0x27FF (8 KB max)
SHELL.SYS    0x3000          7 KB (14 sec)   7 KB (14 sec)  0x4FFF (8 KB max)
KERNEL.SYS   0x5000          3.5 KB (7 sec)  5 KB (10 sec)  0x6FFF (8 KB max)
```

The addresses are compile-time constants in `src/include/memory.inc` and set
via `ORG` directives — they never change.  Debug builds simply use more space
**within** each pre-allocated region.  No regions overlap, and ample growth
room remains.

As of v0.8.0, **both release and debug variants are on the same disk**.  The
MNFS directory has 7 entries — the boot menu selects which set to load:

```
Unified disk layout (v0.8.1)
Sector 2048:    VBR (2 sec)
Sector 2050:    MNFS directory (7 files)
Sector 2051:    LOADER.SYS  (3 sec)    — shared
Sector 2054:    FS.SYS      (2 sec)    — release
Sector 2056:    KERNEL.SYS  (7 sec)    — release
Sector 2063:    SHELL.SYS   (14 sec)    release
Sector 2075:    FSD.SYS     (4 sec)    — debug
Sector 2079:    KERNELD.SYS (11 sec)   — debug
Sector 2090:    SHELLD.SYS  (14 sec)    debug
                53 total sectors (52 data + 1 directory)
```

This is handled automatically by the build pipeline — `create-disk.ps1` reads
each binary's size and packs them contiguously.  The loader and kernel look up
file locations from the MNFS directory at runtime, so the different disk
offsets are transparent.

---

## 9. Stack Canary *(implemented in v0.8.1)*

### 9.1 The Problem

The mini-os stack starts at 0x7C00 and grows downward.  The stack zone
extends to approximately 0x7000 (3 KB).  Below that is the kernel at
0x5000–0x5BFF.  If a bug causes excessive stack usage (deep recursion,
large local buffers), the stack silently overwrites kernel code or data.

### 9.2 The Canary

A **stack canary** is a known magic value written to the bottom of the stack
zone.  Periodically, we check if it's been overwritten:

```nasm
; Constants (in memory.inc):
STACK_CANARY_ADDR  equ 0x7000        ; Linear address of canary (stack floor)
STACK_CANARY_VALUE equ 0xDEAD        ; Sentinel value (written as two words)
STACK_CANARY_SIZE  equ 4             ; Total canary size in bytes

; Implementation (in kernel_stack.inc):
%ifdef DEBUG
canary_init:
    mov word [ss:STACK_CANARY_ADDR], STACK_CANARY_VALUE
    mov word [ss:STACK_CANARY_ADDR + 2], STACK_CANARY_VALUE
    DBG "KERNEL: stack canary planted at 0x7000 (0xDEAD 0xDEAD)"
    ret

canary_check:
    pushf                               ; Preserve FLAGS (caller may need CF)
    cmp word [ss:STACK_CANARY_ADDR], STACK_CANARY_VALUE
    jne .canary_dead
    cmp word [ss:STACK_CANARY_ADDR + 2], STACK_CANARY_VALUE
    jne .canary_dead
    popf                                ; Canary intact — restore FLAGS
    ret

.canary_dead:
    popf                                ; Discard saved FLAGS
    push si
    mov si, .canary_msg
    call serial_puts                    ; Log to serial (most reliable)
    pop si
    DBG_REGS                            ; Dump registers to serial
    push si
    mov si, .canary_msg
    call puts                           ; Also show on screen
    pop si
    cli
.canary_halt:
    hlt
    jmp .canary_halt

.canary_msg:
    db 13, 10, '*** STACK OVERFLOW: canary at 0x7000 destroyed! ***', 13, 10
    db '    Stack grew past safe limit (0x7004).', 13, 10, 0
%endif

; Call-site macros (expand to nothing in release):
%ifdef DEBUG
    %define CANARY_INIT  call canary_init
    %define CANARY_CHECK call canary_check
%else
    %define CANARY_INIT
    %define CANARY_CHECK
%endif
```

**Key design decisions:**

- **SS: segment override** — all canary reads/writes use `[ss:0x7000]` instead
  of `[0x7000]`.  In real mode, `[addr]` uses DS by default, but DS may be
  changed by some components.  SS is always 0x0000 (set by MBR, never moved).
- **FLAGS preservation** — `canary_check` uses `pushf`/`popf` so the caller's
  CF/ZF/etc. survive the check.  This is critical in the syscall handler where
  CF carries return status.
- **Register preservation** — on success, `canary_check` clobbers nothing.
  On failure, it doesn't matter (we halt).
- **Call-site macros** — `CANARY_INIT` and `CANARY_CHECK` eliminate the need for
  `%ifdef DEBUG` guards at every call site.  In release builds, they expand to
  exactly 0 bytes.

### 9.3 When to Check

The canary is checked at one strategic, low-overhead point:

1. **Every syscall entry** — `CANARY_CHECK` is the first thing in
   `syscall_handler`, before the dispatch table lookup.  This catches:
   - Stack overflow that occurred during the **previous** syscall handler
   - Stack overflow that occurred in **user-mode code** (shell) between syscalls
   - Stack overflow during **BIOS calls** invoked by handlers

   Because every user-mode operation eventually calls INT 0x80, the check
   frequency is proportional to OS activity — busy workloads get more checks,
   idle workloads get fewer (but also use less stack).

2. **Never in tight loops** — don't check inside `serial_putc` or `puts`.
   The overhead would be excessive and the check is unnecessary for leaf
   functions.

### 9.4 Canary Layout

```
0x7000  ┌──────────────┐
        │ 0xDEAD       │  ← canary word 1 (first to be overwritten)
0x7002  ├──────────────┤
        │ 0xDEAD       │  ← canary word 2 (redundancy)
0x7004  ├──────────────┤
        │              │
        │  Usable      │  ← SP grows downward from 0x7BFF
        │  stack zone  │     ~3068 bytes of safe stack space
        │  (~3068 B)   │
        │              │
0x7C00  └──────────────┘  ← Initial SP (set by MBR)
```

If the stack grows past 0x7004 and overwrites the canary, the next
`canary_check` call detects the corruption and halts with a diagnostic
message on both serial and screen.

---

## 10. Implementation Plan

### 10.1 File Organization

```
src/include/
├── debug.inc           ← DBG, DBG_REG, DBG_REGS, ASSERT macros
├── serial.inc          ← serial_init, serial_putc, serial_puts,
│                              serial_hex8, serial_hex16, serial_crlf
├── syscalls.inc        (existing — SYS_* function numbers)
├── bib.inc             (existing)
├── memory.inc          (existing — includes STACK_CANARY_* constants)
├── mnfs.inc            (existing)
├── find_file.inc       (existing)
├── load_binary.inc     (existing)
└── boot_msg.inc        (existing)

src/kernel/
├── kernel.asm          (existing — main kernel entry)
├── kernel_syscall.inc  (existing — syscall dispatcher with CANARY_CHECK)
├── kernel_data.inc     (existing — string constants)
├── kernel_fault.inc    (existing — CPU exception handlers)
└── kernel_stack.inc    ← canary_init, canary_check, CANARY_INIT/CHECK macros
```

### 10.2 Integration Points

| Binary | Changes |
|--------|---------|
| LOADER | `%include "serial.inc"` + `%include "debug.inc"` + call `serial_init` early + add DBG calls |
| KERNEL | `%include "serial.inc"` + `%include "debug.inc"` + `%include "kernel_stack.inc"` + syscall tracing + fault handlers + CANARY_INIT at boot + CANARY_CHECK at syscall entry |
| FS.SYS | `%include "debug.inc"` (serial funcs from kernel via far call or duplicated) + DBG/ASSERT calls |
| SHELL | `%include "debug.inc"` + `mnmon` command (always-on) + canary_check in main loop |

### 10.3 Implementation Status

| # | Item | Status |
|---|------|--------|
| 1 | `serial.inc` — serial port init + putc/puts/hex | ✅ Done (v0.7.0) |
| 2 | `debug.inc` — DBG, DBG_REG, DBG_REGS macros | ✅ Done (v0.7.0) |
| 3 | Build system — unified build with both variants | ✅ Done (v0.8.0) |
| 4 | Kernel syscall tracing — named trace in `syscall_handler` | ✅ Done (v0.7.0) |
| 5 | FS tracing — named trace in `fs_syscall_handler` | ✅ Done (v0.7.0) |
| 6 | Hyper-V COM1 setup — `setup-vm.ps1` + `read-serial.bat` | ✅ Done (v0.7.0) |
| 7 | Unified VHD — boot menu selects release/debug at runtime | ✅ Done (v0.8.0) |
| 7b | User-mode debug syscalls — SYS_DBG_PRINT/HEX16/REGS with caller tags | ✅ Done (v0.7.1) |
| 7c | Shell debug tracing — `[SHL]` tagged messages at init, dispatch, errors | ✅ Done (v0.7.1) |
| 8 | Assert macros — ASSERT, ASSERT_MAGIC, ASSERT_CF_CLEAR | ✅ Done (v0.7.2) |
| 9 | Fault handlers — INT 0 (div-by-zero), INT 6 (invalid opcode), etc. | ✅ Done (v0.7.4) |
| 10 | Stack canary — canary_init in kernel, canary_check in dispatcher | ✅ Done (v0.8.1) |
| 11 | `mnmon` command — Wozmon-style machine monitor in shell | 📋 Future |

### 10.4 Backwards Compatibility

- **Release builds are unchanged** — without `-dDEBUG`, every macro expands
  to nothing.  Binary sizes, memory layout, and behavior are identical.
- **No new syscalls required** — all debug infrastructure is kernel-internal
  (serial port I/O is direct, not via INT 0x80).
- **`mnmon` command** is always-available (not conditional on DEBUG).  It's a
  learning tool in the Wozmon tradition and adds ~620 bytes to the shell.

---

## 11. Hyper-V Serial Debugging Walkthrough

### 11.1 VM Setup

COM1 is automatically configured by `setup-vm.bat`.  No manual steps needed.

### 11.2 Capture Session

Use `read-serial.bat` — it manages the entire lifecycle:

```cmd
:: Build debug VHD, set up VM (first time), then capture serial:
build.bat
setup-vm.bat          :: select "debug" when prompted for VHD variant
read-serial.bat       :: stops VM, restarts, captures from first byte
```

The reader auto-reconnects on VM reboot/reset.  Press Ctrl+C to stop.

Open the VM console separately to see display output:
```cmd
vmconnect localhost mini-os
```

### 11.3 Actual Debug Output (v0.7.1)

```
[DBG] KERNEL: serial debug active       ← serial_init completed, COM1 ready
[DBG] KERNEL: INT 0x80 installed        ← syscall jump table wired into IVT
[SYS] READ_SECTOR AX=0400 BX=0983      ← FS.SYS loading directory sector
[DBG] KERNEL: FS.SYS loaded at 0x0800   ← filesystem module in memory
[DBG] KERNEL: INT 0x81 filesystem ready ← FS INT 0x81 handler installed
[DBG] KERNEL: SHELL.SYS loaded          ← shell binary loaded at 0x3000
[SYS] CLEAR_SCREEN AX=060C BX=0000     ← shell clearing screen
[SYS] DBG_PRINT AX=2000 BX=xxxx        ← shell calling debug print syscall
[SHL] shell starting                    ← user-mode debug message
[SYS] PRINT_STRING AX=010C BX=xxxx     ← shell printing banner
[SYS] PRINT_STRING AX=010C BX=xxxx     ← shell printing prompt
[SYS] READ_KEY AX=030C BX=xxxx         ← shell waiting for keypress
```

---

## 12. Future Extensions

These are not yet implemented but would be natural additions later:

| Feature | Description |
|---------|-------------|
| **Breakpoint (INT 3)** | Single-byte `0xCC` instruction, handler dumps state + waits for keypress to continue |
| **Single-step mode** | Set TF (trap flag) to trace one instruction at a time |
| **Watchpoint** | Monitor a memory address for changes (check on every syscall) |
| **Ring buffer** | Keep last N debug messages in memory; dump on fault |
| **GDB stub** | Implement the GDB remote protocol over serial for source-level debugging |
| **Memory map command** | Display the live memory map (which regions are in use) |
| **I/O port dump** | Read and display UART, PIC, PIT, keyboard controller registers |
