# EDIT.MNX — Text Editor Design Document

## 1. Overview

EDIT.MNX is a full-screen text editor for MNOS16, inspired by MS-DOS EDIT.COM.
It runs as a standalone MNEX user-mode program loaded into the Transient Program
Area (TPA) at `0x8000`.  The editor provides a menu-driven interface with
keyboard shortcuts for all operations.

**Binary size:** 6656 bytes (13 sectors)
**Max file size:** ~19.5 KB (gap buffer capacity)
**Screen:** 80×25 VGA text mode (mode 3)

---

## 2. Architecture

### 2.1 Module Structure

```
edit.asm              Entry point, constants, includes, MNEX header
├── edit_keys.inc     Key dispatch (scancode → command routing)
├── edit_draw.inc     Screen rendering (menu bar, edit area, status bar)
├── edit_menu.inc     Drop-down menu system (File/Edit/Search)
├── edit_gap.inc      Gap buffer data structure (insert/delete/move)
├── edit_cursor.inc   Cursor movement (arrows, Home/End, PgUp/PgDn)
├── edit_editing.inc  Text manipulation (Enter, Backspace, Delete, Tab)
├── edit_select.inc   Block selection + cursor↔gap synchronization
├── edit_clipboard.inc Cut/Copy/Paste via clipboard buffer
├── edit_find.inc     Find, Find Next, Replace, Replace All, Go to Line
├── edit_dialog.inc   Modal dialogs (input prompt, file picker)
├── edit_file.inc     File I/O (load/save via INT 0x81)
├── edit_exit.inc     Exit handler (save-changes prompt)
├── edit_msg.inc      Status messages and notifications
└── edit_data.inc     State variables, strings, menu items, help text
```

### 2.2 Memory Layout

```
Address Range     Size    Purpose
─────────────────────────────────────────────────────
0x8000–0xA7FF     10 KB   Editor code (.text)
0xA800–0xA9FF     512 B   Clipboard buffer
0xAA00–0xAAFF     256 B   Search string buffer
0xAB00–0xABFF     256 B   Replace string buffer
0xAC00–0xF7FF     19.5 KB Gap buffer (document text)
0xF800–0xFFFF     2 KB    Stack (grows down from 0xFFFF)
```

### 2.3 Screen Layout (80×25)

```
Row  0:     Menu bar (VGA_ATTR_INV — black on light gray)
Rows 1–23:  Edit area (VGA_ATTR_NORM — bright white on blue)
Row  24:    Status bar (VGA_ATTR_INV — filename, Ln:Col, modified, INS/OVR)
```

---

## 3. Gap Buffer

The editor uses a **gap buffer** — a contiguous array with a movable "gap" at
the cursor position.  This provides O(1) insert/delete at the cursor and O(n)
movement for cursor repositioning.

### 3.1 Structure

```
┌───────────────────┬───────────────────────────┬───────────────────┐
│  Text before gap  │        G A P              │  Text after gap   │
│  (0xAC00..gap_st) │  (gap_start..gap_end-1)   │  (gap_end..end)   │
└───────────────────┴───────────────────────────┴───────────────────┘
```

**State variables:**
- `gap_start` — first byte of the gap (insert point)
- `gap_end` — first byte after the gap (text resumes here)

### 3.2 Operations

| Operation | Implementation | Complexity |
|-----------|---------------|------------|
| Insert char | Write at `gap_start`, increment `gap_start` | O(1) |
| Delete forward | Increment `gap_end` | O(1) |
| Delete backward | Decrement `gap_start` | O(1) |
| Move gap to offset | Shift bytes between current and target position | O(n) |
| Get text length | `(gap_start - BUF_START) + (BUF_END+1 - gap_end)` | O(1) |

### 3.3 Logical↔Physical Mapping

Characters before the gap map 1:1 to buffer offsets.  Characters after the
gap require adding the gap size:

```
physical_addr(logical_offset) =
    if offset < (gap_start - BUF_START):
        BUF_START + offset
    else:
        BUF_START + offset + gap_size
```

---

## 4. Key Dispatch

`ed_handle_key` (edit_keys.inc) is called from the main loop with AH=scancode,
AL=ASCII.  It dispatches in priority order:

1. **ASCII keys** (AL ≠ 0):
   - Ctrl combos: Ctrl+S/O/N/X/C/V/F/H/G/A
   - Escape: close menu/cancel
   - Backspace (scancode 0x0E): delete backward
   - Ctrl+H (scancode 0x23): Replace command
   - Enter, Tab: editing operations
   - Printable chars (≥ 0x20): insert/overwrite

