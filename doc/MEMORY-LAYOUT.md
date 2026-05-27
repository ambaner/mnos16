# Memory Layout Design Document

This document provides an exhaustive description of how mini-os uses the x86
real-mode address space — every memory region, its purpose, its lifetime, and
the constraints that shaped its placement.  It also discusses stack sizing,
real-mode memory limits, and the roadmap to extended/protected-mode memory.

---

## 1. Real-Mode Address Space Overview

In 16-bit real mode, the CPU uses segment:offset addressing.  The effective
(linear) address is `segment × 16 + offset`, giving a 20-bit address space
of exactly **1 MB** (0x00000–0xFFFFF).  However, not all of this is usable:

```
0x00000 ┌──────────────────────────────────────────────┐
        │ Interrupt Vector Table (IVT)          1 KB   │  BIOS-owned
0x00400 ├──────────────────────────────────────────────┤
        │ BIOS Data Area (BDA)                  256 B  │  BIOS-owned
0x00500 ├──────────────────────────────────────────────┤
        │                                              │
        │        Conventional memory (free)            │  ~30 KB usable
        │        Used by mini-os (see §2)              │
        │                                              │
0x07C00 ├──────────────────────────────────────────────┤
        │ Boot sector load point                512 B  │  BIOS loads MBR here
0x07E00 ├──────────────────────────────────────────────┤
        │        Conventional memory (free)            │  ~608 KB
        │                                              │
0x9FC00 ├──────────────────────────────────────────────┤  (varies by platform)
        │ Extended BIOS Data Area (EBDA)        ~1 KB  │  BIOS-owned
0xA0000 ├──────────────────────────────────────────────┤
        │ Video memory (VGA framebuffer)        128 KB │  Hardware-mapped
0xC0000 ├──────────────────────────────────────────────┤
        │ Video BIOS ROM                        32 KB  │  Read-only
0xC8000 ├──────────────────────────────────────────────┤
        │ Adapter ROMs / Expansion area         160 KB │  Hardware-mapped
0xF0000 ├──────────────────────────────────────────────┤
        │ System BIOS ROM                       64 KB  │  Read-only
0xFFFFF └──────────────────────────────────────────────┘
```

**Usable conventional memory** spans 0x00500 through approximately 0x9FBFF
(~639 KB).  The exact upper bound depends on the BIOS and is reported by
INT 12h.  In a Hyper-V Gen 1 VM with 32 MB RAM, INT 12h typically reports
639 KB (0x9FC00).

Everything above 0xA0000 is reserved for video memory, adapter ROMs, and the
system BIOS.  The CPU cannot use these addresses for general-purpose code
or data in real mode — they are memory-mapped hardware regions.

---

## 2. mini-os Memory Map (v0.9.14)

mini-os uses the lower portion of conventional memory (0x0500–0xF7FF).  The
layout was designed around four constraints:

1. **BIOS expects the boot sector at 0x7C00** — this is non-negotiable.
2. **The stack must be below the boot sector** — SP starts at 0x7C00, growing
   downward.
3. **Loaded binaries must not overlap each other or the stack**.
4. **The Boot Info Block must be accessible by all stages at a fixed address**.

