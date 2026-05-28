; stub_spawn_state.asm — Test harness for SYS_SPAWN state management
;
; Exercises the spawn depth tracking, parent stack push/pop, trampoline
; install/removal, and rollback logic in isolation from filesystem ops.
;
; Assembled as flat binary: nasm -f bin -I src/include/ -I src/kernel/ -o tests/bin/stub_spawn_state.bin
;
; Test points (called via entry dispatch at various offsets):
;   spawn_push_one   — push one parent, verify depth=1
;   spawn_push_max   — push to SPAWN_MAX_DEPTH, verify all stored
;   spawn_push_over  — push beyond max → should set CF
;   spawn_rollback   — push + rollback → depth back to 0
;   trampoline_install — verify trampoline is written at [SHELL_SAVED_SP]
;   trampoline_skip  — second push should NOT overwrite spawn_saved_ret
;   rollback_restores_trampoline — rollback restores original ret addr

[bits 16]
[org 0x1000]

%include "memory.inc"
%include "mnfs.inc"
%include "syscalls.inc"

; --- Entry dispatch table ---
; The test harness sets AH = test number, then calls entry.
; Each test exits via HLT.
entry:
    cmp ah, 0x01
    je test_push_one
    cmp ah, 0x02
    je test_push_max
    cmp ah, 0x03
    je test_push_overflow
    cmp ah, 0x04
    je test_rollback_outermost
    cmp ah, 0x05
    je test_trampoline_install
    cmp ah, 0x06
    je test_trampoline_skip_nested
    cmp ah, 0x07
    je test_rollback_restores_ret
    cmp ah, 0x08
    je test_rollback_nested
    hlt                             ; Unknown test → halt

; ═══════════════════════════════════════════════════════════════════════════════
; Test 0x01: Push one parent filename, verify depth = 1
; Input: DS:BX = pointer to 11-byte filename
; Output: spawn_depth = 1, spawn_parent_stack[0] = filename
; ═══════════════════════════════════════════════════════════════════════════════
test_push_one:
    call spawn_push
    hlt

; ═══════════════════════════════════════════════════════════════════════════════
; Test 0x02: Push SPAWN_MAX_DEPTH entries
; Input: DS:BX = pointer to 11-byte filename (same for all pushes)
; Output: spawn_depth = SPAWN_MAX_DEPTH
; ═══════════════════════════════════════════════════════════════════════════════
test_push_max:
    mov cx, SPAWN_MAX_DEPTH
.push_loop:
    call spawn_push
    loop .push_loop
    hlt

; ═══════════════════════════════════════════════════════════════════════════════
; Test 0x03: Push beyond max → CF set
; Input: DS:BX = filename, spawn_depth pre-set to SPAWN_MAX_DEPTH
; Output: CF set, spawn_depth unchanged
; ═══════════════════════════════════════════════════════════════════════════════
test_push_overflow:
    ; Pre-set depth to max
    mov byte [spawn_depth], SPAWN_MAX_DEPTH
    call spawn_push_checked
    hlt

; ═══════════════════════════════════════════════════════════════════════════════
; Test 0x04: Push one then rollback → depth = 0, spawn_saved_ret cleared
; Input: DS:BX = filename
; Setup: SHELL_SAVED_SP points to a fake return addr
; ═══════════════════════════════════════════════════════════════════════════════
test_rollback_outermost:
    call spawn_push
    call install_trampoline_if_outermost
    mov byte [spawn_pending], 1
    call spawn_rollback_if_pending
    hlt

; ═══════════════════════════════════════════════════════════════════════════════
; Test 0x05: Trampoline install — verify [SHELL_SAVED_SP] gets trampoline addr
; Input: DS:BX = filename
; Setup: [SHELL_SAVED_SP] has a stack addr, that addr has 0xBEEF (fake ret)
; ═══════════════════════════════════════════════════════════════════════════════
test_trampoline_install:
    call spawn_push
    call install_trampoline_if_outermost
    hlt

; ═══════════════════════════════════════════════════════════════════════════════
; Test 0x06: Second push does NOT overwrite spawn_saved_ret
; Input: DS:BX = filename
; Setup: spawn_saved_ret already non-zero
; ═══════════════════════════════════════════════════════════════════════════════
test_trampoline_skip_nested:
    ; First push + trampoline install
    call spawn_push
    call install_trampoline_if_outermost
    ; spawn_saved_ret is now set — second push should skip trampoline
    call spawn_push
    call install_trampoline_if_outermost
    hlt

