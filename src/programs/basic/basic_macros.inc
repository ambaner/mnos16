; =============================================================================
; basic_macros.inc — convention-enforcing macros for BASIC asm
;
; These macros bake CF-discipline and dispatcher-flag preservation
; directly into source syntax so the v0.9.19.0-class bugs cannot be
; written by mistake.  Use them in any new routine; convert existing
; routines as you touch them.
;
; All macros are NASM 0.99-safe.  No side-effects beyond the documented
; ones.  All preserve `ax` unless they are explicitly setting it.
;
; -----------------------------------------------------------------------------
; BAS_RET_OK
;     Success exit for any CF-discipline routine.  Emits `clc` + `ret`.
;     Equivalent to:
;         clc
;         ret
;     Use instead of bare `ret` in routines annotated `;@returns cf`.
;     Catches v0.9.19.0 bug #1 class (CF leak through trailing cmp).
;
; BAS_RET_ERR code
;     Error exit for any CF-discipline routine.  Emits:
;         mov al, code
;         stc
;         ret
;     `code` is typically a BERR_* constant.
;     Use instead of the mov-al/stc/ret triple at error labels.
;
; BAS_RET_ERR_NOCODE
;     Error exit when AL was already loaded earlier (rare).  Emits:
;         stc
;         ret
;
; BAS_DISPATCH_BEGIN  / BAS_DISPATCH_END
;     Bracket a `cmp reg, op / je .handler` chain that must preserve
;     flags from a prior value comparison.  Emits `pushf` / `popf`
;     around the contained block.  Use at every site that resembles
;     `.be_cmp_emit` — bug #2 class.
;
;     Example:
;         cmp dx, cx                  ; value compare → sets flags
;         BAS_DISPATCH_BEGIN
;         cmp bl, '='
;         je .emit_eq
;         cmp bl, '<'
;         je .emit_lt
;         ...
;         BAS_DISPATCH_END            ; pops flags from value-compare
;         je .emit_default            ; consumes preserved flags
;
; -----------------------------------------------------------------------------
; Header-guard so accidental double-include is harmless.
; =============================================================================

%ifndef BASIC_MACROS_INC
%define BASIC_MACROS_INC 1

%macro BAS_RET_OK 0
    clc
    ret
%endmacro

%macro BAS_RET_ERR 1
    mov al, %1
    stc
    ret
%endmacro

%macro BAS_RET_ERR_NOCODE 0
    stc
    ret
%endmacro

%macro BAS_DISPATCH_BEGIN 0
    pushf
%endmacro

%macro BAS_DISPATCH_END 0
    popf
%endmacro

%endif  ; BASIC_MACROS_INC
