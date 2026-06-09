# BASIC.MNX — Line-Numbered BASIC Interpreter (v2.0)

A small interactive BASIC interpreter modelled on Microsoft GW-BASIC / IBM
BASICA.  Shipped as a regular `.MNX` user program, so it is loaded by the
shell into the Transient Program Area (TPA) like any other application.

```
┌────────────────────────────────────────────────────────────────────────┐
│  mnos:\> basic                                                         │
│                                                                        │
│  MNOS16 BASIC 2.0                                                      │
│  Type HELP for commands.                                               │
│                                                                        │
│  Ok                                                                    │
│  10 PRINT "Hello, world!"                                              │
│  RUN                                                                   │
│  Hello, world!                                                         │
│  Ok                                                                    │
│                                                                        │
└────────────────────────────────────────────────────────────────────────┘
```

**What's new in v2.0** (over the v1.x core that shipped earlier):

- String type — variables (`A$`..`Z$`, `A0$`..`Z9$`), concatenation with `+`,
  string comparison, and a full set of string functions.
- Numeric and string arrays via `DIM`, with one-dimensional indexing.
- File I/O channels: `OPEN`/`CLOSE` with `INPUT`/`OUTPUT`/`APPEND`,
  `PRINT #n`, `INPUT #n`, `EOF(n)`.
- `DATA` / `READ` / `RESTORE`.
- User-defined functions via `DEF FN`.
- `WHILE` / `WEND` (was tokenised but not executed in v1.x).

Older v1.x programs continue to run unchanged.

---

## 1. Invocation

| Form                  | Behaviour                                              |
|-----------------------|--------------------------------------------------------|
| `basic`               | Start with an empty workspace, drop into the REPL      |
| `basic FOO.BAS`       | Load `FOO.BAS` into the workspace, drop into the REPL  |
| `basic FOO`           | Same, auto-appending `.BAS` if no extension typed      |

The `.MNX` extension on `BASIC` itself is optional (the shell resolves it).
After exit (via `SYSTEM`), control returns to the shell.

---

## 2. The REPL

The REPL accepts two kinds of input:

1. **Program lines.**  Anything beginning with an integer line number is stored
   in the program at that line.  If the line already exists, it is replaced.
   A bare number with no statement deletes that line.

   ```
   10 PRINT "Hi"     ← inserts/replaces line 10
   20                ← deletes line 20
   ```

2. **Immediate (bare) commands.**  Any of the statement or command keywords
   listed below, typed without a line number, executes immediately.

   ```
   PRINT 2 + 2       → 4
   RUN               ← runs the stored program from the lowest line
   LIST              ← shows the stored program
   ```

`Ctrl+C` at the input prompt aborts the current line only (the program and
variables are preserved).  Use `SYSTEM` to exit back to the shell.

---

## 3. Language Reference

### 3.1 Data Types

| Type                | Width            | Variable suffix | Range / shape                 |
|---------------------|------------------|-----------------|-------------------------------|
| 16-bit signed int   | 2 bytes          | (none)          | −32 768 … +32 767             |
| String              | length + bytes   | `$`             | up to 80 bytes per value      |

Scalar variable names are a single ASCII letter `A`..`Z`, optionally followed
by a single digit `0`..`9`, and optionally followed by `$` for strings.  So
`A`, `A1`, `A$`, and `A1$` are four different variables.  Numeric and string
variables with the same letters live in separate namespaces.

Arrays are created with `DIM` and indexed with parentheses:

```basic
DIM A(10)         ' numeric array A(0..10)
DIM N$(20)        ' string  array N$(0..20)
A(I) = A(I) + 1
N$(0) = "Hello"
```

Arrays are one-dimensional; index range is `0..N` where `N` is the size you
declared.  A variable cannot be both a scalar and an array; the first form
used wins.

### 3.2 Statements