2. **Extended keys** (AL = 0, scancode in AH):
   - Arrow keys (with Shift detection for selection)
   - Home/End/PgUp/PgDn/Insert/Delete
   - Alt+F/E/S: open menus
   - Alt+X: exit
   - F1: help, F3: Find Next, F4: Replace All

---

## 5. Menu System

### 5.1 Menu Bar

The menu bar occupies row 0 with three items: **File**, **Edit**, **Search**.
Each item's first letter renders in `VGA_ATTR_HOTKEY` (red) to indicate the
Alt+key accelerator.

### 5.2 Drop-Down Menus

| File | Edit | Search |
|------|------|--------|
| New (Ctrl+N) | Cut (Ctrl+X) | Find (Ctrl+F) |
| Open (Ctrl+O) | Copy (Ctrl+C) | Find Next (F3) |
| Save (Ctrl+S) | Paste (Ctrl+V) | Replace (Ctrl+H) |
| Save As... | Select All (Ctrl+A) | Replace All (F4) |
| Exit (Alt+X) | | Go to Line (Ctrl+G) |

### 5.3 Navigation

- **Alt+F/E/S** opens the corresponding menu
- **Left/Right arrows** switch between menus
- **Up/Down arrows** move selection highlight
- **Enter** activates the selected item
- **Escape** closes the menu

---

## 6. Find & Replace

### 6.1 Architecture

The Find system is layered:

```
┌─────────────────────────────────────────────────────┐
│  Commands (ed_cmd_find, ed_cmd_replace, etc.)       │  ← Thin dispatch
├─────────────────────────────────────────────────────┤
│  ed_input_prompt (modal dialog)                      │  ← Shared UI
├─────────────────────────────────────────────────────┤
│  ed_find_next_from_cursor / ed_find_and_replace     │  ← Mid-level
├─────────────────────────────────────────────────────┤
│  ed_search_text (core loop)                          │  ← Engine
│  ed_get_char_at_offset (gap-aware char access)       │
└─────────────────────────────────────────────────────┘
```

### 6.2 Search Algorithm

`ed_search_text` performs a brute-force linear scan from a given start offset.
It compares the search string at each text position using `ed_get_char_at_offset`
(which handles the gap buffer transparently).

**Wrap-around:** If not found after cursor, `ed_find_next_from_cursor` retries
from offset 0 (wraps around).

### 6.3 Replace All

`ed_find_and_replace` loops:
1. Search from current position
2. Move gap to found offset
3. Delete `search_len` chars (advance `gap_end`)
4. Insert replace string (write at `gap_start`)
5. Repeat from new position until no more matches

---

## 7. Modal Dialog System

### 7.1 Input Prompt (`ed_input_prompt`)

A centered 4-row dialog box used by Find, Replace, Go to Line, and Save As:

```
┌── Title ──────────────────────────────┐
│  [input field with underscores____]   │
│  Enter=OK  Esc=Cancel                 │
└───────────────────────────────────────┘
```

**Constants:** `MD_BOX_TOP=9, MD_BOX_LEFT=20, MD_BOX_W=40, MD_INPUT_MAX=34`

**Input handling:**
- Printable chars: echo to VGA and append to buffer
- Backspace: erase last char, restore underscore
- Enter: accept (return char count in AL)
- Escape: cancel (return 0)

On accept/cancel, `ed_draw_screen` is called to restore the edit area beneath
the dialog.

### 7.2 File Picker

The Open command uses a file picker dialog that lists all files on disk with
arrow-key selection and Enter to confirm.

---

## 8. File I/O

### 8.1 Loading

`ed_load_file` (edit_file.inc):
1. Calls `FS_READ_FILE` (INT 0x81, AH=0x05) to load file into gap buffer at
   `GAP_BUF_START`
2. Sets `gap_start = GAP_BUF_START + file_size`
3. Sets `gap_end = GAP_BUF_END + 1` (gap fills remaining space)
4. Resets cursor to (0,0), clears modified flag

### 8.2 Saving

`ed_save_file` (edit_file.inc):
1. Compacts the gap buffer (moves gap to end) so text is contiguous
2. Calls `FS_WRITE_FILE` (INT 0x81, AH=0x06) with buffer at `GAP_BUF_START`
   and size = text length
3. Clears modified flag on success

---

## 9. Selection & Clipboard

### 9.1 Block Selection

Shift+arrow keys activate selection mode (`sel_active=1`).  Selection is
tracked as a range of logical offsets (`sel_start`, `sel_end`).  During
rendering, characters within the selection range use `VGA_ATTR_SEL` (bright
white on cyan).

### 9.2 Clipboard Operations