```
Address       Size      Contents                 Lifetime
─────────────────────────────────────────────────────────────────
0x0000:0x0000  1024 B   IVT (256 × 4-byte ptrs)  Permanent (BIOS)
0x0000:0x0400   256 B   BIOS Data Area (BDA)      Permanent (BIOS)
0x0000:0x0500   256 B   Free (BIOS-safe area)     Available

0x0000:0x0600    16 B   Boot Info Block (BIB)     Set by VBR+LOADER,
                         ├── 0x0600: boot_drive    read by all stages
                         ├── 0x0601: a20_status
                         ├── 0x0602: part_lba (4B)
                         ├── 0x0606: boot_mode (1B)
                         └── 0x0607–0x060F: reserved

0x0000:0x0610   496 B   (Unused gap)              Available for future use

0x0000:0x0800  ────── MODULE_FIRST_BASE ──────    Dynamic module area
               (up to   System modules packed     Kernel loads FS.SYS,
                ~18 KB)  sequentially by kernel:   MM.SYS, SHELL.SYS
                         ├── FS.SYS (~2.5 KB)     here at boot time.
                         ├── MM.SYS (~1 KB)       Addresses determined
                         └── SHELL.SYS (~7 KB)    dynamically via v2
                                                   relocation patching.
                         Ends before 0x5000.

0x0000:0x4E00   512 B   DIR_SCRATCH_BUF           Boot-time directory
                         (within module area)       scratch (loader uses
                                                    this for MNFS lookup)

0x0000:0x5000  8192 B   KERNEL.SYS                Permanent (OS runtime)
               (8 KB     (8 sectors = 4 KB used,   Loaded by LOADER at
                max)      4 KB growth room)         fixed address, keeps
                                                    [ORG 0x5000].  Installs
                                                    INT 0x80 syscall handler,
                                                    loads and relocates all
                                                    system modules.

0x0000:0x6C00           Stack canary zone          Debug builds: sentinel
                                                    value planted by kernel

0x0000:0x7000  3072 B   Stack zone (grows ↓)      Active (see §3)
               (3 KB)    SP starts at 0x7C00,
                         can grow down to ~0x7000

0x0000:0x7C00  1024 B   VBR (boot-time only)      MBR copies VBR here;
               (2 sec)   Overwritten conceptually  code runs then jumps
                         once LOADER takes over     to loader — dead after

0x0000:0x7E00  8192 B   VBR load buffer           MBR loads VBR here
               (16 sec   (temporary staging area)  before copying to 0x7C00
                max)                                — dead after boot

0x0000:0x8000  30720 B  TPA (Transient Prog Area)  User programs loaded here
               (30 KB)   Programs loaded by shell   via implicit execution.
                          with v2 relocation         Assembled at ORG 0,
                          patching at load time.     relocated to 0x8000.
                          Discarded on return.

0x0000:0xF800           (End of TPA)               0xF7FF is last usable byte

0xFFFF:0x0010  65264 B  HEAP (HMA, managed by      Dynamic allocation region.
               (~64 KB)  MM.SYS via INT 0x82)       MCB-style block headers,
                          Requires A20 enabled.      first-fit, ES:BX access.

0x0000:0x9FC00           EBDA / BIOS reserved      Platform-dependent
...
0x000A0000               Video memory starts       Hardware-mapped
```

### 2.1 Region Details

#### Boot Info Block (BIB) — 0x0600 (16 bytes)

The BIB is a fixed-address parameter block that allows boot stages to pass
information forward without registers or stack.  It sits at 0x0600, just above
the BIOS Data Area, in a region that the BIOS guarantees is free.

| Offset | Size | Field | Written by | Read by |
|--------|------|-------|------------|---------|
| 0x0600 | 1 B | `boot_drive` | VBR | LOADER, KERNEL |
| 0x0601 | 1 B | `a20_status` | LOADER | KERNEL, SHELL (via syscall) |
| 0x0602 | 4 B | `part_lba` | VBR | LOADER, KERNEL (computes absolute LBAs) |
| 0x0606 | 1 B | `boot_mode` | LOADER | KERNEL, SHELL (0=release, 1=debug) |
| 0x0607 | 1 B | `int_depth` | KERNEL, FS | KERNEL, FS (INT nesting counter, debug) |
| 0x0608 | 8 B | *reserved* | — | Future expansion |

**Why 0x0600?**  This address sits in the "free area" between the BDA (ends at
0x04FF) and the traditional boot sector load point (0x7C00).  The real-mode
IVT ends at 0x03FF, BDA ends at 0x04FF, and 0x0500–0x05FF is documented as
available but is sometimes used by BIOS for temporary purposes during POST.
We chose 0x0600 to avoid any possible conflict, while keeping it low enough
that no boot binary would be placed there.

#### System Module Area — 0x0800 to ~0x4E00 (dynamic)

Starting in v0.9.14, the region from MODULE_FIRST_BASE (0x0800) to just below
the kernel (0x5000) holds all system modules, packed sequentially by the kernel
at boot time.  Module positions are **not fixed** — they depend on the size of
each preceding module.

**Boot-time dual use**: During boot, LOADER.SYS occupies 0x0800 temporarily.
After the loader jumps to the kernel, its code is dead.  The kernel then loads
FS.SYS starting at 0x0800 (overwriting the loader), followed by MM.SYS and
SHELL.SYS at the next available addresses.