| Keyword         | Form                                                | Notes |
|-----------------|-----------------------------------------------------|-------|
| `PRINT`         | `PRINT expr-list`                                   | `;` joins without separator; `,` advances to next tab stop. Trailing `;` suppresses newline.  Numbers and strings can be freely mixed. |
| `PRINT #n,`     | `PRINT #n, expr-list`                               | Writes to a file channel opened for `OUTPUT` or `APPEND`. |
| `INPUT`         | `INPUT [prompt;] var [, var ...]`                   | Reads value(s) typed at the keyboard. |
| `INPUT #n,`     | `INPUT #n, var [, var ...]`                         | Reads from a file channel opened for `INPUT`. |
| `LET`           | `[LET] var = expr`                                  | `LET` keyword optional. |
| `IF…THEN`       | `IF cond THEN stmt` or `IF cond THEN line`          | `ELSE` supported on same line. |
| `FOR…NEXT`      | `FOR i = a TO b [STEP s]` … `NEXT [i]`              | Nested up to 8 deep. |
| `WHILE…WEND`    | `WHILE cond` … `WEND`                               | Nested up to 8 deep. |
| `GOTO`          | `GOTO line`                                         | Jump to absolute line number. |
| `GOSUB`/`RETURN`| `GOSUB line` … `RETURN`                             | Call stack 16 deep. |
| `ON…GOTO/GOSUB` | `ON expr GOTO line, line, …`                        | One-based dispatch; out-of-range falls through. |
| `REM`           | `REM rest-of-line`                                  | Comment. |
| `CLS`           | `CLS`                                               | Clear screen. |
| `LOCATE`        | `LOCATE row, col`                                   | 1-based cursor positioning. |
| `COLOR`         | `COLOR fg [, bg]`                                   | Set text colour for subsequent output. |
| `POKE`          | `POKE addr, byte`                                   | Write a byte at `DEF SEG`:offset. |
| `END`/`STOP`    | `END` / `STOP`                                      | Stop program, return to REPL. |
| `DIM`           | `DIM var(size)`                                     | Allocate a 1-D numeric or string array. |
| `DEF SEG`       | `DEF SEG = expr` or `DEF SEG`                       | Set / reset the segment used by `PEEK`/`POKE`. |
| `DEF FN`        | `DEF FN name(param) = expr`                         | Define a single-argument numeric function. |
| `RANDOMIZE`     | `RANDOMIZE`                                         | Seed RNG from `TIME`. |
| `OPEN`          | `OPEN "FILE.EXT" FOR mode AS #n`                    | `mode` ∈ {`INPUT`, `OUTPUT`, `APPEND`}.  Channels `#1`..`#4`. |
| `CLOSE`         | `CLOSE [#n [, #n ...]]`                             | Bare `CLOSE` closes all open channels. |
| `DATA`          | `DATA item, item, ...`                              | Inline data for `READ`; quoted items become strings. |
| `READ`          | `READ var [, var ...]`                              | Pull next `DATA` items into the listed variables. |
| `RESTORE`       | `RESTORE [line]`                                    | Rewind the data cursor (optionally to the first `DATA` at/after `line`). |

### 3.3 Functions

**Numeric:**
`ABS(n)`, `SGN(n)`, `INT(n)`, `RND(n)`, `TIME`, `PEEK(addr)`,
`LEN(s$)`, `ASC(c$)`, `VAL(s$)`, `EOF(n)`.

**String:**
`CHR$(n)`, `STR$(n)`, `LEFT$(s$, n)`, `RIGHT$(s$, n)`,
`MID$(s$, start[, len])`, `INKEY$`, `INPUT$(n)`.

**User-defined:**
After `DEF FN F(X) = X * X + 1`, the expression `FN F(7)` evaluates to 50.
User functions take one numeric argument and return a number; the body may
call other (non-recursive) user functions.

### 3.4 Operators

Arithmetic: `+ - * /`, integer `MOD`
String:     `+`  (concatenation; `"AB" + "CD"` → `"ABCD"`)
Relational: `= <> < <= > >=`  (numeric *and* string)
Logical:    `AND OR NOT`