| Operation | Behavior |
|-----------|----------|
| Cut (Ctrl+X) | Copy selection to clipboard, delete from buffer |
| Copy (Ctrl+C) | Copy selection to clipboard (max 512 bytes) |
| Paste (Ctrl+V) | Insert clipboard contents at cursor |
| Select All (Ctrl+A) | Set selection to entire document |

The clipboard resides at `CLIPBOARD_ADDR` (0xA800), limited to 512 bytes.

---

## 10. Display Rendering

### 10.1 Color Scheme

| Constant | Value | Usage |
|----------|-------|-------|
| `VGA_ATTR_NORM` | 0x1F | Edit area (bright white on blue) |
| `VGA_ATTR_INV` | 0x70 | Menu/status bars (black on light gray) |
| `VGA_ATTR_SEL` | 0x3F | Selected text (bright white on cyan) |
| `VGA_ATTR_DIM` | 0x19 | Line overflow marker |
| `VGA_ATTR_MENU_HI` | 0x0F | Highlighted menu item (bright white on black) |
| `VGA_ATTR_HOTKEY` | 0x74 | Menu accelerator key (red on light gray) |

### 10.2 Rendering Pipeline

`ed_draw_screen` redraws the entire display:
1. `ed_draw_menu` — menu bar with hotkey highlighting
2. `ed_draw_edit_area` — 23 lines from gap buffer (handles gap skip, tab
   expansion, selection highlighting, wrapping)
3. `ed_draw_status` — filename, Ln:Col, [Modified], INS/OVR

The edit area renderer (`ed_draw_edit_area`) scans from `view_top` through
the gap buffer, skipping the gap region, expanding tabs to 8-column stops,
and applying selection attributes where applicable.

### 10.3 Cursor Management

The hardware cursor position is set via `SYS_SET_CURSOR` (INT 0x80) after
each keystroke.  The cursor's screen position is calculated from `cursor_row`,
`cursor_col`, and `view_top`:
- Screen row = `cursor_row - view_top + EDIT_TOP_ROW`
- Screen col = `cursor_col`

---

## 11. Exit & Save Prompt

When the user presses Alt+X (or selects File→Exit) with unsaved changes:
1. A dialog prompts "Save changes? (Y/N/Esc)"
2. **Y** — saves then exits
3. **N** — exits without saving
4. **Esc** — returns to editing

The exit handler returns to the shell via `RET` (standard MNEX program
termination convention).

---

## 12. Debug Support

When `EDIT_DEBUG` is defined (line 36 of edit.asm), debug traces are emitted
via `SYS_DBG_HEX16` (INT 0x80, AH=0x21) to the serial port.  This provides:

- Search offset tracing (Find operation start/found positions)
- Gap buffer state (gap_start, gap_end before/after moves)
- Cursor recalculation progress
- BX register used as a marker ID (0xAAAA, 0xBBBB, etc.) to distinguish
  trace points in serial output

---

## 13. Limitations & Future Work

| Limitation | Notes |
|------------|-------|
| Max file ~19.5 KB | Bounded by gap buffer (0xAC00–0xF7FF) |
| No undo/redo | Would require an operation log or second buffer |
| No syntax highlighting | Single-color edit area |
| No word wrap | Lines truncate at column 80 |
| No multi-file | One file open at a time |
| 512-byte clipboard | Sufficient for most edits, could expand |
| Brute-force search | O(n×m) — acceptable for ~19 KB texts |

### Potential Enhancements

- **Undo ring** — circular buffer of edit operations
- **Line numbers** — left gutter with line numbering
- **Horizontal scroll** — support lines > 80 chars
- **Binary/hex mode** — toggle hex view for non-text files
- **Macro recording** — record/replay keystroke sequences

---

## 14. Build & Integration

### Assembly

```
nasm -f bin -I src/include/ -I src/programs/edit/ -o build/boot/edit.mnx src/programs/edit/edit.asm
```

### MNEX Header

```
Offset  Size  Field
0       4     Magic: "MNEX"
4       2     Sector count: 13 (little-endian)
6       —     Entry point (code begins here)
```

### Shell Integration

The shell uses implicit execution — typing `edit` or `edit MYFILE.TXT` at the
prompt triggers the program loader which resolves `EDIT.MNX` via the filesystem,
loads it into the TPA, and passes the filename argument via the argc/argv
syscalls (`SYS_GET_ARGC`, `SYS_GET_ARGV`).  No special `edit` built-in command
exists; EDIT.MNX is treated like any other .MNX program.

### Disk Layout

EDIT.MNX occupies 13 contiguous sectors in the MNFS filesystem.  It is loaded
on-demand when the user types `edit` or `edit FILENAME.EXT`.