**Current layout** (typical, depends on build):

| Module | Typical address | Sectors | Size |
|--------|----------------|---------|------|
| FS.SYS | 0x0800 | 5 | 2560 B |
| MM.SYS | ~0x1200 | 2 | 1024 B |
| SHELL.SYS | ~0x1A00 | 14 | 7168 B |
| (end) | ~0x3600 | — | — |

These addresses are approximate.  The actual positions include the v2 header
+ relocation table prepended by `pack_module.py`.  The kernel uses the v2
header's `reloc_count` and `entry_offset` to patch each module and compute
its entry point.

**Maximum**: MODULE_AREA_END (0x5000).  If modules exceed this boundary, the
kernel would overwrite itself.  The build system validates total module size.

**Lifetime**: All modules are permanent after kernel init — they must remain
resident because the shell and user programs call into them via INT vectors.

#### KERNEL.SYS — 0x5000 (fixed, up to 8 KB)

The loader loads KERNEL.SYS to linear address 0x5000 (segment 0x0000, offset
0x5000).  The kernel is assembled with `[ORG 0x5000]` — it is the **only**
system binary that retains a hardcoded ORG, because the loader that places it
is too simple to perform relocation.

**Contents** (v0.9.14): INT 0x80 IVT installation, 27+ syscall handlers,
module loading with dynamic placement and `apply_relocs` subroutine, version
info, `next_base` tracking for sequential module placement.

**Current size**: 8 sectors (4096 bytes), ending at 0x5FFF.  **Maximum**:
16 sectors (8192 bytes), ending at 0x6FFF.

**Lifetime**: Permanent — the kernel must remain resident for the entire OS
runtime because the shell and all user-mode programs call into it via INT 0x80.

#### VBR — 0x7C00 (boot-time only, 1 KB)

The MBR loads the VBR from disk to a staging area at 0x7E00, then copies it
to 0x7C00 (overwriting the MBR code).  The VBR runs at 0x7C00, same as the
MBR did — this is by design so the VBR can be assembled with `[ORG 0x7C00]`,
matching the standard boot sector origin.

After the VBR jumps to LOADER.SYS, the memory at 0x7C00 is no longer needed.
However, it is not reusable because the stack grows downward *into* this
region (see §3).

#### VBR Load Buffer — 0x7E00 (temporary, up to 8 KB)

The MBR uses this area to stage the VBR before copying it to 0x7C00.  After
the copy, this memory is free.  In v0.4.0, only 1 KB (2 sectors) is used,
but the MBR supports loading up to 16 sectors here.

---

## 3. The Stack

### 3.1 Configuration

The stack is initialized by the MBR at boot:

```asm
xor ax, ax
mov ss, ax          ; Stack segment = 0x0000
mov sp, 0x7C00      ; Stack pointer = 0x7C00
```

This places the stack at linear address 0x7C00, growing **downward**.  The
first `push` writes to 0x7BFE (SP decrements by 2 before writing).

### 3.2 Available Stack Space

The stack can grow from 0x7C00 downward.  The nearest structure below it is
KERNEL.SYS, which currently extends to 0x57FF (4 sectors × 512 = 2048 bytes
from 0x5000).

```
                    KERNEL.SYS end   Stack bottom     SP initial
                    (current)        (safe limit)     (top)
                         │                │               │
  0x5000 ──── 0x57FF ──── 0x7000 ──────── 0x7BFE ──── 0x7C00
         ╰───2 KB────╯    ╰───3 KB stack───╯
```

**Current available stack**: 0x7C00 − 0x4400 = **14,336 bytes (14 KB)**.
This is the gap between SHELL.SYS's current end and SP's initial value.

**Safe stack budget**: ~3 KB (0x7000–0x7C00).  We use 0x7000 as the
conservative lower bound, leaving 11 KB of headroom between the end of
SHELL.SYS and the bottom of the safe stack zone.  As SHELL.SYS grows
toward its 16 KB maximum (ending at 0x6FFF), the stack zone tightens to
exactly the 3 KB budget.

**Worst case** (shell at max 16 KB): 0x7C00 − 0x7000 = **3072 bytes**.

### 3.3 Stack Usage Analysis

