; =============================================================================
; basic_tokens.inc — Keyword / function / operator / marker token IDs +
; the canonical spelling tables used by both tokenizer and detokenizer.
;
; Token layout (single byte):
;
;   0x00          TOK_EOL              end of line
;   0x01..0x1F    reserved
;   0x20..0x7F    ASCII punctuation / digits — stored as-is
;   0x80..0xBF    statement keywords  (up to 64)
;   0xC0..0xDF    built-in functions   (up to 32)
;   0xE0..0xEF    multi-char operators (up to 16)
;   0xF0          TOK_INT     + 2 bytes signed
;   0xF1          TOK_LINEREF + 2 bytes unsigned line number
;   0xF2          TOK_STR     + 1 byte length + bytes
;   0xF3          TOK_VAR     + byte name0 (letter|str_flag<<7)
;                            + byte name1 (digit or 0xFF)
;   0xF4..0xFF    reserved
; =============================================================================

TOK_EOL          equ 0x00

; --- Statement keywords (0x80..0xBF) ----------------------------------------
TOK_PRINT        equ 0x80
TOK_LET          equ 0x81
TOK_IF           equ 0x82
TOK_THEN         equ 0x83
TOK_ELSE         equ 0x84
TOK_GOTO         equ 0x85
TOK_GOSUB        equ 0x86
TOK_RETURN       equ 0x87
TOK_FOR          equ 0x88
TOK_TO           equ 0x89
TOK_STEP         equ 0x8A
TOK_NEXT         equ 0x8B
TOK_WHILE        equ 0x8C
TOK_WEND         equ 0x8D
TOK_END          equ 0x8E
TOK_STOP         equ 0x8F
TOK_REM          equ 0x90
TOK_INPUT        equ 0x91
TOK_DIM          equ 0x92
TOK_ON           equ 0x93
TOK_CLS          equ 0x94
TOK_LOCATE       equ 0x95
TOK_COLOR        equ 0x96
TOK_DEF          equ 0x97
TOK_SEG          equ 0x98
TOK_POKE         equ 0x99
TOK_RANDOMIZE    equ 0x9A
TOK_OPEN         equ 0x9B
TOK_CLOSE        equ 0x9C
TOK_AS           equ 0x9D
TOK_OUTPUT       equ 0x9E
TOK_APPEND       equ 0x9F
TOK_SYSTEM       equ 0xA0
TOK_LIST         equ 0xA1
TOK_RUN          equ 0xA2
TOK_NEW          equ 0xA3
TOK_LOAD         equ 0xA4
TOK_SAVE         equ 0xA5
TOK_FILES        equ 0xA6
TOK_CLEAR        equ 0xA7
TOK_HELP         equ 0xA8
TOK_DATA         equ 0xA9
TOK_READ         equ 0xAA
TOK_RESTORE      equ 0xAB
TOK_FN           equ 0xAC
TOK_LAST_STMT    equ 0xAC

; --- Functions (0xC0..0xDF) -------------------------------------------------
TOK_FN_FIRST     equ 0xC0
TOK_LEFTS        equ 0xC0           ; LEFT$
TOK_RIGHTS       equ 0xC1           ; RIGHT$
TOK_MIDS         equ 0xC2           ; MID$
TOK_LEN          equ 0xC3
TOK_CHRS         equ 0xC4           ; CHR$
TOK_ASC          equ 0xC5
TOK_STRS         equ 0xC6           ; STR$
TOK_VAL          equ 0xC7
TOK_INKEYS       equ 0xC8           ; INKEY$
TOK_INPUTS       equ 0xC9           ; INPUT$
TOK_ABS          equ 0xCA
TOK_SGN          equ 0xCB
TOK_INT_FN       equ 0xCC           ; INT (function form)
TOK_RND          equ 0xCD
TOK_TIME         equ 0xCE
TOK_PEEK         equ 0xCF
TOK_EOF_FN       equ 0xD0
TOK_FN_LAST      equ 0xD0

; --- Multi-char operators (0xE0..0xEF) --------------------------------------
TOK_MOD          equ 0xE0
TOK_AND          equ 0xE1
TOK_OR           equ 0xE2
TOK_NOT          equ 0xE3
TOK_NE           equ 0xE4           ; <>
TOK_LE           equ 0xE5           ; <=
TOK_GE           equ 0xE6           ; >=

; --- Special markers --------------------------------------------------------
TOK_INT_LIT      equ 0xF0
TOK_LINEREF      equ 0xF1
TOK_STR_LIT      equ 0xF2
TOK_VAR_NAME     equ 0xF3
TOK_DATA_RAW     equ 0xF4           ; DATA payload: 1-byte len + raw bytes

; =============================================================================
; KEYWORD TABLE
;
; Format (one entry):
;   db  N           ; length of canonical spelling
;   db  'PRINT'     ; uppercase spelling (no NUL)
;   db  token       ; assigned token byte
;   db  flags       ; future use
;
; Terminator: a single byte 0x00 in the length field.
;
; ORDER MATTERS: longer keywords MUST come before shorter ones that share
; a prefix.  E.g. "RANDOMIZE" before "REM", "INKEY$" before "INPUT", etc.
; The tokenizer walks the table linearly and the first match wins.
; =============================================================================

