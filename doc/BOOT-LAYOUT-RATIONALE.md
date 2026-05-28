# Boot Layout Design Rationale

This document captures the design discussions, historical comparisons, and
trade-off analysis that led to the three-stage boot chain architecture adopted
in mini-os v0.4.0.

---

## 1. The Problem: Monolithic VBR Growth

Through versions v0.2.0–v0.3.0, all OS functionality lived inside a single
multi-sector VBR (Volume Boot Record). The VBR grew from 1 sector to 16 sectors
(8 KB) as features were added:

| Version | Feature Added            | VBR Usage   |
|---------|--------------------------|-------------|
| v0.2.0  | Basic VBR boot message   | ~5%         |
| v0.2.2  | System info (4 pages)    | ~35%        |
| v0.2.5  | Interactive shell        | ~50%        |
| v0.2.6  | `mem` command            | ~55%        |
| v0.2.7  | `ver` + CPUID/EDD pages  | ~60%        |
| v0.3.0  | A20 gate enablement      | ~62% (5104 / 8192 B) |

At 62% capacity with only 6 shell commands, the monolithic VBR was clearly not
sustainable. Adding a handful more features would exhaust the 8 KB boot area.

### Why not just make the VBR bigger?

While the VBR *could* be enlarged beyond 16 sectors, this creates several
problems:

1. **No protection** — The VBR occupies raw sectors at the start of the
   partition. Disk management utilities, partitioning tools, and filesystem
   formatters all assume this area is small (typically 1–2 sectors for the
   boot record, plus a few reserved sectors). A large VBR has no structural
   protection against being overwritten.

2. **Memory constraints** — In real mode, usable memory below 0x7C00 is
   limited. A massive VBR loaded as a single blob competes with the stack,
   BIOS data area, and interrupt vectors.

3. **Architectural debt** — Mixing boot logic (disk reads, A20 enablement)
   with application logic (shell commands, sysinfo display) in a single file
   makes the code harder to maintain and reason about.

---

## 2. How Others Solved This

Before choosing an approach, we surveyed how three major operating systems
handled the transition from boot record to operating system.

### 2.1 MS-DOS 6.22 — Filesystem-Based Loading

DOS used a minimal boot sector (1 sector, 512 bytes) that understood the FAT12
or FAT16 filesystem. The boot sequence:

```
MBR → PBR (1 sector) → IO.SYS → MSDOS.SYS → COMMAND.COM
```

- The **Partition Boot Record (PBR)** contained a BIOS Parameter Block (BPB)
  describing the FAT filesystem, plus just enough code to locate and load
  `IO.SYS` from the root directory.
- **IO.SYS** was a regular file on the FAT filesystem. It had to be the first
  file in the root directory and occupy contiguous clusters (early DOS required
  this; later versions relaxed it).
- **MSDOS.SYS** provided the DOS kernel.
- **COMMAND.COM** was the interactive shell.

**Key insight**: DOS could keep its boot sector tiny because FAT12/16 is simple
enough to parse in 512 bytes. The trade-off was that the boot sector was tightly
coupled to the filesystem format — a different filesystem would require a
completely different boot sector.

**Relevance to mini-os**: We have no filesystem yet, so we cannot take this
approach today. However, this validates the eventual goal of adding filesystem
support to the VBR and loading binaries by name.

### 2.2 Windows NT/2000/XP — Pre-UEFI Chain Loading

Windows used a more layered approach:

```
MBR → PBR (1 sector) → NTLDR → ntoskrnl.exe
```

- The **PBR** understood NTFS (or FAT) just enough to locate and load `NTLDR`
  from the root of the boot partition.
- **NTLDR** was a large binary that handled everything: hardware detection,
  `boot.ini` parsing, filesystem traversal, protected mode switch, and kernel
  loading.
- The PBR was *still* just 1 sector (512 bytes for FAT, or 16 sectors for NTFS
  — but the NTFS PBR's extra sectors are part of the NTFS specification, not
  arbitrary boot code).

**Key insight**: Windows kept the boot sector minimal and filesystem-aware.
All complex logic lived in a regular file (`NTLDR`) on the filesystem. The
NTFS boot sector used multiple sectors, but these were structurally defined by
the filesystem specification — not ad-hoc boot code.

**Relevance to mini-os**: The pattern of "tiny boot sector loads a bigger
loader binary" is exactly what we adopted. The difference is that Windows
could use the filesystem to find NTLDR, while we use fixed disk offsets.

### 2.3 Linux — Dedicated Boot Partition

Linux introduced the most modular approach:

