; =============================================================================
; EDIT.MNX — Full-screen Text Editor for MNOS16
;
; DOS EDIT.COM-style text editor with:
;   - Menu bar (File / Edit / Search)
;   - Gap buffer for efficient editing
;   - Block selection with cut/copy/paste
;   - Find and Replace
;   - File load/save via INT 0x81
;   - Monochrome display (white on black, inverse for UI)
;
; Screen layout (80x25):
;   Row  0:    Menu bar (inverse video)
;   Rows 1-23: Edit area (23 lines, normal video)
;   Row 24:    Status bar (inverse video)
;
; Loaded into TPA at USER_PROG_BASE (0x8000). Max ~30 KB total.
;
; Build: nasm -f bin -I src/include/ -I src/programs/edit/ -o build/boot/edit.mnx src/programs/edit.asm
; Run:   mnos:\> run EDIT.MNX [filename]
;        mnos:\> edit [filename]
;
; Key bindings:
;   Arrows/Home/End/PgUp/PgDn  — navigation
;   Ctrl+S — Save, Ctrl+O — Open, Ctrl+N — New
;   Ctrl+X/C/V — Cut/Copy/Paste
;   Ctrl+F — Find, Ctrl+H — Replace, Ctrl+G — Go to line
;   Alt+F/E/S — Menu access, Alt+X — Exit
;   F1 — Help, Esc — Close menu/dialog
; =============================================================================

%include "syscalls.inc"
%include "memory.inc"
%include "mnfs.inc"

; %define EDIT_DEBUG 1               ; Uncomment for serial debug traces

[BITS 16]
[ORG USER_PROG_BASE]

; =============================================================================
; MNEX HEADER (6 bytes)
; =============================================================================
            db 'MNEX'               ; Magic — user-mode executable
edit_sectors dw 13                  ; Size in sectors (13 = 6656 bytes)

; =============================================================================
; CONSTANTS
; =============================================================================

; Screen dimensions
SCREEN_COLS     equ 80
SCREEN_ROWS     equ 25
EDIT_TOP_ROW    equ 1               ; First edit row
EDIT_BOT_ROW    equ 23              ; Last edit row
EDIT_LINES      equ 23              ; Visible edit lines (rows 1-23)
STATUS_ROW      equ 24              ; Status bar row
MENU_ROW        equ 0               ; Menu bar row

; VGA text mode — DOS EDIT-style color scheme
VGA_SEG         equ 0xB800          ; VGA text buffer segment
VGA_ATTR_NORM   equ 0x1F            ; Bright white on blue (edit area)
VGA_ATTR_INV    equ 0x70            ; Black on light gray (menu bar, status bar)
VGA_ATTR_SEL    equ 0x3F            ; Bright white on cyan (text selection)
VGA_ATTR_DIM    equ 0x19            ; Blue on blue (line overflow marker)
VGA_ATTR_MENU_HI equ 0x0F           ; Bright white on black (highlighted menu item)
VGA_ATTR_HOTKEY equ 0x74            ; Red on light gray (menu accelerator key)

; Gap buffer layout (within TPA)
; Editor code occupies ~0x8000-0xA7FF (10 KB)
; Clipboard at 0xA800-0xA9FF (512 bytes)
; Search/replace at 0xAA00-0xABFF (512 bytes)
; Gap buffer at 0xAC00-0xF7FF (~19.5 KB)
CLIPBOARD_ADDR  equ 0xA800
CLIPBOARD_SIZE  equ 512
SEARCH_BUF_ADDR equ 0xAA00
REPLACE_BUF_ADDR equ 0xAB00
SEARCH_BUF_SIZE equ 128
GAP_BUF_START   equ 0xAC00
GAP_BUF_END     equ 0xF7FF          ; Last usable byte (before stack area)
GAP_BUF_CAPA    equ GAP_BUF_END - GAP_BUF_START + 1  ; ~19.5 KB

; Tab settings
TAB_WIDTH       equ 8

; Key scancodes (AL=0 for extended keys, scancode in AH)
KEY_UP          equ 0x48
KEY_DOWN        equ 0x50
KEY_LEFT        equ 0x4B
KEY_RIGHT       equ 0x4D
KEY_HOME        equ 0x47
KEY_END         equ 0x4F
KEY_PGUP        equ 0x49
KEY_PGDN        equ 0x51
KEY_INSERT      equ 0x52
KEY_DELETE      equ 0x53
KEY_F1          equ 0x3B
KEY_F3          equ 0x3D
KEY_F4          equ 0x3E