How deep does the stack actually go?  In mini-os's real-mode code:

| Operation | Stack cost | Context |
|-----------|-----------|---------|
| `call` instruction | 2 B (return address) | Every subroutine call |
| `push ax` / `push bx` etc. | 2 B per register | Register saves |
| INT 13h (BIOS disk read) | ~20–40 B | BIOS internal use |
| INT 15h (memory services) | ~10–20 B | BIOS internal use |
| INT 10h (video services) | ~10–20 B | BIOS internal use |

**Deepest call chain** (estimated): `shell_loop` → `cmd_mem` →
`show_e820_map` → `print_hex16` → `puthex8` → `putc` → `INT 10h`.
That's ~6 levels of calls (12 B return addresses) plus register saves
(~20 B) plus BIOS internals (~40 B) = **~72 bytes**.

Even the worst-case scenario (deeply nested commands with BIOS calls) uses
well under 200 bytes of stack.  The 3 KB budget is generous — over 15×
the actual requirement.

### 3.4 Stack Does Not Relocate

The stack is set once by the MBR and never moved.  All three boot stages
(VBR, LOADER, SHELL) inherit the same SS:SP.  This is deliberate:

- No stage needs a different stack size.
- Relocating the stack mid-boot risks losing return addresses.
- A fixed stack simplifies debugging (SP is always relative to 0x7C00).

### 3.5 Stack Canary (debug builds only)

In debug builds, the kernel plants a 4-byte sentinel at the stack floor:

```
0x7000  ┌──────────────┐
        │ 0xDEAD       │  ← canary word 1 (first to be overwritten)
0x7002  ├──────────────┤
        │ 0xDEAD       │  ← canary word 2 (redundancy)
0x7004  ├──────────────┤
        │              │
        │  Usable      │  ← SP grows downward from 0x7BFF
        │  stack zone  │     ~3068 bytes of safe stack space
        │              │
0x7C00  └──────────────┘  ← Initial SP
```

`canary_init` writes `0xDEAD` to both words at kernel boot.  `canary_check`
verifies them on every syscall entry (INT 0x80).  If either word is corrupted,
the handler prints a fatal message to serial + screen and halts.  The canary
uses the `SS:` segment override so it works regardless of DS.  In release
builds, the canary code expands to 0 bytes — the addresses at 0x7000 are
simply unused.

See `doc/DEBUGGING.md` §9 and `src/kernel/kernel_stack.inc` for implementation.

---

## 4. Transient vs. Permanent Memory

Not all memory regions are active simultaneously.  Understanding lifetimes
helps identify what can be reclaimed:

```
Time →    MBR runs    VBR runs    LOADER runs    KERNEL init    SHELL runs
          ────────    ────────    ───────────    ───────────    ──────────
0x0600    (free)      BIB ████    BIB ████████   BIB ████████   BIB ████████
0x0800    (free)      (free)      LOADER █████   FS.SYS █████   FS.SYS █████
0x3000    (free)      (free)      (free)         SHELL ██████   SHELL ██████
0x5000    (free)      (free)      (free)         KERNEL █████   KERNEL █████
0x7C00    MBR █████   VBR ██████  (dead code)    (dead code)    (dead code)
0x7E00    (free)      VBR buf ██  (free)         (free)         (free)
Stack     ████████    ████████    ████████████   ████████████   ████████████

████ = Active    ░░░░ = Dead (reclaimable)    (free) = Never used
```

**Reclaimable after SHELL starts running:**

| Region | Address | Size | Notes |
|--------|---------|------|-------|
| FS.SYS growth room | 0x0C00–0x27FF | 7 KB | FS.SYS is 1 KB, rest unused |
| FS–shell gap | 0x2800–0x2FFF | 2 KB | Never used |
| VBR code | 0x7C00–0x7FFF | 1 KB | Overlaps stack zone |
| VBR staging buffer | 0x7E00–0x9DFF | 8 KB | Fully free |

**Total reclaimable**: ~18 KB (not counting VBR area which overlaps with
stack).  A future memory manager could return these regions to a free pool.
Note: 0x0800 is now occupied permanently by FS.SYS (was reclaimable in v0.4.0).

---

## 5. Disk Address Packets (DAP)

Each boot stage that reads from disk maintains a 16-byte DAP structure in
its own code/data section:

