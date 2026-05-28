; =============================================================================
; memory.inc — Memory layout constants
;
; Defines the segment:offset load addresses for each boot-chain component.
; All components use segment 0x0000 (flat real-mode addressing).
;
; Memory map (low → high):
;   0x0600  Boot Info Block (BIB)   — 7 bytes, shared boot data
;   0x0800  MODULE_FIRST_BASE       — FS.SYS, then MM.SYS, then SHELL.SYS
;           (modules packed sequentially, positions determined at load time)
;   0x5000  KERNEL.SYS              — up to 7 KB (14 sectors)
;   0x7C00  VBR (boot-time only)    — 1 KB (2 sectors)
;
; System modules (FS/MM/SHELL) are relocatable — assembled with ORG 0 and
; patched by the kernel at load time.  Their positions are NOT hardcoded.
;
; See doc/MEMORY-LAYOUT.md for the complete memory map.
; =============================================================================

LOADER_SEG   equ 0x0000             ; Segment for LOADER.SYS
LOADER_OFF   equ 0x0800             ; Offset for LOADER.SYS

KERNEL_SEG   equ 0x0000             ; Segment for KERNEL.SYS
KERNEL_OFF   equ 0x5000             ; Offset for KERNEL.SYS

; --- Dynamic module placement -------------------------------------------------
; System modules are loaded sequentially starting at MODULE_FIRST_BASE.
; The kernel tracks next_base as it loads each module.
; SHELL_SEG/SHELL_OFF/MM_SEG/MM_OFF are NO LONGER FIXED — they are computed
; by the kernel at boot time based on actual module sizes.
MODULE_FIRST_BASE equ 0x0800         ; First module loads here (FS.SYS)
MODULE_AREA_END   equ 0x5000         ; Must not overlap kernel

; Scratch buffer for directory reads during module loading.
; Placed just below the kernel, safe from module packing (modules grow upward
; from 0x0800 and are validated to not exceed MODULE_AREA_END).
DIR_SCRATCH_BUF   equ 0x4E00         ; 512 bytes at 0x4E00–0x4FFF

; =============================================================================
; Stack canary constants
;
; The stack grows downward from 0x7C00.  The safe stack floor is 0x6C00,
; giving ~4 KB of stack space.  We place a 4-byte sentinel ("canary") at
; the very bottom of the stack zone.  If the stack ever grows past 0x6C04
; and overwrites the canary, we know a stack overflow occurred.
;
; Layout:
;   0x6C00–0x6C03  Canary (4 bytes: two copies of 0xDEAD)
;   0x6C04–0x7BFF  Usable stack (~4092 bytes)
;   0x7C00         Initial SP (set by MBR, never moved)
;
; The canary value 0xDEAD was chosen because it is visually distinctive
; in hex dumps and unlikely to appear as a legitimate stack value.
; =============================================================================
STACK_CANARY_ADDR  equ 0x6C00        ; Linear address of canary (stack floor)
STACK_CANARY_VALUE equ 0xDEAD        ; Sentinel value (written as two words)
STACK_CANARY_SIZE  equ 4             ; Total canary size in bytes

; =============================================================================
; High Memory Area (HMA) constants
;
; The HMA is the 65,520 bytes above 1 MB accessible from real mode when
; A20 is enabled.  Segment 0xFFFF with offsets 0x0010–0xFFFF maps to
; physical addresses 0x100000–0x10FFEF.
;
; MNOS16 uses the HMA as the primary heap region (~64 KB), replacing the
; conventional 4 KB heap.  The MM.SYS code stays in conventional memory;
; only the heap data lives in HMA.
;
; Callers receive AX=0xFFFF, BX=offset from MEM_ALLOC and must access
; allocated memory via ES:BX (mov es, ax; mov [es:bx], value).
;
; If A20 fails (extremely rare), MM falls back to the conventional heap.
; =============================================================================
HMA_SEG         equ 0xFFFF           ; Segment register value for HMA
HMA_HEAP_START  equ 0x0010           ; First usable offset (physical 0x100000)
HMA_HEAP_END    equ 0xFF00           ; Exclusive end (256-byte guard at top)
HMA_HEAP_SIZE   equ HMA_HEAP_END - HMA_HEAP_START  ; 65,264 bytes (~63.7 KB)

; =============================================================================
; Memory Manager (MNMM) constants
;
; MM.SYS is loaded dynamically by the kernel (immediately after FS.SYS).
; It provides heap allocation via INT 0x82.
;
; The heap resides in the HMA (~64 KB above 1 MB, accessible via A20).
; The conventional heap at 0x8000 is kept only for test harness compatibility.
;
; See doc/MEMORY-MANAGER.md for the complete specification.
; =============================================================================

HEAP_START      equ 0x8000           ; Legacy conventional heap start (test harness only)
HEAP_END        equ 0x9000           ; Legacy conventional heap end (test harness only)
HEAP_SIZE       equ HEAP_END - HEAP_START  ; 4,096 bytes (4 KB)

