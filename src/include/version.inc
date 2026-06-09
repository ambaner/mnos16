; =============================================================================
; version.inc — Single source of truth for OS version
;
; All version references (kernel syscall, shell banner, ver command) should
; use these constants.  To bump the version, edit ONLY this file.
; =============================================================================

; Numeric version (returned by SYS_GET_VERSION: AH=major, AL=minor)
OS_VERSION_MAJOR  equ 0x09            ; v0.9
OS_VERSION_MINOR  equ 0x13            ; .19 (hex 0x13 == decimal 19)
OS_VERSION        equ (OS_VERSION_MAJOR << 8) | OS_VERSION_MINOR

; String version — use in db directives: db '  MNOS ', OS_VERSION_STR, ' ', 0
; Trailing `.0` is the patch field (currently unused by SYS_GET_VERSION).
%define OS_VERSION_STR 'v0.9.19.0'