```
DAP Structure (16 bytes):
  Offset 0:  db 0x10      ; Size of packet (always 16)
  Offset 1:  db 0         ; Reserved
  Offset 2:  dw N         ; Number of sectors to read
  Offset 4:  dw offset    ; Destination offset
  Offset 6:  dw segment   ; Destination segment
  Offset 8:  dq LBA       ; Starting LBA (64-bit)
```

| Stage | DAP location | Loads what | Destination |
|-------|-------------|------------|-------------|
| MBR | Within MBR code (~0x7D4E) | VBR sectors | 0x0000:0x7E00 |
| VBR | Within VBR code (~0x7E98) | LOADER.SYS | 0x0000:0x0800 |
| LOADER | Within LOADER code (~0x0907) | KERNEL.SYS | 0x0000:0x5000 |
| KERNEL | Within KERNEL code | FS.SYS | 0x0000:0x0800 |
| KERNEL | Within KERNEL code | SHELL.SYS | 0x0000:0x3000 |

DAP structures are part of their respective binaries and share the same
lifetime.  They are modified in place (sector count, LBA fields) during the
two-phase load process (read 1 sector for header, then read all sectors).

---

## 6. A20 Verification Probe Addresses

The `check_a20` subroutine (in both LOADER and SHELL) tests whether the A20
address line is enabled by exploiting the 8086 memory wrap-around:

```
Test address 1:  0x0000:0x0500  (linear 0x00500)
Test address 2:  0xFFFF:0x0510  (linear 0x100500 — wraps to 0x00500 if A20 off)
```

If A20 is disabled, writing to 0xFFFF:0x0510 modifies 0x0000:0x0500 (because
bit 20 of the address is masked).  If A20 is enabled, they are two distinct
addresses 1 MB apart.

**Why 0x0500?**  It must be a low-memory address that is safe to write to
temporarily.  0x0500 is in the "free" area below 0x0600 (our BIB) — it is
not used by the IVT, BDA, or any boot stage.  The original values are saved
and restored after the test.

---

## 7. Memory Map as Seen by INT 15h E820

The `mem` command calls INT 15h EAX=0xE820 to query the firmware's view of
physical memory.  On a Hyper-V Gen 1 VM with 32 MB RAM, a typical E820 map
looks like:

```
Base Address     Length         Type
0x00000000       0x0009FC00     Usable (639 KB)
0x0009FC00       0x00000400     Reserved (EBDA)
0x000E0000       0x00020000     Reserved (BIOS ROM area)
0x00100000       0x01F00000     Usable (31 MB — this is "extended memory")
0xFFFC0000       0x00040000     Reserved (high BIOS)
```

**Important**: The E820 map describes *physical* memory as seen by the
firmware.  In real mode, the CPU can only address the first 1 MB (plus ~64 KB
if A20 is enabled and using segment tricks).  The 31 MB of extended memory
starting at 0x100000 is visible to E820 but **not directly accessible** until
the CPU switches to protected mode or uses unreal mode.

---

## 8. Future: Beyond 1 MB

### 8.1 The A20 Gate (Current — v0.3.0+)

The A20 gate is enabled by LOADER.SYS at boot.  This is a prerequisite for
any memory access above 1 MB, but enabling A20 alone is **not sufficient**.
It simply unmasks address line 20 so the CPU does not wrap at 1 MB.

In real mode, even with A20 enabled, the CPU is still limited by segment:offset
addressing.  The maximum address reachable is 0xFFFF:0xFFFF = 0x10FFEF
(~1 MB + 64 KB - 16 bytes).  Accessing memory beyond that requires leaving
real mode.

### 8.2 Unreal Mode (Potential Intermediate Step)

"Unreal mode" (also called "big real mode") is a technique where:

1. Switch to protected mode.
2. Load a segment register with a descriptor that has a 4 GB limit.
3. Switch back to real mode.
4. The segment register retains the 4 GB limit (CPU caches it).

This allows real-mode code to use 32-bit offsets to access memory above 1 MB
while still using BIOS interrupts (which require real mode).  Many bootloaders
use this trick to load kernels into extended memory.

**Pros**: BIOS services remain available; minimal code change.
**Cons**: Not officially supported by Intel; requires careful GDT setup.