%macro KW 2
    db %strlen(%1)
    db %1
    db %2
    db 0
%endmacro

bas_keyword_table:
    ; --- Longest first (alphabetised within length band) --------------------
    KW 'RANDOMIZE', TOK_RANDOMIZE
    KW 'RESTORE',   TOK_RESTORE
    KW 'INKEY$',    TOK_INKEYS
    KW 'INPUT$',    TOK_INPUTS
    KW 'LEFT$',     TOK_LEFTS
    KW 'RIGHT$',    TOK_RIGHTS
    KW 'COLOR',     TOK_COLOR
    KW 'CLEAR',     TOK_CLEAR
    KW 'CLOSE',     TOK_CLOSE
    KW 'FILES',     TOK_FILES
    KW 'GOSUB',     TOK_GOSUB
    KW 'INPUT',     TOK_INPUT
    KW 'PRINT',     TOK_PRINT
    KW 'WHILE',     TOK_WHILE
    KW 'WEND',      TOK_WEND
    KW 'CHR$',      TOK_CHRS
    KW 'MID$',      TOK_MIDS
    KW 'STR$',      TOK_STRS
    KW 'DATA',      TOK_DATA
    KW 'READ',      TOK_READ
    KW 'FN',        TOK_FN
    KW 'LIST',      TOK_LIST
    KW 'LOAD',      TOK_LOAD
    KW 'SAVE',      TOK_SAVE
    KW 'POKE',      TOK_POKE
    KW 'OPEN',      TOK_OPEN
    KW 'GOTO',      TOK_GOTO
    KW 'STEP',      TOK_STEP
    KW 'NEXT',      TOK_NEXT
    KW 'STOP',      TOK_STOP
    KW 'THEN',      TOK_THEN
    KW 'ELSE',      TOK_ELSE
    KW 'PEEK',      TOK_PEEK
    KW 'RND',       TOK_RND
    KW 'ABS',       TOK_ABS
    KW 'SGN',       TOK_SGN
    KW 'INT',       TOK_INT_FN
    KW 'VAL',       TOK_VAL
    KW 'ASC',       TOK_ASC
    KW 'LEN',       TOK_LEN
    KW 'EOF',       TOK_EOF_FN
    KW 'REM',       TOK_REM
    KW 'DIM',       TOK_DIM
    KW 'FOR',       TOK_FOR
    KW 'CLS',       TOK_CLS
    KW 'DEF',       TOK_DEF
    KW 'SEG',       TOK_SEG
    KW 'MOD',       TOK_MOD
    KW 'AND',       TOK_AND
    KW 'NOT',       TOK_NOT
    KW 'RUN',       TOK_RUN
    KW 'NEW',       TOK_NEW
    KW 'END',       TOK_END
    KW 'RETURN',    TOK_RETURN
    KW 'SYSTEM',    TOK_SYSTEM
    KW 'LOCATE',    TOK_LOCATE
    KW 'OUTPUT',    TOK_OUTPUT
    KW 'APPEND',    TOK_APPEND
    KW 'HELP',      TOK_HELP
    KW 'TIME',      TOK_TIME
    KW 'OR',        TOK_OR
    KW 'TO',        TOK_TO
    KW 'ON',        TOK_ON
    KW 'AS',        TOK_AS
    KW 'IF',        TOK_IF
    KW 'LET',       TOK_LET
    db 0                           ; end marker

; =============================================================================
; DETOKENIZE TABLE — token → spelling (pointer table indexed by token byte).
; Provides O(1) lookup during LIST.  Built for all tokens 0x80..0xEF.
;
; Each pointer references a NUL-terminated uppercase spelling string.
; Tokens with no entry (zero pointer) are decoded via fallback paths.
; =============================================================================

