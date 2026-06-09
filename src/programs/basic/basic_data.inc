; =============================================================================
; basic_data.inc — BASIC interpreter data section
;
; Memory layout inside the TPA (0x8000–0xF7FF, 30 KB):
;
;   0x8000–0xC3FF  BASIC.MNX code + initialised data + relocations  (~16 KB)
;                  Hard cap enforced by NASM-time TIMES guard at the bottom of
;                  basic.asm — code+data MUST stay below BAS_BSS_BASE or the
;                  build fails.  See history below for why this matters.
;   0xC400–0xD3FF  BSS region (4 KB).  This is FRESH memory — it does not
;                  overlap code.  bas_init zeros the whole 4 KB on entry.
;   0xD400–0xE3FF  LOAD scratch buffer (4 KB, conventional memory; BIOS INT 13h
;                  reads must not target HMA — see history).
;   0xE400–0xF7FF  Tokenised program area (≈5 KB, BAS_PROG_LIMIT).
;
; Each region is fully disjoint from the others.  All BSS lives at fixed TPA
; offsets via `equ` constants — no relocatable labels into uninitialised
; space.  This matches the EDIT.MNX convention and prevents BASIC from
; needing extra relocations for its working storage.
;
; The LOAD scratch buffer lives in conventional memory because BIOS INT 13h
; disk reads do not reliably preserve A20 — using HMA (seg 0xFFFF) as a
; destination can wrap to physical 0x0 and corrupt the IVT, triple-faulting
; the CPU.
;
; History (v0.10.x):
;   * v0.9.19 a: BASIC v2.0 grew code past 0xC000, where bas_load_buf used to
;                live.  LOAD overwrote BASIC's own code → #UD on next call.
;                Fix: bumped bas_load_buf to 0xC400 and shrank BAS_PROG_BASE.
;   * v0.9.19 b: BSS still started at 0xB000, OVERLAPPING the code segment.
;                Tokenising any input wrote into bas_token_buf at 0xB160 —
;                physically the same bytes as the running interpreter's
;                middle of `bas_handle_input_line`.  REPL hung silently after
;                executing ONE line because the dispatch table was scribbled
;                on.  This had been "working" in v1.x only because the
;                overlapping bytes happened to be unreferenced.  Real fix:
;                moved BAS_BSS_BASE to 0xC400 (above all code) and added a
;                hard build-time guard that pads code to the boundary.
; =============================================================================

