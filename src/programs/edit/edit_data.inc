; =============================================================================
; DATA SECTION
; =============================================================================

; --- Editor state ---
gap_start       dw GAP_BUF_START    ; Points to first byte of gap
gap_end         dw GAP_BUF_END + 1  ; Points past last byte of gap
cursor_row      dw 0                ; Logical line (0-based)
cursor_col      dw 0                ; Column (0-based)
view_top        dw 0                ; First visible line
total_lines     dw 1                ; Total line count
modified        db 0                ; 1 if buffer changed since last save
insert_mode     db 1                ; 1=insert, 0=overwrite
sel_active      db 0                ; 1 if selection is active
sel_start       dw 0                ; Selection start offset
sel_end         dw 0                ; Selection end offset
clipboard_len   dw 0                ; Bytes in clipboard
search_len      db 0                ; Search string length
replace_len     db 0                ; Replace string length
menu_open       db 0                ; 0=closed, 1=File, 2=Edit, 3=Search
menu_sel        db 0                ; Currently highlighted menu item

filename        times 12 db 0       ; Current filename (8.3 + NUL)
filename_len    db 0                ; Filename length
_prev_filename  times 12 db 0       ; Previous filename for "already open" check

_num_buf        times 6 db 0        ; Temp buffer for line number input

; --- UI Strings ---
str_menu_file   db 'File', 0
str_menu_edit   db 'Edit', 0
str_menu_search db 'Search', 0

str_untitled    db 'Untitled', 0
str_ln          db 'Ln:', 0
str_col         db 'Col:', 0
str_modified    db '[Modified]', 0
str_ins         db 'INS', 0
str_ovr         db 'OVR', 0

str_prompt_filename db 'Filename: ', 0
str_prompt_search   db 'Find: ', 0
str_prompt_replace  db 'Replace with: ', 0
str_prompt_goto     db 'Go to line: ', 0
str_save_prompt     db 'Save changes? (Y/N/Esc) ', 0

str_msg_saved       db 'File saved.', 0
str_msg_save_err    db 'Save failed!', 0
str_msg_load_err    db 'File not found.', 0
str_msg_not_found   db 'Not found.', 0
str_msg_already_open db 'File already open.', 0
str_msg_overwrite   db 'Overwrite? (Y/N)', 0

; --- Menu item strings (NUL-terminated, sequential) ---
menu_file_items:
    db 'New         Ctrl+N', 0
    db 'Open        Ctrl+O', 0
    db 'Save        Ctrl+S', 0
    db 'Save As...       ', 0
    db 'Exit        Alt+X', 0

menu_edit_items:
    db 'Cut         Ctrl+X', 0
    db 'Copy        Ctrl+C', 0
    db 'Paste       Ctrl+V', 0
    db 'Select All  Ctrl+A', 0

menu_search_items:
    db 'Find        Ctrl+F', 0
    db 'Find Next   F3    ', 0
    db 'Replace     Ctrl+H', 0
    db 'Replace All F4    ', 0
    db 'Go to Line  Ctrl+G', 0

; --- Help text lines (16 lines) ---
help_lines:
    dw .h0, .h1, .h2, .h3, .h4, .h5, .h6, .h7
    dw .h8, .h9, .h10, .h11, .h12, .h13, .h14, .h15

.h0  db '       EDIT.MNX - Text Editor Help        ', 0
.h1  db '                                          ', 0
.h2  db ' Navigation:                              ', 0
.h3  db '   Arrows       Move cursor               ', 0
.h4  db '   Home/End     Start/End of line         ', 0
.h5  db '   PgUp/PgDn    Scroll page               ', 0
.h6  db '   Ctrl+Home    Start of file             ', 0
.h7  db '   Ctrl+End     End of file               ', 0
.h8  db '                                          ', 0
.h9  db ' Editing:                                 ', 0
.h10 db '   Ctrl+X/C/V   Cut/Copy/Paste            ', 0
.h11 db '   Ctrl+S       Save file                 ', 0
.h12 db '   Ctrl+O       Open file                 ', 0
.h13 db '   Ctrl+F       Find, Ctrl+H Replace      ', 0
.h14 db '   Alt+X        Exit editor               ', 0
.h15 db '   F1           This help                 ', 0