```
MBR → GRUB Stage 1 → GRUB Stage 1.5/2 → vmlinuz → initramfs → /sbin/init
```

- **GRUB Stage 1** (446 bytes in MBR or 512 bytes in PBR) loads Stage 1.5.
- **GRUB Stage 1.5** (~32 KB) lives in the "MBR gap" (sectors 1–2047 before
  the first partition). It contains a filesystem driver.
- **GRUB Stage 2** lives on the `/boot` partition as a regular file. It
  provides the boot menu, kernel selection, and OS loading.
- The **kernel** (`vmlinuz`) and **initial ramdisk** (`initramfs`) also live
  on `/boot`.
- The **shell** (bash) lives on the *root filesystem* (`/`), not on `/boot`.

**Key insight**: Linux separates concerns cleanly:
- `/boot` partition: only what's needed to get the kernel running (bootloader,
  kernel, initramfs, bootloader config).
- Root partition: everything else (shell, utilities, applications).

The `/boot` partition can use a simple filesystem (ext2, no journaling needed)
that the bootloader can parse. The root partition uses a full-featured
filesystem (ext4, XFS, etc.) that only the kernel needs to understand.

**Relevance to mini-os**: This is the long-term model we want to evolve toward.
The current fixed-offset approach is a stepping stone — eventually the VBR
will parse a simple filesystem to find LOADER.SYS and SHELL.SYS.

### 2.4 Comparison Summary

| Aspect | DOS 6.22 | Windows NT | Linux/GRUB | mini-os v0.4.0 |
|--------|----------|------------|------------|----------------|
| Boot sector size | 1 sector | 1–16 sectors | 1 sector | 2 sectors |
| Finds loader via | FAT root dir | NTFS/FAT | MBR gap + FS | Fixed LBA offset |
| Loader binary | IO.SYS (file) | NTLDR (file) | GRUB Stage 2 (file) | LOADER.SYS (raw sectors) |
| Shell location | COMMAND.COM (file) | explorer.exe (file) | /bin/bash (file) | SHELL.SYS (raw sectors) |
| Filesystem required? | Yes (FAT) | Yes (NTFS/FAT) | Yes (ext2+) | **No** |
| Protected from clobber? | By FS metadata | By FS metadata | By FS metadata | **No** (raw offsets) |

---

## 3. The "LBA Reserved Area" Question

During design discussions, we considered whether the **LBA reserved area**
(sectors 1–2047 between the MBR and the first partition) would have been a
better place to store boot code than extending the VBR.

### What is the LBA reserved area?

When a disk uses LBA (Logical Block Addressing) with standard 1 MB partition
alignment, sectors 1 through 2047 sit between the MBR (sector 0) and the first
partition (sector 2048). This is 1,048,064 bytes (~1 MB) of space that no
partition owns.

### Arguments for using it

- **GRUB uses it** — GRUB Stage 1.5 lives here. It's a proven approach.
- **Large** — ~1 MB is far more than the 8 KB VBR boot area.
- **Outside partitions** — Partition-aware tools won't format or overwrite it.

### Arguments against (and why we didn't use it)

- **Not universally safe** — Some disk utilities, partition managers, and
  imaging tools assume this area is empty and may zero it.
- **Not part of any partition** — It has no structural ownership. There is no
  metadata saying "these sectors contain boot code." If a tool rewrites the
  MBR, the gap may be collateral damage.
- **Conceptual confusion** — The boot loader code logically belongs to the
  OS that lives in the partition, not to the disk as a whole. Storing it
  outside the partition creates a dependency that isn't tracked anywhere.
- **GRUB-specific** — GRUB gets away with it because GRUB *owns* the MBR too.
  The GRUB MBR knows exactly where Stage 1.5 lives because GRUB's installer
  wrote both. In mini-os, the MBR is generic — it finds the active partition
  and loads whatever VBR is there. Storing extra code in the gap would require
  the MBR to know about it, breaking the clean separation.

### Conclusion

Using the LBA gap would have given us more space earlier, but at the cost of
architectural cleanliness. The partition-internal approach (VBR + fixed offsets
within the partition) keeps all boot code within the partition it belongs to,
which is both simpler and more correct.

The real mistake wasn't "we should have used the gap" — it was "we should have
split the VBR into multiple binaries sooner." Which is exactly what v0.4.0 does.

---

## 4. Protection Against Clobbering

A recurring concern was: **how do we prevent disk management utilities from
overwriting our boot binaries?**

### How others solve this

