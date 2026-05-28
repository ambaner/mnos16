; =============================================================================
; SYSINFO DATA — String constants and runtime buffers
; =============================================================================

; --- Page headers ------------------------------------------------------------
msg_page1_hdr   db '--- Page 1: CPU Information ---', 13, 10, 0
msg_page2_hdr   db 13, 10, '--- Page 2: Memory ---', 13, 10, 0
msg_page3_hdr   db 13, 10, '--- Page 3: BIOS Data Area ---', 13, 10, 0
msg_page4_hdr   db 13, 10, '--- Page 4: Video & Disk ---', 13, 10, 0
msg_page5_hdr   db 13, 10, '--- Page 5: IVT (Interrupt Vector Table) ---', 13, 10, 0

; --- Memory strings ----------------------------------------------------------
msg_conv_mem    db '  Conv. memory: ', 0
msg_ext_mem     db '  Ext. memory:  ', 0
msg_kb          db ' KB', 13, 10, 0
msg_na          db 'N/A', 13, 10, 0
msg_e820_hdr    db '  E820 Memory Map:', 13, 10, 0
msg_e820_base   db '    Base=', 0
msg_e820_len    db ' Len=', 0
msg_e820_type   db ' Type=', 0

; E820 type names
msg_type_usable   db ' (Usable)', 13, 10, 0
msg_type_reserved db ' (Reserved)', 13, 10, 0
msg_type_acpi     db ' (ACPI)', 13, 10, 0
msg_type_nvs      db ' (ACPI NVS)', 13, 10, 0
msg_type_bad      db ' (Bad)', 13, 10, 0
msg_type_unknown  db ' (?)', 13, 10, 0

; --- BDA strings -------------------------------------------------------------
msg_com_hdr     db '  COM Ports:', 13, 10, 0
msg_lpt_hdr     db '  LPT Ports:', 13, 10, 0
msg_equip       db '  Equipment word: ', 0
msg_vid_mode_bda db '  Video mode (BDA): ', 0
msg_vid_cols    db '  Screen columns:   ', 0
msg_vid_pagesz  db '  Video page size:  ', 0
msg_bytes       db ' bytes', 13, 10, 0
msg_not_present db 'N/A', 13, 10, 0

; --- Video & Disk strings ----------------------------------------------------
msg_vid_active  db '  Video mode:    ', 0
msg_vid_page    db '  Display page:  ', 0
msg_vid_base    db '  Video mem base: ', 0
msg_b8000       db '0xB8000 (color)', 13, 10, 0
msg_b0000       db '0xB0000 (mono)', 13, 10, 0
msg_cursor      db '  Cursor pos:    ', 0
msg_cursor_rc   db ' (row,col)', 13, 10, 0
msg_boot_drv    db '  Boot drive:    ', 0
msg_drv_geom    db '  Drive geometry:', 13, 10, 0
msg_drv_cyl     db '    Cylinders: ', 0
msg_drv_sec     db '    Sectors:   ', 0
msg_drv_head    db '    Heads:     ', 0

; --- EDD strings -------------------------------------------------------------
msg_edd_hdr     db '  EDD Support:', 13, 10, 0
msg_edd_ver     db '    Version:       ', 0
msg_edd_ext_skip db '    (Extended params not available via syscall)', 13, 10, 0
msg_edd_none    db '    Not available', 13, 10, 0

; --- IVT strings -------------------------------------------------------------
ivt_names_table:
    dw msg_ivt_00, msg_ivt_01, msg_ivt_02, msg_ivt_03
    dw msg_ivt_04, msg_ivt_05, msg_ivt_06, msg_ivt_07

msg_ivt_00      db '  Divide/0', 13, 10, 0
msg_ivt_01      db '  Debug', 13, 10, 0
msg_ivt_02      db '  NMI', 13, 10, 0
msg_ivt_03      db '  Breakpoint', 13, 10, 0
msg_ivt_04      db '  Overflow', 13, 10, 0
msg_ivt_05      db '  BOUND/PrtSc', 13, 10, 0
msg_ivt_06      db '  Invalid Op', 13, 10, 0
msg_ivt_07      db '  No Coproc', 13, 10, 0

; --- CPU information strings --------------------------------------------------
msg_cpu_vendor  db '  Vendor:     ', 0
msg_cpu_family  db '  Family:     ', 0
msg_cpu_model   db '  Model:      ', 0
msg_cpu_step    db '  Stepping:   ', 0
msg_cpu_feat    db '  Features:   ', 0
msg_no_cpuid    db '  CPUID not supported (pre-486 CPU)', 13, 10, 0

; Feature flag tags
msg_f_fpu       db 'FPU ', 0
msg_f_tsc       db 'TSC ', 0
msg_f_msr       db 'MSR ', 0
msg_f_cx8       db 'CX8 ', 0
msg_f_pge       db 'PGE ', 0
msg_f_cmov      db 'CMOV ', 0
msg_f_mmx       db 'MMX ', 0
msg_f_sse       db 'SSE ', 0
msg_f_sse2      db 'SSE2 ', 0
msg_f_sse3      db 'SSE3 ', 0
msg_f_sse41     db 'SSE4.1 ', 0
msg_f_sse42     db 'SSE4.2 ', 0

; Hypervisor detection
msg_hv_yes      db '  Hypervisor: Yes', 13, 10, 0
msg_hv_no       db '  Hypervisor: No', 13, 10, 0
msg_hv_vendor   db '  HV Vendor:  ', 0

; --- Shared strings ----------------------------------------------------------
msg_crlf        db 13, 10, 0
msg_indent      db '    ', 0
msg_anykey      db 13, 10, '  Press any key...', 0

; --- Sysinfo done message ----------------------------------------------------
msg_sysinfo_done db 13, 10, '  System info complete.', 13, 10, 0

; =============================================================================
; RUNTIME DATA (BSS)
; =============================================================================
e820_buf        times 20 db 0       ; E820 buffer (one entry)
bda_offset      dw 0                ; Temp storage for BDA offset in loops
ivt_index       db 0                ; IVT loop counter

; CPUID scratch space
cpuid_vendor    times 13 db 0       ; 12-byte vendor string + NUL
cpuid_ver       dd 0
cpuid_feat_edx  dd 0
cpuid_feat_ecx  dd 0