Standard precedence: `NOT > * / MOD > + - > relational > AND > OR`.
Parentheses override.

---

## 4. Commands (REPL only)

| Command              | Effect                                                       |
|----------------------|--------------------------------------------------------------|
| `RUN`                | Run the stored program from its lowest line                  |
| `LIST [from[-to]]`   | Print the stored program (full, or a line range)             |
| `NEW`                | Erase the program and clear all variables                    |
| `CLEAR`              | Clear variables only (program is kept)                       |
| `LOAD "FILE.BAS"`    | Replace the program with the contents of `FILE.BAS`          |
| `SAVE "FILE.BAS"`    | Write the program to disk (atomic; see notes below)          |
| `FILES`              | List `.BAS` files on disk                                    |
| `HELP`               | Brief on-screen command reference                            |
| `SYSTEM`             | Exit BASIC, return to the shell                              |

Quoted filenames are recommended; bare filenames also work (e.g.
`LOAD TESTBAS.BAS`).  The `.BAS` extension is added automatically if omitted.

**`NEW` / `CLEAR` semantics.**  Both call the same teardown helper that:
- closes every open file channel (`#1`..`#4`),
- frees every array and string variable's heap allocation,
- frees the string-variable heap and every temp string,
- rewinds the `DATA` cursor and clears the `DEF FN` table.

`NEW` additionally empties the program buffer; `CLEAR` keeps it.