bas_det_table:
    ; 0x80..0xBF statements
    dw bas_kw_PRINT, bas_kw_LET, bas_kw_IF, bas_kw_THEN          ; 80..83
    dw bas_kw_ELSE, bas_kw_GOTO, bas_kw_GOSUB, bas_kw_RETURN     ; 84..87
    dw bas_kw_FOR, bas_kw_TO, bas_kw_STEP, bas_kw_NEXT           ; 88..8B
    dw bas_kw_WHILE, bas_kw_WEND, bas_kw_END, bas_kw_STOP        ; 8C..8F
    dw bas_kw_REM, bas_kw_INPUT, bas_kw_DIM, bas_kw_ON           ; 90..93
    dw bas_kw_CLS, bas_kw_LOCATE, bas_kw_COLOR, bas_kw_DEF       ; 94..97
    dw bas_kw_SEG, bas_kw_POKE, bas_kw_RANDOMIZE, bas_kw_OPEN    ; 98..9B
    dw bas_kw_CLOSE, bas_kw_AS, bas_kw_OUTPUT, bas_kw_APPEND     ; 9C..9F
    dw bas_kw_SYSTEM, bas_kw_LIST, bas_kw_RUN, bas_kw_NEW        ; A0..A3
    dw bas_kw_LOAD, bas_kw_SAVE, bas_kw_FILES, bas_kw_CLEAR      ; A4..A7
    dw bas_kw_HELP, bas_kw_DATA, bas_kw_READ, bas_kw_RESTORE     ; A8..AB
    dw bas_kw_FN                                                 ; AC
    times (0xC0 - 0xAD) dw 0                                     ; AD..BF gap

    ; 0xC0..0xDF functions
    dw bas_kw_LEFTS, bas_kw_RIGHTS, bas_kw_MIDS, bas_kw_LEN      ; C0..C3
    dw bas_kw_CHRS, bas_kw_ASC, bas_kw_STRS, bas_kw_VAL          ; C4..C7
    dw bas_kw_INKEYS, bas_kw_INPUTS, bas_kw_ABS, bas_kw_SGN      ; C8..CB
    dw bas_kw_INT, bas_kw_RND, bas_kw_TIME, bas_kw_PEEK          ; CC..CF
    dw bas_kw_EOF                                                ; D0
    times (0xE0 - 0xD1) dw 0                                     ; D1..DF gap

    ; 0xE0..0xEF operators
    dw bas_kw_MOD, bas_kw_AND, bas_kw_OR, bas_kw_NOT             ; E0..E3
    dw bas_kw_NE, bas_kw_LE, bas_kw_GE                           ; E4..E6
    times (0xF0 - 0xE7) dw 0                                     ; E7..EF gap

; Canonical uppercase spellings (NUL-terminated, used by detokenizer)
bas_kw_PRINT:     db 'PRINT',0
bas_kw_LET:       db 'LET',0
bas_kw_IF:        db 'IF',0
bas_kw_THEN:      db 'THEN',0
bas_kw_ELSE:      db 'ELSE',0
bas_kw_GOTO:      db 'GOTO',0
bas_kw_GOSUB:     db 'GOSUB',0
bas_kw_RETURN:    db 'RETURN',0
bas_kw_FOR:       db 'FOR',0
bas_kw_TO:        db 'TO',0
bas_kw_STEP:      db 'STEP',0
bas_kw_NEXT:      db 'NEXT',0
bas_kw_WHILE:     db 'WHILE',0
bas_kw_WEND:      db 'WEND',0
bas_kw_END:       db 'END',0
bas_kw_STOP:      db 'STOP',0
bas_kw_REM:       db 'REM',0
bas_kw_INPUT:     db 'INPUT',0
bas_kw_DIM:       db 'DIM',0
bas_kw_ON:        db 'ON',0
bas_kw_CLS:       db 'CLS',0
bas_kw_LOCATE:    db 'LOCATE',0
bas_kw_COLOR:     db 'COLOR',0
bas_kw_DEF:       db 'DEF',0
bas_kw_SEG:       db 'SEG',0
bas_kw_POKE:      db 'POKE',0
bas_kw_RANDOMIZE: db 'RANDOMIZE',0
bas_kw_OPEN:      db 'OPEN',0
bas_kw_CLOSE:     db 'CLOSE',0
bas_kw_AS:        db 'AS',0
bas_kw_OUTPUT:    db 'OUTPUT',0
bas_kw_APPEND:    db 'APPEND',0
bas_kw_SYSTEM:    db 'SYSTEM',0
bas_kw_LIST:      db 'LIST',0
bas_kw_RUN:       db 'RUN',0
bas_kw_NEW:       db 'NEW',0
bas_kw_LOAD:      db 'LOAD',0
bas_kw_SAVE:      db 'SAVE',0
bas_kw_FILES:     db 'FILES',0
bas_kw_CLEAR:     db 'CLEAR',0
bas_kw_HELP:      db 'HELP',0
bas_kw_DATA:      db 'DATA',0
bas_kw_READ:      db 'READ',0
bas_kw_RESTORE:   db 'RESTORE',0
bas_kw_FN:        db 'FN',0

bas_kw_LEFTS:     db 'LEFT$',0
bas_kw_RIGHTS:    db 'RIGHT$',0
bas_kw_MIDS:      db 'MID$',0
bas_kw_LEN:       db 'LEN',0
bas_kw_CHRS:      db 'CHR$',0
bas_kw_ASC:       db 'ASC',0
bas_kw_STRS:      db 'STR$',0
bas_kw_VAL:       db 'VAL',0
bas_kw_INKEYS:    db 'INKEY$',0
bas_kw_INPUTS:    db 'INPUT$',0
bas_kw_ABS:       db 'ABS',0
bas_kw_SGN:       db 'SGN',0
bas_kw_INT:       db 'INT',0
bas_kw_RND:       db 'RND',0
bas_kw_TIME:      db 'TIME',0
bas_kw_PEEK:      db 'PEEK',0
bas_kw_EOF:       db 'EOF',0

bas_kw_MOD:       db 'MOD',0
bas_kw_AND:       db 'AND',0
bas_kw_OR:        db 'OR',0
bas_kw_NOT:       db 'NOT',0
bas_kw_NE:        db '<>',0
bas_kw_LE:        db '<=',0
bas_kw_GE:        db '>=',0
