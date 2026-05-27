# MNOS16 Application Binary Interface (ABI) Contract

**Version**: 1.0  
**Effective from**: MNOS16 v0.9.14  
**Status**: Stable — programs compiled against this ABI will run on all future versions.

## Purpose

This document defines the **binary portability guarantee** for MNOS16 user
programs (.MNX executables).  Any program assembled against this ABI is
guaranteed to load and execute correctly on the current and all future
versions of MNOS16, subject to the constraints below.

## Guarantees (MUST NOT break across versions)

### 1. Transient Program Area (TPA)

| Property | Value | Notes |
|----------|-------|-------|
| Load address | `0x8000` | Default TPA base (may change in future versions) |
| Maximum size | 30 KB (0x7800 bytes) | Including v2 header + relocation table |
| Entry point | From v2 header `entry_offset` field | Absolute = load_base + entry_offset |

### 2. MNEX v2 Header Format (all programs since v0.9.14)

```
Offset 0:  db 'MNEX'          ; Magic signature (4 bytes)
Offset 4:  dw sector_count    ; Total size in 512-byte sectors
Offset 6:  dw flags           ; Bit 0: has relocation table (0x0001)
Offset 8:  dw reloc_count     ; Number of 16-bit relocation entries
Offset 10: dw entry_offset    ; File-relative offset to first instruction
Offset 12: dw reloc_table[]   ; reloc_count × 2-byte file-relative offsets
           ...code/data...    ; Code begins at entry_offset from file start
```

The shell applies relocations at load time by adding the TPA load address
to each 16-bit word at the offsets listed in the relocation table. This
makes programs independent of the TPA address — they will run correctly
regardless of where they are loaded.

### 2b. Legacy MNEX v1 Header (backward compatibility)

```
Offset 0: db 'MNEX'      ; Magic signature (4 bytes)
Offset 4: dw sectors     ; Program size in 512-byte sectors
Offset 6: <code entry>   ; First executable instruction
```

The shell uses a **two-stage v2 detection** algorithm:
1. **Flag check**: `word [load_base + 6] & 0x0001` — if clear, definitely v1.
2. **Secondary validation**: if flag is set, verify that
   `entry_offset == 12 + reloc_count * 2`.  If this invariant fails, the
   program is v1 (the first opcode just happened to have bit 0 set).

This prevents false-positive v2 detection for v1 programs starting with
opcodes like `jmp short` (0xEB), `push bp` (0x55), or `ret` (0xC3).

V1 programs are NOT relocatable and will only work if TPA remains at 0x8000.

### 3. CPU State at Entry

| Register | Value | Guaranteed |
|----------|-------|-----------|
| CS | 0x0000 | ✓ Always |
| DS | 0x0000 | ✓ Always |
| ES | 0x0000 | ✓ Always |
| SS | 0x0000 | ✓ Always |
| SP | 0x7C00 | ✓ Always |
| IP | entry_offset + load_base | ✓ Always (from v2 header) |
| Direction flag | Clear (CLD) | ✓ Always |
| Interrupts | Enabled (STI) | ✓ Always |

### 4. Syscall Interface

Programs interact with the OS exclusively through software interrupts:

| Vector | Service | Stable |
|--------|---------|--------|
| INT 0x80 | Kernel services | ✓ |
| INT 0x81 | Filesystem services | ✓ |
| INT 0x82 | Memory management | ✓ |

**Calling convention**:
- `AH` = function number (1-based)
- Other registers = function-specific arguments
- Return: function-specific (CF=1 on error, AX may contain error code)

### 5. Unknown Syscall Handling

If a program calls a syscall function number that does not exist in the
running OS version:

- **CF is set** (carry flag = 1)
- The program does NOT crash
- AX may be undefined

This allows forward-compatible programs to probe for new features:
```nasm
    mov ah, 0xFF        ; hypothetical future syscall
    int 0x80
    jc .not_supported   ; CF=1 means "unknown"
```

### 6. Program Exit

Programs return to the shell via:
```nasm
    mov ah, SYS_EXIT    ; AH = 0x0A
    int 0x80
```
This restores the shell's stack and resumes the command prompt.

### 7. Command-Line Arguments

| Address | Content |
|---------|---------|
| `0x7F00` | `argc` (1 byte) |
| `0x7F02` | `argv[0]` pointer (2 bytes) |
| `0x7F04` | `argv[1]` pointer ... |
| `0x7F22`–`0x7FFB` | NUL-separated argument strings |

Access via syscall:
- `SYS_GET_ARGC` (AH=0x0B) → AL = argument count
- `SYS_GET_ARGV` (AH=0x0C, AL=index) → SI = pointer to string

## Constraints (programs MUST obey)

### What programs MUST NOT do:

1. **Reference fixed addresses below TPA** — the module area layout is internal
   and changes between versions
2. **Hardcode the TPA load address** — use relocatable assembly (`[ORG 0]`)
   and let the shell patch absolute references at load time
3. **Hook or modify IVT entries** — the kernel owns interrupt vectors
4. **Assume positions of system modules** — FS, MM, SHELL are relocatable
5. **Use direct port I/O** — use syscalls instead
6. **Call BIOS interrupts directly** — use INT 0x80/0x81/0x82
7. **Modify memory below 0x7F00** — reserved for OS use
8. **Assume stack contents below SP** — the OS may use sub-SP memory

### What programs MAY do:

1. Use all memory allocated to the TPA (currently 30 KB)
2. Use the stack (grows down from 0x7C00, ~4 KB available)
3. Call any documented syscall
4. Read the argv table at 0x7F00–0x7FFB
5. Set up their own interrupt handlers (e.g., timer) if they restore on exit

## Version Discovery

Programs can query the OS version at runtime:
```nasm
    mov ah, SYS_GET_VERSION    ; AH = 0x09
    int 0x80
    ; AL = major version, AH = minor version
```

## Compatibility Matrix

| Scenario | Behavior |
|----------|----------|
| Old program on new OS | ✓ Runs correctly |
| New program on old OS (uses unknown syscall) | Program gets CF=1, can handle gracefully |
| Program accesses internal addresses | ⚠️ UNDEFINED — may crash |
| Program modifies IVT | ⚠️ UNDEFINED — may crash |

## Rationale

MNOS16 v0.9.14 introduced relocatable system modules, meaning the internal
memory layout (positions of FS.SYS, MM.SYS, SHELL.SYS) can change between
builds and versions.  This ABI contract ensures that user programs are
insulated from these internal changes by providing stable external interfaces.

The key design principles:
1. **Fixed TPA** — programs always load at 0x8000, always
2. **Stable syscalls** — function numbers never change meaning
3. **Graceful degradation** — unknown syscalls return error, not crash
4. **Separation of concerns** — programs use services, not internal structures