; =============================================================================
; LIMITS
; =============================================================================
BAS_LINE_BUF_LEN   equ 256          ; REPL/input line buffer
BAS_TOKEN_BUF_LEN  equ 256          ; Scratch tokenized line
BAS_MAX_LINES      equ 128          ; Max program lines (size of line index)
BAS_MAX_VARS       equ 64           ; Variable table entries
BAS_VAR_REC_SIZE   equ 16           ; Bytes per variable record
BAS_GOSUB_DEPTH    equ 32           ; GOSUB stack depth
BAS_FOR_DEPTH      equ 16           ; FOR stack depth
BAS_WHILE_DEPTH    equ 16           ; WHILE stack depth
BAS_EXPR_DEPTH     equ 16           ; Expression value stack depth
BAS_TEMP_STR_MAX   equ 32           ; Temp string pool slots (HMA descriptors)
BAS_MAX_CHANNELS   equ 4            ; Open file channels (#1..#4)
BAS_CHAN_REC_SIZE  equ 32           ; Bytes per channel record
BAS_MAX_USERFNS    equ 16           ; User-defined functions (DEF FN)
BAS_USERFN_REC_SIZE equ 8           ; Bytes per user-fn record
BAS_MAX_LINENO     equ 32767        ; Highest valid line number
BAS_TAB_ZONE       equ 14           ; PRINT comma tab width
BAS_STR_VAR_LEN    equ 80           ; Fixed slot size per string variable
BAS_STR_HEAP_SIZE  equ 2048         ; HMA bytes reserved for string variables
BAS_STR_MAX_TEMP   equ 80           ; Max bytes per HMA-temp string

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
BERR_OUTOFDATA     equ 36
BERR_MAX           equ 36

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

; --- User-FN record (8 bytes) — DEF FN -------------------------------------
BUF_NAME0          equ 0            ; byte: letter index 0..25 (no $ flag for now)
BUF_NAME1          equ 1            ; byte: digit 0..9 or 0xFF
BUF_PARAM0         equ 2            ; byte: param-var name0
BUF_PARAM1         equ 3            ; byte: param-var name1
BUF_EXPR_OFF       equ 4            ; word: offset into program area where expr starts
BUF_EXPR_LEN       equ 6            ; byte: length of expr token bytes (≤ 255)
BUF_RESERVED       equ 7            ; byte (reserved)

; =============================================================================
; FIXED BSS ADDRESSES (within the TPA)
;
; v0.9.19 layout: BSS_BASE moved to 0xC400 — ABOVE the code segment, fully
; disjoint from it.  See history block at top of file for why the prior
; placement (0xB000) was unsafe.
;
; Layout map (all addresses absolute within DS):
;   0xC400..0xC460  tiny scalars + v2 slots                        (  96 B)
;   0xC460..0xC560  bas_line_buf                                   ( 256 B)
;   0xC560..0xC660  bas_token_buf                                  ( 256 B)
;   0xC660..0xC680  bas_fname_buf + pad                            (  32 B)
;   0xC680..0xCA80  bas_var_table   (64  recs × 16 B)              (1024 B)
;   0xCA80..0xCC80  bas_line_index  (128 entries × 4 B)            ( 512 B)
;   0xCC80..0xCD00  bas_gs_stack    (32 × 4 B)                     ( 128 B)
;   0xCD00..0xCE00  bas_for_stack   (16 × 16 B)                    ( 256 B)
;   0xCE00..0xCE40  bas_wh_stack    (16 × 4 B)                     (  64 B)
;   0xCE40..0xCEC0  bas_ex_stack    (16 × 8 B)                     ( 128 B)
;   0xCEC0..0xCEE0  bas_op_stack    (16 × 2 B)                     (  32 B)
;   0xCEE0..0xCF60  bas_temp_pool   (32 × 4 B HMA descriptors)     ( 128 B)
;   0xCF60..0xCFE0  bas_channels    (4 × 32 B)                     ( 128 B)
;   0xCFE0..0xD060  bas_userfn_table (16 × 8 B)                    ( 128 B)
;   0xD060..0xD3FF  reserved future growth                         ( 928 B)
; =============================================================================
BAS_BSS_BASE       equ 0xC400       ; BSS region starts here
BAS_BSS_SIZE       equ 0x1000       ; 4 KB — sized to bas_load_buf boundary

; --- Tiny scalars (saved SP, pointers, counts, etc.) at top of BSS ----------
bas_repl_sp        equ 0xC400       ; word: saved SP for error longjmp
bas_in_run         equ 0xC402       ; byte: 1 if program executing
bas_break_tick     equ 0xC403       ; byte: cooperative BREAK polling tick
bas_err            equ 0xC404       ; word: ERR
bas_erl            equ 0xC406       ; word: ERL
bas_cur_line       equ 0xC408       ; word: current line ptr (during RUN)
bas_cur_off        equ 0xC40A       ; word: 1 if last statement performed a GOTO/GOSUB/RETURN jump
bas_jumped         equ 0xC40A       ; alias for clarity
bas_def_seg        equ 0xC40C       ; word: PEEK/POKE segment (DEF SEG)
bas_rnd_state      equ 0xC40E       ; word: RND LCG state
bas_rnd_seed       equ 0xC40E       ; alias for bas_rnd_state

bas_prog_len       equ 0xC410       ; word: bytes in use in program area
bas_var_count      equ 0xC412       ; word
bas_line_count     equ 0xC414       ; word
bas_gs_top         equ 0xC416       ; word: GOSUB stack pointer (count)
bas_for_top        equ 0xC418       ; word
bas_wh_top         equ 0xC41A       ; word
bas_ex_top         equ 0xC41C       ; word
bas_op_top         equ 0xC41E       ; word
bas_temp_top       equ 0xC420       ; word: count of live HMA temp descriptors
bas_heap_seg       equ 0xC422       ; word: cached HMA segment
bas_scratch_a      equ 0xC424       ; word
bas_scratch_b      equ 0xC426       ; word
bas_scratch_c      equ 0xC428       ; word
bas_scratch_d      equ 0xC42A       ; word  ⚠ RESERVED  @owner bas_run_program_from_cur
                                    ;   owns this across bas_run_direct calls
                                    ;   (holds precomputed next-line offset).
                                    ;   Statement handlers MUST NOT touch it.
bas_print_col      equ 0xC42C       ; word: PRINT current column
bas_save_chan      equ 0xC42E       ; word: channel index in use for SAVE
bas_color_attr     equ 0xC430       ; byte: COLOR attribute (bg<<4|fg)
; --- LOAD-path persistent heap tracker (so we can free on any exit) ----------
bas_load_heap_seg  equ 0xC432       ; word: HMA seg of LOAD buffer (0 if none)
bas_load_heap_off  equ 0xC434       ; word: offset within HMA seg

; --- v2 NEW SLOTS -----------------------------------------------------------
bas_str_heap_seg   equ 0xC436       ; word: HMA seg of string-var heap (0=unalloc)
bas_str_heap_off   equ 0xC438       ; word: HMA off  of string-var heap base
bas_str_top        equ 0xC43A       ; word: bytes bumped within string heap
bas_data_line_ptr  equ 0xC43C       ; word: prog offset of current DATA line (0 = restart)
bas_data_sub_off   equ 0xC43E       ; word: byte offset within current DATA payload
bas_userfn_count   equ 0xC440       ; word: number of DEF FN entries
; bas_expr_result: 8-byte typed result slot — fields match BES_* offsets so
; the same offset macros work whether reading from bas_ex_stack or this slot.
bas_expr_result    equ 0xC442       ; +0  word  val_lo
                                    ; +2  word  val_hi
                                    ; +4  word  len
                                    ; +6  byte  type ('N'/'S')
                                    ; +7  byte  flags (BSTR_*)
; Dedicated 8-byte slot for stashing the LHS string descriptor across a
; recursive expression call (e.g. concat/compare evaluating RHS).  We can't
; reuse bas_scratch_a..d because bas_expr_pow already uses bas_scratch_a.
bas_str_lhs        equ 0xC44A       ; +0..+7 (same BES_* layout as result)
; Diag: previous mn_avail total (so brd_next_stmt only dumps on significant
; change).  Permanent: drives the BAS:diag.heap-* serial trace under KERNELD.
bas_dbg_last_avail equ 0xC452       ; word
; (0xC454..0xC45F scalar headroom — 12 B)

; --- Line buffers -----------------------------------------------------------
bas_line_buf       equ 0xC460       ; 256 bytes (input line)
bas_token_buf      equ 0xC560       ; 256 bytes (tokenized line scratch)
bas_fname_buf      equ 0xC660       ; 16 bytes (8.3 filename scratch) + 16 pad

; --- Variable table (64 * 16 = 1024 bytes) ----------------------------------
bas_var_table      equ 0xC680       ; ends at 0xCA80

; --- Line index (128 * 4 = 512 bytes; entry = word line_no, word offset) ----
bas_line_index     equ 0xCA80       ; ends at 0xCC80

; --- Stacks -----------------------------------------------------------------
bas_gs_stack       equ 0xCC80       ; 32 * 4 = 128 → ends 0xCD00
bas_for_stack      equ 0xCD00       ; 16 * 16 = 256 → ends 0xCE00
bas_wh_stack       equ 0xCE00       ; 16 * 4 = 64  → ends 0xCE40
bas_ex_stack       equ 0xCE40       ; 16 * 8 = 128 → ends 0xCEC0
bas_op_stack       equ 0xCEC0       ; 16 * 2 = 32  → ends 0xCEE0
bas_temp_pool      equ 0xCEE0       ; 32 * 4 = 128 → ends 0xCF60
bas_channels       equ 0xCF60       ; 4 * 32 = 128 → ends 0xCFE0
bas_userfn_table   equ 0xCFE0       ; 16 * 8 = 128 → ends 0xD060

; (0xD060..0xD3FF reserved for future BSS growth — 928 bytes.
;  Currently 0xD060..0xD06A hosts the DEF FN active-call frame —
;  see basic_defn.inc.  All addresses in this region get zeroed by
;  bas_init's stosb over BAS_BSS_SIZE.)

; --- LOAD scratch buffer (conventional memory, avoids HMA/BIOS-A20 issue) ---
; BIOS INT 13h reads via FS_READ_FILE don't reliably honor A20, so loading
; into an HMA-backed heap allocation corrupts the IVT.  We use a fixed
; conventional-memory buffer in TPA instead.  Caps LOAD files at 4 KB.
;
; IMPORTANT: must live ABOVE both the code end AND the BSS end.  History:
;   * Pre-v0.9.19: at 0xC000.  When BASIC v2.0 code grew past 0xC000, LOAD
;     overwrote BASIC's own code and the next call #UD'd.
;   * v0.9.19 a:   moved to 0xC400.  Fixed LOAD overlap but exposed BSS/code
;     overlap (BSS at 0xB000 sat INSIDE the code segment).
;   * v0.9.19 b:   moved to 0xD400 once BSS was relocated to 0xC400..0xD3FF.
;     Now all four regions (code / BSS / LOAD_BUF / PROG_BUF) are disjoint.
BAS_LOAD_BUF_LEN   equ 4096
bas_load_buf       equ 0xD400       ; 4 KB scratch for FS_READ_FILE

; --- Tokenized program area -------------------------------------------------
BAS_PROG_BASE      equ 0xE400
BAS_PROG_END       equ 0xF7FF       ; last usable byte
BAS_PROG_LIMIT     equ BAS_PROG_END - BAS_PROG_BASE + 1   ; 5120 bytes

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
