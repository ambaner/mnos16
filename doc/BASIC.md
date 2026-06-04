# BASIC.MNX — Line-Numbered BASIC Interpreter

A small interactive BASIC interpreter modelled on Microsoft GW-BASIC / IBM
BASICA.  Shipped as a regular `.MNX` user program, so it is loaded by the
shell into the Transient Program Area (TPA) like any other application.

```
┌────────────────────────────────────────────────────────────────────────┐
│  mnos:\> basic                                                         │
│                                                                        │
│  MNOS16 BASIC 1.0                                                      │
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

| Type                | Width       | Variable suffix | Range / shape                 |
|---------------------|-------------|-----------------|-------------------------------|
| 16-bit signed int   | 2 bytes     | (none)          | −32 768 … +32 767             |
| String              | length + bytes | `$`           | up to ~80 bytes, BCPL-style   |

Variables are single letter `A`-`Z`, optionally followed by `$` for strings
(so `A` and `A$` are different variables of different type).  Arrays are
created with `DIM` and indexed with parentheses (e.g. `DIM A(10)` then `A(I)`).

### 3.2 Statements

| Keyword      | Form                                                | Notes |
|--------------|-----------------------------------------------------|-------|
| `PRINT`      | `PRINT expr-list`                                   | `;` joins without separator; `,` advances to next tab stop. Trailing `;` suppresses newline. |
| `INPUT`      | `INPUT [prompt;] var`                               | Reads a value (typed at the keyboard) into the variable. |
| `LET`        | `[LET] var = expr`                                  | `LET` keyword optional. |
| `IF…THEN`    | `IF cond THEN stmt` or `IF cond THEN line`          | `ELSE` supported on same line. |
| `FOR…NEXT`   | `FOR i = a TO b [STEP s]` … `NEXT [i]`              | Nested up to 8 deep. |
| `GOTO`       | `GOTO line`                                         | Jump to absolute line number. |
| `GOSUB`/`RETURN` | `GOSUB line` … `RETURN`                         | Call stack 16 deep. |
| `REM`        | `REM rest-of-line`                                  | Comment. |
| `CLS`        | `CLS`                                               | Clear screen. |
| `END`/`STOP` | `END` / `STOP`                                      | Stop program, return to REPL. |
| `DIM`        | `DIM var(size)`                                     | Allocate a 16-bit-int array. |
| `RANDOMIZE`  | `RANDOMIZE`                                         | Seed RNG from TIME. |

### 3.3 Functions

Numeric: `ABS`, `SGN`, `INT`, `RND(n)`, `TIME`, `PEEK(addr)`,
         `LEN(s$)`, `ASC(c$)`, `VAL(s$)`, `EOF(n)`

String:  `CHR$(n)`, `STR$(n)`, `LEFT$(s$, n)`, `RIGHT$(s$, n)`,
         `MID$(s$, start[, len])`, `INKEY$`, `INPUT$(n)`

### 3.4 Operators

Arithmetic: `+ - * /`, integer `MOD`
Relational: `= <> < <= > >=`
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
`LOAD HELLO.BAS`).  The `.BAS` extension is added automatically if omitted.

**SAVE atomicity.**  `SAVE` is implemented via the `FS_REPLACE_FILE` syscall
(AH=0x09) through the `mn_save_file` helper in `mnoslib.inc`.  The new file
contents are written to freshly allocated sectors first, and only then is the
directory entry flipped to point at them.  If the data write fails, the old
file is untouched.  Replacing a file leaks the old extent (acceptable in
MNFS's append-only allocation model).

---

## 5. Sample Programs

Two `.BAS` files are pre-seeded on the disk image — try them first:

### 5.1 `HELLO.BAS`

```basic
10 REM HELLO.BAS - prints squares of 1..10
20 PRINT "Squares from 1 to 10:"
30 FOR I = 1 TO 10
40 PRINT I; "squared is"; I * I
50 NEXT I
60 PRINT "Done."
70 END
```

### 5.2 `GUESS.BAS`

```basic
10 REM GUESS.BAS - guess a number between 1 and 100
20 RANDOMIZE
30 LET N = RND(100)
40 LET T = 0
50 PRINT "I'm thinking of a number from 1 to 100."
60 INPUT "Your guess"; G
70 LET T = T + 1
80 IF G = N THEN GOTO 200
90 IF G < N THEN PRINT "Too low."
100 IF G > N THEN PRINT "Too high."
110 IF T < 10 THEN GOTO 60
120 PRINT "Out of guesses. It was"; N
130 END
200 PRINT "Got it in"; T; "tries!"
210 END
```

Load and run either with:

```
basic hello.bas
RUN
```

---

## 6. Error Handling

When a runtime or syntax error occurs, BASIC prints a brief message and the
line number on which it happened, then returns to the REPL prompt.  The
program and all variables are preserved.

| Message               | Meaning                                                |
|-----------------------|--------------------------------------------------------|
| `Syntax error in N`   | Could not tokenise or parse the line                    |
| `Type mismatch in N`  | Wrong type in an expression (e.g. number where string expected) |
| `Undefined line N`    | `GOTO`/`GOSUB`/`THEN` target line does not exist        |
| `Out of memory`       | Program buffer or variable storage exhausted            |
| `Out of data`         | `READ` past end of `DATA` (reserved)                    |
| `Division by zero`    | `/` or `MOD` with zero divisor                          |
| `Illegal function call` | Argument out of range for a function                  |
| `Disk error`          | `LOAD`/`SAVE` failed (see serial debug for FS error code)|

Internally, all error paths jump to a central `bas_error` trampoline that
restores the REPL stack frame and resumes input — no `iret`-from-nowhere
hazards.

---

## 7. Internals

Layout (all relative to TPA load address):

```
Source modules (src/programs/basic/):
  basic.asm          Entry point, REPL loop, MNEX header
  basic_data.inc     Fixed-address layout: program buffer, variable table,
                     for-stack, gosub-stack, error trampoline state
  basic_tokens.inc   Keyword / function / operator token IDs and spellings
  basic_lex.inc      Tokenizer (one-pass; numbers and strings stored inline)
                     and detokenizer for LIST
  basic_err.inc      Central error path, ERR/ERL, message table
  basic_edit.inc     Readline + program-line list operations (insert/replace/delete)
  basic_load.inc     LOAD / SAVE (via mn_save_file) / FILES
  basic_var.inc      Variable storage (A-Z scalars + arrays + strings)
  basic_expr.inc     Pratt-style expression evaluator (int + string)
  basic_stmt.inc     Statement dispatcher + handler set