; ═══════════════════════════════════════════════════════════════════════════════
; Test 0x07: Rollback restores original ret addr at [SHELL_SAVED_SP]
; Input: DS:BX = filename
; Setup: [SHELL_SAVED_SP] → stack_slot, stack_slot = 0xBEEF (original ret)
; ═══════════════════════════════════════════════════════════════════════════════
test_rollback_restores_ret:
    call spawn_push
    call install_trampoline_if_outermost
    ; Now [stack_slot] should have trampoline addr, spawn_saved_ret=0xBEEF
    mov byte [spawn_pending], 1
    call spawn_rollback_if_pending
    ; After rollback, [stack_slot] should be 0xBEEF again
    hlt

; ═══════════════════════════════════════════════════════════════════════════════
; Test 0x08: Rollback at nested level (depth > 1 after rollback) — trampoline kept
; Input: DS:BX = filename
; Setup: Push twice, then rollback once → depth = 1, trampoline still active
; ═══════════════════════════════════════════════════════════════════════════════
test_rollback_nested:
    call spawn_push
    call install_trampoline_if_outermost
    call spawn_push
    ; Now depth=2. Rollback the second push.
    mov byte [spawn_pending], 1
    call spawn_rollback_if_pending
    ; depth should be 1, spawn_saved_ret still non-zero, trampoline intact
    hlt

; ═══════════════════════════════════════════════════════════════════════════════
; SPAWN STATE ROUTINES (extracted from kernel_syscall.inc for testability)
; ═══════════════════════════════════════════════════════════════════════════════

; --- spawn_push: push filename at DS:BX onto parent stack, inc depth ---
; Does NOT check overflow (caller must check first or use spawn_push_checked)
spawn_push:
    push si
    push di
    push cx
    push ax

    movzx ax, byte [spawn_depth]
    mov cx, 11
    mul cx                          ; AX = spawn_depth * 11
    lea di, [spawn_parent_stack]
    add di, ax

    mov si, bx
    mov cx, 11
    rep movsb

    inc byte [spawn_depth]

    pop ax
    pop cx
    pop di
    pop si
    ret

; --- spawn_push_checked: check depth limit, set CF on overflow ---
spawn_push_checked:
    cmp byte [spawn_depth], SPAWN_MAX_DEPTH
    jb .push_ok
    stc
    ret
.push_ok:
    call spawn_push
    clc
    ret

; --- install_trampoline_if_outermost: install trampoline only if first spawn ---
install_trampoline_if_outermost:
    cmp word [spawn_saved_ret], 0
    jne .skip_trampoline
    push bx
    push ax
    mov bx, [SHELL_SAVED_SP]        ; BX = address of stack slot
    mov ax, [bx]                    ; AX = original return address
    mov [spawn_saved_ret], ax       ; Save it
    mov word [bx], spawn_trampoline ; Install trampoline
    pop ax
    pop bx
.skip_trampoline:
    ret

; --- spawn_trampoline: target installed on stack for child's `ret` ---
spawn_trampoline:
    hlt                             ; In real kernel this is `mov ah, SYS_EXIT; int 0x80`

; --- spawn_rollback_if_pending: undo spawn state on error ---
spawn_rollback_if_pending:
    cmp byte [spawn_pending], 0
    je .rb_done
    mov byte [spawn_pending], 0

    dec byte [spawn_depth]

    cmp byte [spawn_depth], 0
    jne .rb_done

    ; Outermost: restore original return address
    cmp word [spawn_saved_ret], 0
    je .rb_done
    push bx
    push ax
    mov bx, [SHELL_SAVED_SP]
    mov ax, [spawn_saved_ret]
    mov [bx], ax                    ; Restore real ret addr
    mov word [spawn_saved_ret], 0
    pop ax
    pop bx

.rb_done:
    ret

; ═══════════════════════════════════════════════════════════════════════════════
; DATA (matches kernel_data.inc layout)
; ═══════════════════════════════════════════════════════════════════════════════
SPAWN_MAX_DEPTH equ 4
spawn_depth:        db 0
spawn_parent_stack: times (SPAWN_MAX_DEPTH * 11) db 0
spawn_saved_ret:    dw 0
spawn_pending:      db 0
