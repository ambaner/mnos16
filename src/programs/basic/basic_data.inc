; =============================================================================
; basic_data.inc — BASIC interpreter data section
;
; Memory layout inside the TPA (0x8000–0xF7FF, 30 KB):
;
;   0x8000–0xAxxx  BASIC.MNX code + initialised data + relocations
;   0xAC00–0xBFFF  BSS region (working state, tables, buffers)
;   0xC000–0xCFFF  LOAD scratch buffer (4 KB; conventional memory)
;   0xD000–0xF7FF  Tokenised program area (10 KB)
;
; All BSS lives at fixed TPA offsets via `equ` constants — no relocatable
; labels into uninitialised space.  This matches the EDIT.MNX convention.
;
; The LOAD scratch buffer lives at 0xC000 because BIOS INT 13h disk reads
; do not reliably preserve A20 — using HMA (seg 0xFFFF) as a destination
; can wrap to physical 0x0 and corrupt the IVT, triple-faulting the CPU.
;
; Tokenised lines live at 0xD000+ and grow upward to 0xF7FF.  When the
; interpreter exits to the shell, that area is reclaimed by the next .MNX.
; =============================================================================

; =============================================================================
; LIMITS
; =============================================================================
BAS_LINE_BUF_LEN   equ 256          ; REPL/input line buffer
BAS_TOKEN_BUF_LEN  equ 256          ; Scratch tokenized line
BAS_MAX_LINES      equ 256          ; Max program lines (size of line index)
BAS_MAX_VARS       equ 128          ; Variable table entries
BAS_VAR_REC_SIZE   equ 16           ; Bytes per variable record
BAS_GOSUB_DEPTH    equ 32           ; GOSUB stack depth
BAS_FOR_DEPTH      equ 16           ; FOR stack depth
BAS_WHILE_DEPTH    equ 16           ; WHILE stack depth
BAS_EXPR_DEPTH     equ 16           ; Expression value stack depth
BAS_TEMP_STR_MAX   equ 32           ; Temp string pool slots
BAS_MAX_CHANNELS   equ 4            ; Open file channels (#1..#4)
BAS_CHAN_REC_SIZE  equ 32           ; Bytes per channel record
BAS_MAX_LINENO     equ 32767        ; Highest valid line number
BAS_TAB_ZONE       equ 14           ; PRINT comma tab width

; =============================================================================
; ERROR CODES
; =============================================================================
BERR_SYNTAX        equ 1
BERR_UNDEF_LINE    equ 2
BERR_OOM           equ 3
BERR_TYPE          equ 4
BERR_DIVZERO       equ 5
BERR_OVERFLOW      equ 6
BERR_BADFUNC       equ 7
BERR_RETWITHOUT    equ 8
BERR_NEXTWITHOUT   equ 9
BERR_WENDWITHOUT   equ 10
BERR_TOOCOMPLEX    equ 11
BERR_TOOMANYVARS   equ 12
BERR_PROGTOOBIG    equ 13
BERR_STRTOOLONG    equ 14
BERR_BADSUBSCR     equ 15
BERR_DUPDEF        equ 16
BERR_UNDIM         equ 17
BERR_GOSUBOVER     equ 18
BERR_FOROVER       equ 19
BERR_WHILEOVER     equ 20
BERR_INPUTERR      equ 21
BERR_INPUTPAST     equ 22
BERR_BADCHAN       equ 23
BERR_ALREADYOPEN   equ 24
BERR_NOTOPEN       equ 25
BERR_TOOMANYFILES  equ 26
BERR_FILENOTFOUND  equ 27
BERR_DISKFULL      equ 28
BERR_DISKIO        equ 29
BERR_FILEEXISTS    equ 30
BERR_BREAK         equ 31
BERR_LINETOOLONG   equ 32
BERR_BADLINENUM    equ 33
BERR_NOPROGRAM     equ 34
BERR_INTERNAL      equ 35
BERR_MAX           equ 35

; =============================================================================
; CHANNEL MODES
; =============================================================================
BCHAN_CLOSED       equ 0
BCHAN_INPUT        equ 'I'
BCHAN_OUTPUT       equ 'O'
BCHAN_APPEND       equ 'A'

; =============================================================================
; VARIABLE TYPE TAGS
; =============================================================================
BVAR_NUMERIC       equ 'N'
BVAR_STRING        equ 'S'
BVAR_NUM_ARRAY     equ 'A'
BVAR_STR_ARRAY     equ 'B'

; --- String value flags (used by string ABI) ---------------------------------
BSTR_LITERAL       equ 0
BSTR_VAR           equ 1
BSTR_TEMP          equ 2

; =============================================================================
; STRUCT OFFSETS
; =============================================================================
; --- Channel record (32 bytes) ----------------------------------------------
BCH_MODE           equ 0            ; byte: BCHAN_*
BCH_NAME           equ 1            ; 12 bytes: 11-byte MNFS name + pad
BCH_BUF_SEG        equ 13           ; word
BCH_BUF_OFF        equ 15           ; word
BCH_LEN            equ 17           ; word: bytes in buffer
BCH_POS            equ 19           ; word: current read/write position
BCH_CAP            equ 21           ; word: allocated buffer capacity

; --- Variable record (16 bytes) ---------------------------------------------
BVR_NAME0          equ 0            ; byte: letter (0..25) | (string_flag<<7)
BVR_NAME1          equ 1            ; byte: digit (0..9 or 0xFF)
BVR_TYPE           equ 2            ; BVAR_* tag
BVR_FLAGS          equ 3
BVR_VAL_LO         equ 4            ; int value, OR string heap seg, OR array seg
BVR_VAL_HI         equ 6            ; string heap offset, OR array offset
BVR_LEN_DIM        equ 8            ; current string length OR array upper bound
BVR_ELEM_TYPE      equ 10           ; for arrays: BVAR_NUMERIC/STRING of elements

; --- GOSUB stack entry (4 bytes) --------------------------------------------
BGS_LINEPTR        equ 0
BGS_OFFSET         equ 2

; --- FOR stack entry (16 bytes) ---------------------------------------------
BFE_VARIDX         equ 0
BFE_LIMIT          equ 2
BFE_STEP           equ 4
BFE_LINEPTR        equ 6
BFE_OFFSET         equ 8

; --- WHILE stack entry (4 bytes) --------------------------------------------
BWE_LINEPTR        equ 0
BWE_OFFSET         equ 2

; --- Expression value stack slot (8 bytes) ----------------------------------
BES_VAL_LO         equ 0            ; int OR string seg
BES_VAL_HI         equ 2            ; 0 OR string off
BES_LEN            equ 4            ; 0 OR string length
BES_TYPE           equ 6            ; 'N' / 'S'
BES_FLAGS          equ 7            ; BSTR_* (for strings)

; =============================================================================
; FIXED BSS ADDRESSES (within the TPA)
;
; Carefully laid out so the program area (BAS_PROG_BASE) gives the maximum
; possible space (~14 KB) while still leaving room for all BSS structures.
; =============================================================================
BAS_BSS_BASE       equ 0xAC00       ; BSS region starts here

; --- Tiny scalars (saved SP, pointers, counts, etc.) at top of BSS ----------
bas_repl_sp        equ 0xAC00       ; word: saved SP for error longjmp
bas_in_run         equ 0xAC02       ; byte: 1 if program executing
bas_break_tick     equ 0xAC03       ; byte: cooperative BREAK polling tick
bas_err            equ 0xAC04       ; word: ERR
bas_erl            equ 0xAC06       ; word: ERL
bas_cur_line       equ 0xAC08       ; word: current line ptr (during RUN)
bas_cur_off        equ 0xAC0A       ; word: 1 if last statement performed a GOTO/GOSUB/RETURN jump
bas_jumped         equ 0xAC0A       ; alias for clarity
bas_def_seg        equ 0xAC0C       ; word: PEEK/POKE segment (DEF SEG)
bas_rnd_state      equ 0xAC0E       ; word: RND LCG state
bas_rnd_seed       equ 0xAC0E       ; alias for bas_rnd_state

bas_prog_len       equ 0xAC10       ; word: bytes in use in program area
bas_var_count      equ 0xAC12       ; word
bas_line_count     equ 0xAC14       ; word
bas_gs_top         equ 0xAC16       ; word: GOSUB stack pointer (count)
bas_for_top        equ 0xAC18       ; word
bas_wh_top         equ 0xAC1A       ; word
bas_ex_top         equ 0xAC1C       ; word
bas_op_top         equ 0xAC1E       ; word
bas_temp_top       equ 0xAC20       ; word
bas_heap_seg       equ 0xAC22       ; word: cached HMA segment
bas_scratch_a      equ 0xAC24       ; word
bas_scratch_b      equ 0xAC26       ; word
bas_scratch_c      equ 0xAC28       ; word
bas_scratch_d      equ 0xAC2A       ; word
bas_print_col      equ 0xAC2C       ; word: PRINT current column
bas_save_chan      equ 0xAC2E       ; word: channel index in use for SAVE
bas_color_attr     equ 0xAC30       ; byte: COLOR attribute (bg<<4|fg)
; --- LOAD-path persistent heap tracker (so we can free on any exit) ----------
bas_load_heap_seg  equ 0xAC32       ; word: HMA seg of LOAD buffer (0 if none)
bas_load_heap_off  equ 0xAC34       ; word: offset within HMA seg

; --- Line buffers -----------------------------------------------------------
bas_line_buf       equ 0xAC40       ; 256 bytes (input line)
bas_token_buf      equ 0xAD40       ; 256 bytes (tokenized line scratch)
bas_fname_buf      equ 0xAE40       ; 16 bytes (8.3 filename scratch)

; --- Variable table (128 * 16 = 2048 bytes) ---------------------------------
bas_var_table      equ 0xAE60       ; ends at 0xB660

; --- Line index (256 * 4 = 1024 bytes; entry = word line_no, word offset) ---
bas_line_index     equ 0xB660       ; ends at 0xBA60

; --- Stacks -----------------------------------------------------------------
bas_gs_stack       equ 0xBA60       ; 32 * 4 = 128 → ends 0xBAE0
bas_for_stack      equ 0xBAE0       ; 16 * 16 = 256 → ends 0xBBE0
bas_wh_stack       equ 0xBBE0       ; 16 * 4 = 64  → ends 0xBC20
bas_ex_stack       equ 0xBC20       ; 16 * 8 = 128 → ends 0xBCA0
bas_op_stack       equ 0xBCA0       ; 16 * 2 = 32  → ends 0xBCC0
bas_temp_pool      equ 0xBCC0       ; 32 * 4 = 128 → ends 0xBD40
bas_channels       equ 0xBD40       ; 4 * 32 = 128 → ends 0xBDC0

; (0xBDC0..0xBFFF reserved for future BSS growth — ~576 bytes)

; --- LOAD scratch buffer (conventional memory, avoids HMA/BIOS-A20 issue) ---
; BIOS INT 13h reads via FS_READ_FILE don't reliably honor A20, so loading
; into an HMA-backed heap allocation corrupts the IVT.  We use a fixed
; conventional-memory buffer in TPA instead.  Caps LOAD files at 4 KB.
BAS_LOAD_BUF_LEN   equ 4096
bas_load_buf       equ 0xC000       ; 4 KB scratch for FS_READ_FILE

; --- Tokenized program area -------------------------------------------------
BAS_PROG_BASE      equ 0xD000
BAS_PROG_END       equ 0xF7FF       ; last usable byte
BAS_PROG_LIMIT     equ BAS_PROG_END - BAS_PROG_BASE + 1   ; 10240 bytes

; =============================================================================
; PROGRAM LINE FORMAT (in the program area, packed back-to-back)
;
; Offset 0: word  line_number  (1..32767)
; Offset 2: byte  payload_len  (1..255, length of tokenized bytes incl. EOL)
; Offset 3: ...   payload tokens, last byte = TOK_EOL (0x00)
;
; Lines kept sorted by line_number, scanned linearly for editing.
; The line_index parallel table caches (line_no, offset_from_PROG_BASE) for
; O(log n) GOTO/GOSUB lookup, rebuilt on every edit.
; =============================================================================
BLINE_HDR_SIZE     equ 3
BLINE_OFF_LINENO   equ 0
BLINE_OFF_LEN      equ 2
BLINE_OFF_PAYLOAD  equ 3