**SAVE atomicity.**  `SAVE` is implemented via the `FS_REPLACE_FILE` syscall
(AH=0x09) through the `mn_save_file` helper in `mnoslib.inc`.  The new file
contents are written to freshly allocated sectors first, and only then is the
directory entry flipped to point at them.  If the data write fails, the old
file is untouched.  Replacing a file leaks the old extent (acceptable in
MNFS's append-only allocation model).

---

## 5. File I/O

```basic
10 OPEN "OUT.TXT" FOR OUTPUT AS #1
20 FOR I = 1 TO 5
30   PRINT #1, I, I * I
40 NEXT I
50 CLOSE #1
60 OPEN "OUT.TXT" FOR INPUT AS #2
70 WHILE NOT EOF(2)
80   INPUT #2, A, B
90   PRINT A, B
100 WEND
110 CLOSE #2
```

- Channels `#1`..`#4` (four concurrent files).
- `FOR OUTPUT` starts an empty buffer; the file is rewritten atomically at
  `CLOSE` via `FS_REPLACE_FILE`.
- `FOR APPEND` reads the existing file first (if any), then behaves like
  `OUTPUT` — the file is rewritten with the original + appended bytes.
- `FOR INPUT` slurps the file into a 4 KB per-channel buffer in High Memory
  (HMA).  `EOF(n)` returns true when the read cursor reaches end-of-data.
- `NEW`, `CLEAR`, `SYSTEM`, and any runtime error close all open channels.
  `OUTPUT`/`APPEND` channels closed by error are *not* flushed (the buffer
  is discarded), which matches the "no half-written files" guarantee.

Per-channel cap: 4 KB.  Exceeding this from `PRINT #` returns
`Out of memory`; reading a >4 KB file with `FOR INPUT` truncates at 4 KB.

---

## 6. DATA / READ / RESTORE

```basic
10 DATA 3, 1, 4, 1, 5, 9, 2, 6
20 DATA "Spring", "Summer", "Autumn", "Winter"
30 FOR I = 1 TO 8
40   READ N
50   PRINT N;
60 NEXT I
70 PRINT
80 FOR I = 1 TO 4
90   READ S$
100   PRINT S$
110 NEXT I
120 RESTORE
130 READ X : PRINT "First number again:"; X
```

- `DATA` items can be unquoted (any non-`,`, non-`:` text) or `"quoted"`
  (any characters except `"`).  Whitespace around items is trimmed.
- `READ N` parses the next item as an integer (`VAL`-style).
- `READ S$` takes the next item as a string.
- `RESTORE` (no argument) rewinds to the first `DATA` in the program.
- `RESTORE line` rewinds to the first `DATA` at or after `line`.
- Reading past the last item raises `Out of DATA`.

---

## 7. DEF FN

```basic
10 DEF FN SQ(X) = X * X
20 DEF FN HYP(X) = INT(SQR(FN SQ(X) + FN SQ(X+1)))     ' (illustrative)
30 PRINT FN SQ(5), FN SQ(12)
40 DEF FN SQ(X) = X * X * X       ' redefining is allowed
50 PRINT FN SQ(5)                  ' now 125
```

- Single-argument numeric functions only.
- The parameter is a normal `A`..`Z[0-9]` variable; while the function body
  runs, the parameter's value is the argument, and the previous value is
  restored on return.
- A user function can refer to other user functions, but **direct or
  indirect recursion** is rejected with `Too complex`.
- Up to 16 user functions; redefining an existing name replaces its body.

---

## 8. Sample Program — `TESTBAS.BAS`

The disk image ships with one all-in-one `.BAS` file — `TESTBAS.BAS` — which
exercises every v2.0 feature behind a menu.  (The MNFS directory is capped at
15 entries total, so the disk holds a single consolidated sample rather than
several smaller ones.)

```
mnos:\> basic testbas.bas
RUN

MNOS16 BASIC 2.0 - Test Menu
----------------------------
  1. HELLO    - FOR/NEXT squares
  2. GUESS    - INPUT + RND number guess
  3. STRINGS  - string variables and functions
  4. DATA     - DATA / READ / RESTORE
  5. DEFFN    - DEF FN user functions
  6. FILEIO   - OPEN/CLOSE/PRINT#/INPUT#/EOF (creates TMP.TXT)
  7. WHILE    - WHILE/WEND loop
  8. ARRAY    - DIM + 1-D array sum
  0. QUIT
Choice?
```

Each section is namespaced by line range (1000s for HELLO, 2000s for GUESS,
…, 8000s for ARRAY) and ends with `GOTO 20` to return to the menu.  Variable
names are chosen so that exercising any subset of the sections in any order
will not cause cross-section collisions (notably: the ARRAY test uses `Q` so
that the FILEIO scalar `F1`/`F2` does not clash with a `DIM`med name).

`FILEIO` writes a `TMP.TXT` to disk; subsequent runs replace it atomically,
but it remains on disk after the test — use the shell `del tmp.txt` if you
need to reclaim the directory slot.

---

## 9. Error Handling

When a runtime or syntax error occurs, BASIC prints a brief message and the
line number on which it happened, then returns to the REPL prompt.  The
program and all variables are preserved (open file channels are closed
without flushing).

| Message                  | Meaning                                                  |
|--------------------------|----------------------------------------------------------|
| `Syntax error in N`      | Could not tokenise or parse the line                     |
| `Type mismatch in N`     | Numeric expression where string expected, or vice versa  |
| `Undefined line N`       | `GOTO`/`GOSUB`/`THEN` target line does not exist         |
| `Out of memory`          | Program buffer, var/array storage, or channel buf full   |
| `Out of DATA`            | `READ` past the end of all `DATA` statements             |
| `Division by zero`       | `/` or `MOD` with zero divisor                           |
| `Illegal function call`  | Argument out of range for a function                     |
| `Too complex`            | Recursion in `DEF FN`, or temp-string pool full          |
| `Too many variables`     | Variable / `DEF FN` / array table full                   |
| `String too long`        | String value would exceed 80 bytes                       |
| `Subscript out of range` | Array index outside `0..DIM size`                        |
| `Bad file number`        | `#n` is not a currently open channel                     |
| `File not found`         | `LOAD` / `OPEN FOR INPUT` of a missing file              |
| `Disk error`             | `LOAD`/`SAVE`/file I/O failed (see serial debug)         |

Internally, all error paths jump to a central `bas_error` trampoline that
restores the REPL stack frame and resumes input — no `iret`-from-nowhere
hazards.

---

## 10. Internals

Source modules (`src/programs/basic/`):

```
basic.asm             Entry point, REPL loop, MNEX header.  Master %include list.
basic_data.inc        Fixed-address layout: program buffer, var table, FOR/GOSUB
                      stacks, channel records, DEF FN table, error trampoline state.
basic_tokens.inc      Keyword / function / operator token IDs and spellings.
basic_lex.inc         Tokenizer (one-pass; numbers/strings stored inline) and the
                      detokenizer used by LIST and SAVE.
basic_err.inc         Central error path, ERR/ERL, message table.
basic_edit.inc        Readline + program-line list operations (insert/replace/delete).
basic_load.inc        LOAD / SAVE (via mn_save_file) / FILES / NEW / CLEAR.
basic_var.inc         Variable storage (numeric + string scalars).
basic_str.inc         String descriptors, the HMA string-var heap, and the
                      temp-string pool freed at each statement boundary.
basic_array.inc       DIM + 1-D array storage (numeric and string).
basic_io.inc          File channels — OPEN / CLOSE / PRINT# / INPUT# / EOF.
basic_dataread.inc    DATA tokenisation payload + READ / RESTORE.
basic_defn.inc        DEF FN registration + invocation.
basic_expr.inc        Pratt-style expression evaluator (typed: numeric + string).
basic_stmt.inc        Statement dispatcher + handler set.
```

### 10.1 Tokenisation

Each line is tokenised once on entry.  Numeric literals become a 1-byte
`TOK_INT_LIT` followed by a 2-byte little-endian value.  Line references
(targets of `GOTO`/`GOSUB`/`THEN`) become `TOK_LINEREF` + line number, which
makes the runtime branch fast.  Quoted strings become `TOK_STR_LIT` +
length + bytes.  Variable names are stored as `TOK_VAR_NAME` + a packed
2-byte name (letter, then digit-or-`$`-or-NUL).  Keywords and operators each
have their own token ID (see `basic_tokens.inc`).

`DATA` payloads use a dedicated `TOK_DATA_RAW` (= 0xF4) + length + raw bytes
format: the tokenizer emits the keyword `DATA`, then a single `TOK_DATA_RAW`
record containing everything up to the next `:` or end-of-line (with
`:`-inside-quotes preserved).  `READ` parses items from this payload at run
time; `LIST` and `SAVE` print the bytes back verbatim.

`LIST` reverses tokenisation with a small per-token print table.

### 10.2 Program Storage

The program is stored as a sorted linked list of line records:

```
[ 2 bytes link offset ][ 2 bytes line number ][ tokens... ][ 0x00 ]
```

`link offset` is the byte offset (within the program buffer) of the next line
record; zero means end-of-program.  This makes `LIST`, `LOAD`/`SAVE`, and line
insertion all simple linear walks.

### 10.3 Expression Evaluator

`bas_expr_eval` returns its result via a fixed 8-byte BSS slot
`bas_expr_result` carrying `{type, length, lo, hi/pointer}`.  Numeric callers
use the wrapper `bas_expr_eval_int` which enforces `'N'` and returns the
value in `DX`.

String temporaries (e.g. `LEFT$(A$+B$, 3)`) are allocated in HMA from a
32-entry pool; `bas_temp_pool_free_all` is called at each statement boundary.
String *variables* live in a separate 2 KB HMA heap with fixed 80-byte slots
per name — reassigning a string variable overwrites the slot in place.

### 10.4 Stacks: FOR / GOSUB / WHILE

`FOR` pushes a record on the for-stack: `{ var_addr, end_val, step_val,
loop_top_offset }`.  `NEXT` peeks the top, applies STEP, compares with end,
either falls through (loop done) or jumps back to `loop_top_offset`.
`NEXT var` rewinds the stack until it finds a matching record.

`WHILE` shares the for-stack and records `{ loop_top_offset }`; `WEND`
re-evaluates the condition and jumps back or pops.

`GOSUB` pushes the *return offset* (offset within program buffer of the
statement after the `GOSUB`) on the gosub-stack; `RETURN` pops and jumps.

### 10.5 File Channels

Four channel records live at fixed BSS offsets.  Each holds: a 2-byte HMA
allocation handle, a mode byte, a current-position word, and a current-length
word.  `PRINT #` formats into a local stack buffer then `memcpy`s into the
channel buffer at `+length`; `INPUT #` parses tokens out of the buffer at
`+position`.  `CLOSE` of an `OUTPUT`/`APPEND` channel pages the buffer back
out to disk in ≤4 KB chunks via `mn_load_file` staging and `FS_REPLACE_FILE`.

### 10.6 The bas_error Trampoline

A `setjmp`-style mechanism implemented entirely in 16-bit asm: at REPL entry,
`bas_repl_sp` is saved.  Any error path does:

```
mov sp, [bas_repl_sp]
jmp bas_repl_resume
```

— so the deep handler stack is unwound in one shot and control resumes at the
prompt.  The trampoline also runs `bas_temp_pool_free_all`, closes all
channels with discard, and clears the `DEF FN` recursion guard, so the next
input line starts from a clean transient state.

### 10.7 SAVE

`SAVE` calls `mn_save_file` (`mnoslib.inc`), which wraps `FS_REPLACE_FILE`
(INT 0x81 AH=0x09).  This is atomic: data is written to new sectors first,
then the directory entry is flipped.  See `doc/FILESYSTEM.md` §8.9 for the
full syscall contract.

The earlier implementation used `FS_DELETE_FILE` + `FS_WRITE_FILE`, which had
a register-clobber footgun (`fs_recalc_total` clobbered DX inside the DELETE
path) and could truncate a file to a single byte on a successful "save".  The
move to `FS_REPLACE_FILE` + the FS ABI contract eliminates both classes of
bug.

---

## 11. Known Limits

| Limit                            | Value         |
|----------------------------------|---------------|
| Program buffer                   | 5 KB tokens (≈5 120 bytes)   |
| Max string value                 | 80 bytes      |
| String-variable heap (HMA)       | 2 KB → ~25 distinct string variables |
| Temp-string pool                 | 32 concurrent temps per statement |
| Numeric range                    | 16-bit signed |
| Scalar variables                 | 96 entries (numeric + string + array headers combined) |
| Array entries (per array)        | Capped by HMA heap and the 96 var-table slots |
| Open file channels               | 4 (`#1`..`#4`) |
| Per-channel I/O buffer (HMA)     | 4 KB          |
| `DEF FN` count                   | 16            |
| `DATA` payload per source line   | 255 bytes (raw) |
| FOR/WHILE nesting (shared)       | 8 deep        |
| GOSUB nesting                    | 16 deep       |
| Line numbers                     | 1 … 65535     |
| `.BAS` files in `FILES`          | Up to MNFS dir capacity (15 entries total disk-wide) |

No floating-point.  No multi-dimensional arrays.  No `ON ERROR` /
user-driven error trapping (the central trampoline always resumes at the
REPL prompt).

---

## 12. References

- `src/programs/basic/basic.asm` — module overview and `%include` order
- `src/programs/basic/basic_data.inc` — runtime layout
- `doc/FILESYSTEM.md` §8.9 — `FS_REPLACE_FILE` syscall used by `SAVE`
- `doc/MNOSLIB.md` — `mn_save_file` / `mn_load_file` helpers
- `tests/test_basic_*.py` — structural regression tests for tokens, dispatchers,
  budgets
- `data/TESTBAS.BAS` — single consolidated sample / smoke-test program
  (seeded onto the VHD)
