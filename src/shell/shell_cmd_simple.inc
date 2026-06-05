; =============================================================================
; COMMAND: cls
; Clear the screen and re-display the banner.
; =============================================================================
cmd_cls:
    jmp shell_init                  ; Re-init clears screen + prints banner

; =============================================================================
; COMMAND: reboot
; Perform a warm reboot via kernel syscall.
; No direct memory writes — the kernel handles the reset vector.
; =============================================================================
cmd_reboot:
    call mn_reboot                  ; Does not return

; =============================================================================
; COMMAND: help
; Print a list of available commands with brief descriptions.
; =============================================================================
cmd_help:
    mov si, msg_help_text
    call mn_print_string
    jmp shell_prompt

; =============================================================================
; COMMAND: ver
; Print version and build information, including boot mode tag.
; =============================================================================
cmd_ver:
    mov si, msg_ver_line1
    call mn_print_string
    call print_boot_tag
    mov si, msg_ver_rest
    call mn_print_string
    jmp shell_prompt