; =============================================================================
; Transient Program Area (TPA) — user program loading zone
;
; User executables (.MNX files) are loaded into the TPA by the shell's `run`
; command.  The TPA occupies the region above the shell ABI area:
;   0x8000–0xF7FF (30 KB max program size)
;
; This was expanded from 26 KB (starting at 0x9000) when the heap moved to
; the HMA.  The old 4 KB conventional heap at 0x8000 is no longer needed.
;
; Programs are assembled with [ORG 0x8000] and execute in flat segment 0.
; The TPA is not persistent — it is overwritten on each `run` invocation.
;
; See doc/PROGRAM-LOADER.md for the complete specification.
; =============================================================================
USER_PROG_BASE  equ 0x8000           ; Linear address: program load point
USER_PROG_END   equ 0xF7FF           ; Last usable byte in TPA
USER_PROG_MAX   equ 0x7800           ; Maximum program size (30 KB)
USER_PROG_MAX_SEC equ 60             ; Maximum program size in sectors (30KB/512)

; =============================================================================
; Shell/Kernel shared state — Program loader ABI
;
; These fixed addresses are used to communicate between shell and kernel for
; the SYS_EXIT and SYS_GET_ARGS syscalls.  They reside in the gap between
; the VBR area (boot-only) and the heap start:
;   0x7C00–0x7FFF  — originally VBR, free after boot
;
; Layout:
;   0x7FFC  SHELL_ARGS_PTR  (2 bytes) — pointer to argument string
;   0x7FFE  SHELL_SAVED_SP  (2 bytes) — shell's SP for SYS_EXIT return
; =============================================================================
SHELL_SAVED_SP  equ 0x7FFE           ; Shell stores SP-2 here before call
SHELL_ARGS_PTR  equ 0x7FFC           ; Shell stores args string pointer here

; --- Parsed argument table (Layer 2: argc/argv) -------------------------------
; The shell parses the argument string into individual tokens and stores them
; here for programs to access via SYS_GET_ARGC / SYS_GET_ARGV.
;
; Layout (0x7F00–0x7FFB):
;   0x7F00: argc (1 byte) — number of arguments (0–15)
;   0x7F01: reserved (1 byte)
;   0x7F02: argv[0] pointer (2 bytes)
;   0x7F04: argv[1] pointer (2 bytes)
;   ...
;   0x7F20: argv[15] pointer (2 bytes)
;   0x7F22–0x7FFB: argument string storage (NUL-separated tokens)
;
; See doc/COMMAND-LINE.md for the full specification.
ARGV_TABLE      equ 0x7F00           ; Start of argv region
ARGV_ARGC       equ 0x7F00           ; 1 byte: argument count
ARGV_PTRS       equ 0x7F02           ; Array of 16 word pointers
ARGV_STORAGE    equ 0x7F22           ; NUL-separated arg strings
ARGV_STORAGE_END equ 0x7FFB          ; Last usable byte for arg storage
ARGV_MAX_ARGS   equ 15               ; Maximum number of parsed arguments

; --- MCB (Memory Control Block) header layout --------------------------------
; Each heap block starts with a 4-byte header:
;   Offset 0: size    (2 bytes) — total block size INCLUDING header
;   Offset 2: flags   (1 byte)  — bit 0: 1=allocated, 0=free
;                                  bits 1-3: owner ID (0-7)
;   Offset 3: magic   (1 byte)  — 'M' (0x4D) for integrity checking
MCB_SIZE_OFF    equ 0                ; Offset of size field in MCB
MCB_FLAGS_OFF   equ 2                ; Offset of flags field in MCB
MCB_MAGIC_OFF   equ 3                ; Offset of magic field in MCB
MCB_HDR_SIZE    equ 4                ; Header size in bytes
MCB_MAGIC       equ 0x4D             ; 'M' — block magic byte
MCB_FLAG_USED   equ 0x01             ; Flag bit: block is allocated
MCB_OWNER_MASK  equ 0x0E             ; Mask for owner bits (bits 1-3)
MCB_OWNER_SHIFT equ 1                ; Shift count to extract owner ID
MCB_MIN_BLOCK   equ 8                ; Minimum block: 4 header + 4 payload

; --- Owner IDs (bits 1-3 of flags byte) --------------------------------------
MCB_OWNER_NONE  equ 0                ; Free block / unknown
MCB_OWNER_KERN  equ 1                ; Kernel (INT 0x80)
MCB_OWNER_FS    equ 2                ; Filesystem (INT 0x81)
MCB_OWNER_MM    equ 3                ; Memory manager internal
MCB_OWNER_SHELL equ 4                ; Shell / user process
MCB_OWNER_USR1  equ 5                ; Reserved for future use
MCB_OWNER_USR2  equ 6                ; Reserved for future use
MCB_OWNER_USR3  equ 7                ; Reserved for future use

; --- INT 0x82 function numbers -----------------------------------------------
; AH = function number before INT 0x82.
;
; MEM_ALLOC: CX = requested size in bytes, DL = owner ID (0-7)
;            Returns: BX = pointer to usable memory (past MCB header)
;            CF set on failure (out of memory or invalid size)
;
; MEM_FREE:  BX = pointer previously returned by MEM_ALLOC
;            CF set on failure (invalid pointer, double free, corruption)
;
; MEM_AVAIL: Returns: AX = largest contiguous free block (usable bytes)
;                     DX = total free bytes (usable)
;
; MEM_INFO:  Returns: AX = total heap size, BX = bytes used,
;                     CX = bytes free, DX = total block count
MEM_ALLOC       equ 0x01             ; Allocate memory
MEM_FREE        equ 0x02             ; Free memory
MEM_AVAIL       equ 0x03             ; Query available memory
MEM_INFO        equ 0x04             ; Query heap statistics
MEM_QUERY       equ 0x05             ; Query heap location (seg/start/size)

MEM_SYSCALL_MAX equ 0x05             ; Highest valid MM syscall number