All three OS families (DOS, Windows, Linux) solve this the same way: they store
boot binaries on a **filesystem**. The filesystem's metadata (FAT allocation
table, NTFS MFT, ext4 inode table) marks the clusters/blocks as allocated. Any
filesystem-aware tool will not overwrite allocated data.

### Our situation

mini-os v0.4.0 uses **fixed disk offsets** within the partition:

```
Partition offset 0:   VBR (2 sectors)
Partition offset 4:   LOADER.SYS (up to 16 sectors)
Partition offset 20:  SHELL.SYS (up to 32 sectors)
```

These offsets are *not* protected by any metadata. A disk utility that writes
to the partition (e.g., formatting it with FAT) would overwrite everything.

### Why this is acceptable (for now)

1. **No filesystem exists yet** — There is nothing to format. The partition
   type is 0x7F (experimental), so standard tools won't recognize or try to
   format it.

2. **The OS owns the entire disk** — mini-os is the sole OS on this VHD.
   There are no other tools running on it that could clobber the data.

3. **It's a stepping stone** — When we add filesystem support, the binaries
   will be tracked by the filesystem's allocation structures, gaining the same
   protection that DOS/Windows/Linux have.

---

## 5. The Role of the VBR Going Forward

With LOADER.SYS and SHELL.SYS extracted, the VBR's role has become minimal:

1. Receive control from MBR
2. Populate the Boot Info Block (BIB) with boot drive and partition LBA
3. Load LOADER.SYS from a fixed partition offset
4. Verify the magic number and jump

This raises the question: **why keep the VBR multi-sector (2 sectors) if it's
so simple?**

### Answer: future filesystem support

When mini-os eventually gains a filesystem, the VBR will need to:

1. Parse filesystem metadata (superblock, allocation table, directory entries)
2. Locate LOADER.SYS by filename
3. Handle fragmented files (read multiple extents/clusters)

A FAT12 BPB alone is 62 bytes. Directory parsing, cluster chain following, and
multi-sector reads will easily exceed 512 bytes. Keeping the VBR at 2 sectors
(1 KB) — with room to grow to 4 sectors — provides space for a filesystem
driver without needing another architectural overhaul.

The VBR will transition from:

```
Current:   VBR reads LOADER.SYS from fixed LBA offset
Future:    VBR reads LOADER.SYS from filesystem by filename
```

The three-stage architecture stays the same. Only the VBR's method of locating
LOADER.SYS changes.

---

## 6. Architecture Decision Record

| Decision | Chosen | Alternatives Considered | Rationale |
|----------|--------|------------------------|-----------|
| Boot chain stages | 3 (VBR → LOADER → SHELL) | 2 (VBR → SHELL), monolithic VBR | Separation of concerns; each stage has a clear role |
| Binary location | Fixed partition offsets | LBA gap, embedded in VBR, filesystem | No FS yet; partition-internal is cleaner than gap |
| Binary discovery | Magic number + sector count header | Hardcoded sizes, filesystem lookup | Self-describing; forward-compatible with FS |
| VBR size | 2 sectors (1 KB) | 1 sector, 16 sectors | Room for future FS driver; not wasteful |
| Parameter passing | Fixed-address BIB at 0x0600 | Registers, stack, segment tricks | Readable, extensible, survives stage transitions |
| Partition type | 0x7F | Standard types, GPT | Low collision risk; signals "not a standard FS" |

---

## 7. Evolution Path

```
v0.1.0–v0.3.0          v0.4.0                    Future
─────────────          ──────                    ──────
┌──────────┐     ┌─────┐ ┌──────┐ ┌─────┐     ┌─────┐ ┌──────┐ ┌─────┐
│ Monolith │     │ VBR │→│LOADER│→│SHELL│     │ VBR │→│LOADER│→│SHELL│
│   VBR    │     │fixed│ │ A20  │ │ cmd │     │ FS  │ │pmode │ │ cmd │
│ (8 KB)   │     │ LBA │ │      │ │     │     │parse│ │kernel│ │     │
└──────────┘     └─────┘ └──────┘ └─────┘     └─────┘ └──────┘ └─────┘
                  2 sec   2 sec   10 sec       2-4 sec  varies  varies
                  │                              │
                  │ Loads by fixed offset         │ Loads by filename
                  │ No clobber protection          │ FS-protected
```

The three-stage architecture established in v0.4.0 is designed to be the
permanent boot chain structure. Future milestones (protected mode, kernel,
filesystem) slot into this framework without restructuring the boot flow.

---

*Document created: 2026-05-11*
*Relates to: DESIGN.md §2.4–2.7, §9 Roadmap*
