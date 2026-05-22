; =============================================================================
; kernel_stack.inc — Stack canary (overflow detection)
;
; PURPOSE:
;   Detects stack overflow by placing a known sentinel value ("canary") at the
;   bottom of the stack zone (0x7400).  If any code grows the stack past this
;   point, the canary is overwritten and the next check catches it.
;
;   Without this, stack overflow silently corrupts kernel code/data at 0x5800,
;   producing extremely difficult-to-diagnose bugs (wrong syscall behavior,
;   random crashes, data corruption).
;
; HOW IT WORKS:
;   1. canary_init  — called once at boot, writes 0xDEAD 0xDEAD at 0x7400.
;   2. canary_check — called on every syscall entry/exit, verifies both words
;                     are intact.  If either is corrupted → fatal halt.
;
; MEMORY LAYOUT:
;   0x7400  ┌──────────────┐
;           │ 0xDEAD       │  ← canary word 1 (first to be overwritten)
;   0x7402  ├──────────────┤
;           │ 0xDEAD       │  ← canary word 2 (redundancy)
;   0x7404  ├──────────────┤
;           │              │
;           │  Stack zone  │  ← SP grows downward from 0x7BFF
;           │  (~2044 B)   │
;           │              │
;   0x7C00  └──────────────┘  ← Initial SP (set by MBR)
;
; ADDRESSING:
;   All canary reads/writes use the SS: segment override.  In MNOS16,
;   SS is always 0x0000 (set by MBR, never changed), so SS:0x7400 is
;   linear address 0x7400.  Using SS: makes the intent explicit and avoids
;   depending on DS (which may be changed by some components).
;
; REGISTER PRESERVATION:
;   canary_check preserves ALL registers and FLAGS.  This is critical
;   because it runs in the syscall hot path where AH contains the function
;   number, other registers carry arguments, and CF may carry return status.
;
; DEBUG-ONLY:
;   Both routines are wrapped in %ifdef DEBUG.  In release builds, the
;   CANARY_INIT and CANARY_CHECK macros expand to nothing (0 bytes).
;   The canary address at 0x7400 is never written or read in release.
;
; PREREQUISITES:
;   - memory.inc must be %included (provides STACK_CANARY_ADDR/VALUE)
;   - serial.inc must be %included (provides serial_puts, serial_crlf)
;   - debug.inc must be %included (provides DBG, DBG_REGS)
;   - For screen output: a `puts` subroutine must exist in the same binary
;
; See doc/DEBUGGING.md §9 for full specification.
; See doc/MEMORY-LAYOUT.md §3 for stack layout details.
; =============================================================================

%ifdef DEBUG

; =============================================================================
; canary_init — Plant the stack canary at the bottom of the stack zone
;
; Writes two copies of STACK_CANARY_VALUE (0xDEAD) at STACK_CANARY_ADDR
; (0x7400).  Two words provide redundancy — if only one is corrupted, we
; still detect it.  The canary is the very first thing the stack would
; overwrite when it grows too deep.
;
; Call this ONCE during kernel initialization, after fault handlers are
; installed but before loading any binaries that use significant stack.
;
; Clobbers: none (all registers preserved)
; =============================================================================
canary_init:
    ; Write the sentinel value to the stack floor.
    ; SS: override ensures we target the stack segment regardless of DS.
    mov word [ss:STACK_CANARY_ADDR],     STACK_CANARY_VALUE
    mov word [ss:STACK_CANARY_ADDR + 2], STACK_CANARY_VALUE

    ; Log to serial so the developer knows the canary is active
    DBG "KERNEL: stack canary planted at 0x7400 (0xDEAD 0xDEAD)"
    ret

; =============================================================================
; canary_check — Verify the stack canary is still intact
;
; Compares both canary words against the expected value.  If either word
; has been overwritten, the stack has overflowed — we print a fatal message
; to both serial and screen, dump all registers, and halt permanently.
;
; This routine is called:
;   - At syscall entry (before dispatch) — catches overflow from user code
;   - At syscall exit (before iret/retf) — catches overflow from handlers
;
; REGISTER PRESERVATION:
;   On success (canary intact): ALL registers and FLAGS are preserved.
;   This is essential because:
;     - AH holds the syscall function number at entry
;     - Other registers hold syscall arguments
;     - CF holds return status at exit (used by syscall_ret_cf)
;   We use pushf/popf to save/restore FLAGS, and only clobber registers
;   on the fatal path (which never returns).
;
; On failure (canary dead): does not return — halts the CPU.
;
; Clobbers on success: none
; Clobbers on failure: n/a (halts)
; =============================================================================
canary_check:
    ; Save FLAGS so our cmp doesn't destroy the caller's CF/ZF/etc.
    pushf

    ; Check canary word 1 (0x7400) — this is the first byte the stack
    ; would overwrite when growing past the safe zone.
    cmp word [ss:STACK_CANARY_ADDR], STACK_CANARY_VALUE
    jne .canary_dead

    ; Check canary word 2 (0x7402) — redundancy: catches partial
    ; overwrites that only corrupt 2 of the 4 canary bytes.
    cmp word [ss:STACK_CANARY_ADDR + 2], STACK_CANARY_VALUE
    jne .canary_dead

    ; Canary intact — restore FLAGS and return normally
    popf
    ret

; =============================================================================
; .canary_dead — Fatal handler: stack overflow detected
;
; At this point the stack has grown past 0x7404 and corrupted the canary.
; The stack itself may be partially corrupted, so we keep our actions
; minimal and avoid deep call chains.
;
; Actions:
;   1. Log to serial (always works — direct port I/O, no stack needed)
;   2. Print to screen (BIOS INT 10h uses minimal stack internally)
;   3. Dump registers to serial for post-mortem analysis
;   4. Halt permanently (cli + hlt in a loop)
;
; We do NOT attempt recovery — stack overflow means memory corruption
; has already occurred.  The only safe action is to stop and diagnose.
; =============================================================================
.canary_dead:
    ; Discard the saved FLAGS from pushf above — we're never returning
    popf

    ; --- Serial output (most reliable — direct port I/O) ---
    push si
    mov si, .canary_msg
    call serial_puts                ; "[CANARY] STACK OVERFLOW ..."
    pop si

    ; Dump all registers to serial for post-mortem analysis.
    ; These values show what was happening when the overflow was detected.
    DBG_REGS

    ; --- Screen output (visible to user in VM console) ---
    push si
    mov si, .canary_msg
    call puts                       ; Also show on screen
    pop si

    ; --- Permanent halt ---
    ; cli prevents any interrupt from resuming execution.
    ; The jmp loop handles the (theoretical) case where an NMI wakes
    ; the CPU from HLT — we just halt again.
    cli
.canary_halt:
    hlt
    jmp .canary_halt

; --- Fatal message string ---
; Includes the address that was corrupted so the developer knows exactly
; where to look in a hex dump.
.canary_msg:
    db 13, 10
    db '*** STACK OVERFLOW: canary at 0x7400 destroyed! ***', 13, 10
    db '    Stack grew past safe limit (0x7404).', 13, 10
    db '    Kernel/data below 0x7400 may be corrupted.', 13, 10, 0

%endif ; DEBUG

; NOTE: CANARY_INIT / CANARY_CHECK macros are defined in debug.inc (included
; early) so they are available before first use in kernel.asm boot sequence.