### 8.3 Protected Mode (Planned Milestone)

The definitive solution is switching to 32-bit protected mode:

```
Real Mode (current)          Protected Mode (future)
─────────────────           ───────────────────────
20-bit addresses (1 MB)      32-bit addresses (4 GB)
Segment:offset               Flat memory model (or segmented)
BIOS interrupts available    BIOS interrupts NOT available
No memory protection         Page-level protection, rings 0–3
```

**What's needed**:

1. **Global Descriptor Table (GDT)** — Define code and data segments with
   base 0, limit 4 GB.  Minimum: null descriptor, code segment, data segment
   (24 bytes total).

2. **Switch sequence**:
   ```
   cli                    ; Disable interrupts
   lgdt [gdt_descriptor]  ; Load the GDT register
   mov eax, cr0
   or eax, 1             ; Set PE (Protection Enable) bit
   mov cr0, eax
   jmp 0x08:pm_entry     ; Far jump to flush pipeline + load CS
   ```

3. **32-bit kernel** — Once in protected mode, all code must be 32-bit.
   The shell (or a new kernel binary) would need to be reassembled as
   `[BITS 32]` code.

4. **No more BIOS** — In protected mode, INT 10h/13h/15h are unavailable.
   The kernel needs its own drivers:
   - **VGA text mode**: Direct writes to 0xB8000 framebuffer
   - **Keyboard**: 8042 controller via ports 0x60/0x64 with IRQ 1
   - **Disk**: ATA/IDE PIO via ports 0x1F0–0x1F7 with IRQ 14

### 8.4 Planned Memory Layout (Post Protected Mode)

```
0x00000000 ┌──────────────────────────────────────┐
           │ Real-mode structures (IVT, BDA)      │  Preserved for
           │ BIB, loader remnants                 │  reference
0x00100000 ├──────────────────────────────────────┤  (1 MB mark)
           │ Kernel code + data                   │  Loaded by LOADER
           │                                      │  before pmode switch
0x00200000 ├──────────────────────────────────────┤  (2 MB mark)
           │ Kernel heap                          │  Dynamic allocation
           │                                      │
           │                                      │
0x01F00000 ├──────────────────────────────────────┤  (31 MB — RAM end)
           │ Kernel stack (top)                   │  Grows downward
0x02000000 └──────────────────────────────────────┘  (32 MB total)
```

**Key decisions for the future**:
- Kernel is loaded above 1 MB (requires unreal mode or a two-step load).
- First 1 MB is preserved so the kernel can inspect boot-time data (BIB,
  E820 map results) if needed.
- Kernel stack placed at the top of available RAM.
- Heap grows upward from the end of kernel code.

### 8.5 Roadmap Summary

| Milestone | Memory Capability | Addressing |
|-----------|-------------------|------------|
| **v0.3.0** ✅ | A20 enabled (prerequisite) | Real mode, ~1 MB |
| **v0.4.0** ✅ | Multi-binary memory layout | Real mode, ~1 MB |
| **Future** | Unreal mode (optional) | Real mode + 32-bit offsets, 4 GB |
| **Future** | Protected mode switch | 32-bit flat, 4 GB |
| **Future** | Paging (optional) | Virtual memory, 4 GB virtual |

---

## 9. Design Constraints and Trade-offs

### Why not load SHELL.SYS higher (e.g., 0x8000)?

Loading above 0x7C00 would conflict with the VBR staging buffer at 0x7E00.
While the staging buffer is transient, the MBR is still using it when the
VBR hasn't run yet.  Keeping all persistent binaries below 0x7000 and all
transient boot buffers above 0x7C00 creates a clean separation.

### Why not use segment registers to get more address space?

We could use non-zero segment bases to spread binaries across the full 1 MB.
For example, loading SHELL.SYS at 0x5000:0x0000 (linear 0x50000).  However:

- **Complexity**: Every pointer must account for the segment.  A bug in
  segment setup means silent memory corruption.
- **No real benefit yet**: Our total code (MBR + VBR + LOADER + SHELL) is
  under 8 KB.  We have ~30 KB of free conventional memory below 0x7C00 —
  plenty of room without segment tricks.
- **BIOS compatibility**: Many BIOS services expect ES:BX or DS:SI with
  segment 0.  Non-zero segments can cause subtle bugs.