```

### 7.1 Tokenisation

Each line is tokenised once on entry.  Numeric literals become a 1-byte
`TOK_INT_LIT` followed by a 2-byte little-endian value.  Line references
(targets of `GOTO`/`GOSUB`/`THEN`) become `TOK_LINEREF` + line number, which
makes the runtime branch fast.  Strings become `TOK_STR_LIT` + length + bytes.
Variable names are stored as `TOK_VAR_NAME` + single ASCII letter (+ `$` for
string vars).  Keywords and operators each have their own token ID
(see `basic_tokens.inc`).

`LIST` reverses this with a small per-token print table.

### 7.2 Program Storage

The program is stored as a sorted linked list of line records:

```
[ 2 bytes link offset ][ 2 bytes line number ][ tokens... ][ 0x00 ]
```

`link offset` is the byte offset (within the program buffer) of the next line
record; zero means end-of-program.  This makes `LIST`, `LOAD`/`SAVE`, and line
insertion all simple linear walks.

### 7.3 FOR / GOSUB Stacks

`FOR` pushes a record on the for-stack: `{ var_addr, end_val, step_val,
loop_top_offset }`.  `NEXT` peeks the top, applies STEP, compares with end,
either falls through (loop done) or jumps back to `loop_top_offset`.  The
matching `NEXT [var]` form looks up the stack until it finds a record whose
`var_addr` matches.

`GOSUB` pushes the *return offset* (offset within program buffer of the
statement after the `GOSUB`) on the gosub-stack; `RETURN` pops and jumps.

### 7.4 The bas_error Trampoline

A `setjmp`-style mechanism implemented entirely in 16-bit asm: at REPL entry,
`bas_repl_sp` is saved.  Any error path does:

```
mov sp, [bas_repl_sp]
jmp bas_repl_resume
```

— so the deep handler stack is unwound in one shot and control resumes at the
prompt.  This is what avoids the triple-fault-on-LOAD failure pattern.

### 7.5 SAVE

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

## 8. Known Limits

| Limit                            | Value         |
|----------------------------------|---------------|
| Max program size                 | ~6 KB tokens  |
| Max string length                | ~80 bytes     |
| Numeric range                    | 16-bit signed |
| Variables                        | A-Z scalars + A$-Z$ strings + DIM arrays |
| FOR/NEXT nesting                 | 8 deep        |
| GOSUB nesting                    | 16 deep       |
| Line numbers                     | 1 … 65535     |
| `.BAS` files in `FILES`          | Up to MNFS dir capacity (15 entries total disk-wide) |

No floating-point, no `WHILE/WEND` runtime (token reserved), no file I/O
beyond `LOAD`/`SAVE` (`OPEN`/`CLOSE` tokens reserved for future use),
no `DATA/READ/RESTORE` (reserved).

---

## 9. References

- `src/programs/basic/basic.asm` — module overview and includes
- `src/programs/basic/basic_data.inc` — runtime layout
- `doc/FILESYSTEM.md` §8.9 — `FS_REPLACE_FILE` syscall used by `SAVE`
- `src/include/mnoslib.inc` — `mn_save_file` / `mn_load_file` helpers
- `data/HELLO.BAS`, `data/GUESS.BAS` — sample programs (seeded onto VHD)