; Ctrl key combos (ASCII values)
CTRL_S          equ 0x13            ; Ctrl+S = Save
CTRL_O          equ 0x0F            ; Ctrl+O = Open
CTRL_N          equ 0x0E            ; Ctrl+N = New
CTRL_X          equ 0x18            ; Ctrl+X = Cut
CTRL_C          equ 0x03            ; Ctrl+C = Copy
CTRL_V          equ 0x16            ; Ctrl+V = Paste
CTRL_F          equ 0x06            ; Ctrl+F = Find
CTRL_H          equ 0x08            ; Ctrl+H = Replace
CTRL_G          equ 0x07            ; Ctrl+G = Go to line
CTRL_A          equ 0x01            ; Ctrl+A = Select All
CTRL_HOME       equ 0x77            ; Ctrl+Home scancode
CTRL_END        equ 0x75            ; Ctrl+End scancode

; Alt key scancodes (AL=0, AH=scancode)
ALT_F           equ 0x21            ; Alt+F
ALT_E           equ 0x12            ; Alt+E
ALT_S           equ 0x1F            ; Alt+S
ALT_X           equ 0x2D            ; Alt+X

; Shift flag location in BIOS Data Area
BDA_SHIFT_FLAGS equ 0x0417          ; Shift/Ctrl/Alt flags

; =============================================================================
; ENTRY POINT (offset 6)
; =============================================================================
entry:
    ; Ensure direction flag is clear for all string operations
    cld

    ; Initialize editor state
    call ed_init

    ; Check if filename was passed as argument
    mov ah, SYS_GET_ARGC
    int 0x80
    cmp cl, 0
    je .no_file_arg

    ; Load the file argument
    mov cl, 0                       ; argv[0]
    mov ah, SYS_GET_ARGV
    int 0x80                        ; SI = filename, CX = length
    jc .no_file_arg                 ; Guard: skip if argv failed
    test cx, cx
    jz .no_file_arg                 ; Guard: skip if empty
    call ed_load_file

.no_file_arg:
    ; Draw initial screen
    call ed_draw_screen

    ; Main loop
.main_loop:
    ; Read a key
    mov ah, SYS_READ_KEY
    int 0x80                        ; AH=scancode, AL=ASCII

    ; Dispatch key
    call ed_handle_key

    ; Update status bar
    call ed_draw_status

    jmp .main_loop

; =============================================================================
; ED_INIT — Initialize editor state
; =============================================================================
ed_init:
    ; Initialize gap buffer (entire buffer is the gap)
    mov word [gap_start], GAP_BUF_START
    mov word [gap_end], GAP_BUF_END + 1

    ; Clear cursor/view state
    xor ax, ax
    mov [cursor_row], ax            ; Logical row 0
    mov [cursor_col], ax            ; Column 0
    mov [view_top], ax              ; View starts at line 0
    mov [total_lines], ax
    inc word [total_lines]          ; At least 1 line (empty file)
    mov [sel_active], al            ; No selection
    mov [sel_start], ax
    mov [sel_end], ax
    mov [modified], al              ; Not modified
    mov [insert_mode], al
    mov byte [insert_mode], 1       ; Insert mode on by default
    mov [clipboard_len], ax         ; Empty clipboard
    mov [search_len], al            ; No search string
    mov [menu_open], al             ; No menu open

    ; Clear filename
    mov byte [filename], 0
    mov byte [filename_len], 0

    ; Clear the clipboard area
    push es
    push di
    mov ax, ds
    mov es, ax
    mov di, CLIPBOARD_ADDR
    xor al, al
    mov cx, CLIPBOARD_SIZE
    rep stosb
    pop di
    pop es

    ret

; =============================================================================
; MODULE INCLUDES
; =============================================================================

%include "edit_draw.inc"
%include "edit_keys.inc"
%include "edit_gap.inc"
%include "edit_cursor.inc"
%include "edit_editing.inc"
%include "edit_select.inc"
%include "edit_clipboard.inc"
%include "edit_file.inc"
%include "edit_find.inc"
%include "edit_menu.inc"
%include "edit_dialog.inc"
%include "edit_msg.inc"
%include "edit_exit.inc"
%include "edit_data.inc"