### Why is the LOADER–SHELL gap (0x2800–0x2FFF) wasted?

It is not wasted — it is **growth room**.  If LOADER.SYS grows beyond 2
sectors (which it will when it gains filesystem parsing or protected-mode
switching), it expands toward 0x27FF.  The 2 KB gap is a buffer zone that
prevents a larger loader from colliding with the shell.

---

## 10. Quick Reference Card

```
┌─────────────────────────────────────────────────────────┐
│            mini-os v0.7.0 Memory Quick Reference        │
├──────────┬──────────┬──────────────────────┬────────────┤
│ Start    │ End      │ Contents             │ Size       │
├──────────┼──────────┼──────────────────────┼────────────┤
│ 0x0000   │ 0x03FF   │ IVT                  │ 1024 B     │
│ 0x0400   │ 0x04FF   │ BDA                  │ 256 B      │
│ 0x0500   │ 0x05FF   │ Free (A20 test uses) │ 256 B      │
│ 0x0600   │ 0x060F   │ Boot Info Block      │ 16 B       │
│ 0x0610   │ 0x07FF   │ (unused)             │ 496 B      │
│ 0x0800   │ 0x0BFF   │ FS.SYS (2 sec)       │ 1 KB       │
│ 0x0C00   │ 0x27FF   │ (FS growth room)     │ 7 KB       │
│ 0x2800   │ 0x2FFF   │ (gap / buffer zone)  │ 2 KB       │
│ 0x3000   │ 0x47FF   │ SHELL.SYS (14 sec)   │ 6 KB       │
│ 0x4800   │ 0x4FFF   │ (shell growth)       │ 2 KB       │
│ 0x5000   │ 0x5DFF   │ KERNEL.SYS (7 sec)   │ 3.5 KB     │
│ 0x5E00   │ 0x6FFF   │ (kernel growth)      │ 4.5 KB     │
│ 0x7000   │ 0x7BFF   │ Stack zone           │ 3 KB       │
│ 0x7C00   │ 0x7FFF   │ VBR (boot-time)      │ 1 KB       │
│ 0x7E00   │ 0x9DFF   │ VBR staging (temp)   │ 8 KB       │
│ 0x9FC00  │ 0x9FFFF  │ EBDA                 │ 1 KB       │
│ 0xA0000  │ 0xBFFFF  │ Video memory         │ 128 KB     │
│ 0xC0000  │ 0xFFFFF  │ ROMs + BIOS          │ 256 KB     │
├──────────┴──────────┴──────────────────────┴────────────┤
│ Total code loaded: 11,776 B (MBR 512 + VBR 1K + LOADER │
│         1K + FS 1K + KERNEL 3K + SHELL 6K) — ~2% of    │
│         640 KB                                          │
└─────────────────────────────────────────────────────────┘
```

---

## 11. Debug vs. Release Build Sizes

Debug builds (`build.bat /debug`) include serial logging functions, syscall
tracing, and DBG macros via `%ifdef DEBUG`.  This increases some binaries,
but **the runtime memory map is identical** — every component loads at its
hardcoded address.  Debug code simply occupies more bytes within each
pre-allocated region.

| Component | Address | Release | Debug | Growth room left |
|-----------|---------|---------|-------|-----------------|
| FS.SYS | 0x0800 | 1 KB (2 sec) | 2 KB (4 sec) | 6 KB |
| SHELL.SYS | 0x3000 | 7 KB (14 sec) | 7 KB (14 sec) | 2 KB |
| KERNEL.SYS | 0x5000 | 3.5 KB (7 sec) | 5 KB (10 sec) | 4.5 KB |

Each binary's header contains a conditional sector count (`%ifdef DEBUG`),
so the loader reads the correct size at runtime.  No code changes are needed
to accommodate the larger debug binaries — the existing load logic handles it.

The **disk layout** does differ (files pack at different sector offsets),
but this is transparent because the MNFS directory is self-describing.
See DEBUGGING.md §8.5 for the full disk layout comparison.

---

*Document created: 2026-05-12*
*Relates to: DESIGN.md §2.2 (Memory Layout), BOOT-LAYOUT-RATIONALE.md, DEBUGGING.md §8*
