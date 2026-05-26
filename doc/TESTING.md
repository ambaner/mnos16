# Unit Testing — Design Document

**Version:** 2.1  
**Status:** Tier 1 implemented (v0.9.13); 176 tests across 10 modules; branch coverage + trend tracking  
**Prerequisite:** Python 3.9+, `pip install -r tests/requirements.txt` (unicorn, pytest, capstone)

---

## 1. Motivation

MNOS16 is a bare-metal 16-bit OS with no standard test harness available at
runtime.  Traditional unit testing frameworks (xUnit, Google Test, etc.) cannot
run directly on the target — there is no hosted C runtime, no linker, and no
loader.

We need a testing strategy that:

- **Catches regressions** in pure-logic routines (argument parsing, string
  comparison, filename parsing) before they reach a VM
- **Runs in CI/CD** on every push and PR (GitHub Actions)
- **Produces coverage reports** published to GitHub Pages
- **Works locally** via `build.ps1` or a standalone `pytest` command
- **Scales** from current unit tests to future integration tests

---

## 2. Test Strategy — Three Tiers

Each tier builds on the previous.  Only **Tier 1** is implemented now; the
others are documented as future work.

```
┌──────────────────────────────────────────────────────────────────────┐
│  Tier 3: Integration Tests        (QEMU headless boot)             │
│  Full boot → shell commands → serial log assertions                │
├──────────────────────────────────────────────────────────────────────┤
│  Tier 2: Syscall-Level Tests      (Unicorn + INT hooks)            │
│  Emulate kernel dispatcher, test syscall handlers end-to-end       │
├──────────────────────────────────────────────────────────────────────┤
│  Tier 1: Pure-Logic Unit Tests    (Unicorn CPU emulator) ← NOW     │
│  Test individual routines in isolation — no hardware, no INTs      │
└──────────────────────────────────────────────────────────────────────┘
```

### 2.1 Tier 1 — Pure-Logic Unit Tests (Current)

**Approach:** Assemble individual routines into flat binaries, load them into
the [Unicorn Engine](https://www.unicorn-engine.org/) (a lightweight x86 CPU
emulator), execute them in emulated 16-bit real mode, and assert on register
and memory state.

**What it tests:**
- Routines that are pure computation — no `INT` instructions, no port I/O,
  no hardware access
- Input/output is entirely through registers and memory

**Testable routines (current):**

| Routine | File | What it does |
|---------|------|-------------|
| `shell_parse_args` | `shell_parse_args.inc` | Tokenize args into argc/argv table |
| `run_parse_filename` | `shell_cmd_run.inc` | Parse "filename.ext args" into 8.3 + args ptr |
| `strcmp` | `shell_readline.inc` | Compare two NUL-terminated strings |
| `cmdmatch` | `shell_readline.inc` | Prefix-match command against string table |
| `mm_alloc` | `mm.asm` | First-fit heap allocation with block splitting |
| `mm_free` | `mm.asm` | Free block + forward coalescing |
| `mm_avail` | `mm.asm` | Report largest and total free memory |
| `mm_info` | `mm.asm` | Report total/used/free/block-count statistics |
| `fs_write_file` | `fs.asm` | Create/overwrite files in MNFS directory |
| `fs_delete_file` | `fs.asm` | Tombstone-delete files (system-file protection) |
| `fs_rename_file` | `fs.asm` | Rename files (duplicate-name check) |
| `ed_gap_insert` | `edit_gap.inc` | Gap buffer insert character |
| `ed_gap_delete_back` | `edit_gap.inc` | Gap buffer delete backward |
| `ed_gap_delete_fwd` | `edit_gap.inc` | Gap buffer delete forward |
| `ed_gap_move_to` | `edit_gap.inc` | Move gap to arbitrary offset |
| `ed_search_text` | `edit_find.inc` | Linear search through gap buffer |
| `ed_get_char_at_offset` | `edit_find.inc` | Gap-aware character access |
| `ed_atoi` | `edit_find.inc` | ASCII decimal string to integer |
| `ed_parse_8_3` | `edit_file.inc` | Parse filename into 8.3 format for FS |

**How it works:**

1. **Assemble** — A small NASM wrapper (`tests/harness/`) includes the routine
   under test and adds a `hlt` instruction at the end.  This produces a flat
   binary with no headers.

2. **Load** — The Python test loads the binary into Unicorn at a fixed address,
   sets up memory (input strings, buffer areas), and configures registers.

3. **Execute** — Unicorn runs the code until it hits `hlt` (or a cycle limit).

4. **Assert** — The test reads registers and memory to verify the routine
   produced the expected output.

5. **Coverage** — An instruction-level hook records every address executed
   and every (from → to) edge transition.  After all tests complete, the
   coverage collector compares executed addresses against the binary size
   for statement coverage, and uses Capstone disassembly to identify
   conditional branches for branch coverage (taken vs. fall-through).

6. **Trend Tracking** — Each CI run appends a record to `history.json`
   (last 50 entries).  A Chart.js trend page (`trend.html`) is deployed
   to GitHub Pages alongside the coverage dashboard.

**Example test flow:**

```python
def test_parse_args_two_words():
    """'hello world' → argc=2, argv[0]='hello', argv[1]='world'"""
    emu = MiniOSEmulator()
    emu.write_string(0x5000, "hello world")
    emu.write_word(SHELL_ARGS_PTR, 0x5000)
    emu.run("shell_parse_args")
    assert emu.read_byte(ARGV_ARGC) == 2
    assert emu.read_string(emu.read_word(ARGV_PTRS + 0)) == "hello"
    assert emu.read_string(emu.read_word(ARGV_PTRS + 2)) == "world"
```

### 2.2 Tier 2 — Syscall-Level Tests (Future)

**Approach:** Extend the Unicorn harness to hook `INT 0x80` instructions.
When the emulated code triggers a software interrupt, the hook dispatches
to the real syscall handler code (also loaded into Unicorn).

**What it tests:**
- Syscall argument validation (bad index → CF set)
- Syscall return values (argc, argv pointers)
- Jump table wiring (correct handler reached for each AH value)

**What it requires:**
- INT hook implementation in the emulator wrapper
- Loading the full kernel syscall dispatcher binary
- Mocking hardware-dependent syscalls (disk, video) with stubs

### 2.3 Tier 3 — Integration Tests (Future)

**Approach:** Boot the full OS image in QEMU headless mode, inject keystrokes
via the QEMU monitor protocol (QMP), and assert on serial port output.

**What it tests:**
- Full boot chain (MBR → VBR → LOADER → KERNEL → SHELL)
- Shell commands produce expected output
- Program loading and execution
- Syscall traces in debug builds (serial log)

**What it requires:**
- QEMU installed on CI runner (available on `ubuntu-latest`)
- QMP scripting to send keystrokes
- Serial log capture and parsing
- Longer CI times (~30s per boot cycle)

---

## 3. Architecture

### 3.1 Directory Structure

```
tests/
├── conftest.py              # pytest fixtures, shared emulator setup
├── gen_constants.py         # Auto-generates constants.py from .inc files
├── harness/
│   ├── emulator.py          # MiniOSEmulator class (Unicorn wrapper + edge tracking)
│   ├── assembler.py         # NASM assembly helper (build test binaries)
│   ├── coverage.py          # Coverage report generator (stmt + branch + trend)
│   └── constants.py         # Auto-generated from memory.inc + syscalls.inc + mnfs.inc
├── stubs/
│   ├── stub_parse_args.asm  # Harness: includes shell_parse_args.inc + hlt
│   ├── stub_parse_fname.asm # Harness: includes run_parse_filename + hlt
│   ├── stub_strcmp.asm       # Harness: includes strcmp + hlt
│   ├── stub_cmdmatch.asm    # Harness: includes cmdmatch + hlt
│   ├── stub_mm.asm          # Harness: MM allocator routines + hlt
│   ├── stub_fs_write.asm    # Harness: FS write/delete/rename + hlt
│   ├── stub_edit_gap.asm    # Harness: gap buffer ops + hlt
│   ├── stub_edit_find.asm   # Harness: search/char_at/atoi + hlt
│   └── stub_edit_fname.asm  # Harness: editor filename parsing + hlt
├── test_parse_args.py       # 15 tests for shell_parse_args
├── test_parse_filename.py   # 9 tests for run_parse_filename
├── test_strcmp.py            # 11 tests for strcmp
├── test_cmdmatch.py         # 12 tests for cmdmatch (prefix matching)
├── test_mm.py               # 29 tests for mm_alloc/free/avail/info
├── test_fs_write.py         # 26 tests for fs write/delete/rename
├── test_edit_gap.py         # 27 tests for gap buffer operations
├── test_edit_find.py        # 19 tests for search/char_at_offset/atoi
├── test_edit_fname.py       # 12 tests for editor 8.3 filename parsing
├── test_memory_layout.py    # 16 tests for memory layout consistency (pure Python)
└── requirements.txt         # unicorn, pytest, capstone
```

### 3.2 Test Harness Design

The `MiniOSEmulator` class wraps Unicorn to provide an MNOS16-aware API:

```python
class MiniOSEmulator:
    def __init__(self):
        # Create 16-bit real mode x86 emulator
        # Map 1 MB of memory (0x0000–0xFFFFF)
        # Set SP to 0xFFF0
        # Install instruction-level coverage hook

    def load(self, binary_path, base=0x1000):
        # Load flat binary at base address
        # Record entry/end addresses for coverage

    def run(self, entry=None, timeout_us=1_000_000):
        # Execute from entry until HLT or timeout
        # Raise on timeout (infinite loop detection)

    def write_string(self, addr, s):
        # Write NUL-terminated ASCII string to memory

    def read_string(self, addr, max_len=256):
        # Read NUL-terminated string from memory

    def read_byte(self, addr) / write_byte(self, addr, val)
    def read_word(self, addr) / write_word(self, addr, val)
```

### 3.3 NASM Stub Pattern

Each stub file is minimal — it sets up the include path, defines any
dependencies the routine needs (e.g., data labels), includes the routine,
and ends with `hlt`:

```nasm
; stub_parse_args.asm — Test harness for shell_parse_args
[bits 16]
[org 0x1000]

%include "memory.inc"       ; ARGV_* constants, SHELL_ARGS_PTR
%include "syscalls.inc"     ; SYS_* constants (needed by some includes)

entry:
    call shell_parse_args
    hlt

%include "shell_parse_args.inc"
```

The stub is assembled with:
```
nasm -f bin -I src/include/ -I src/shell/ -o tests/bin/parse_args.bin tests/stubs/stub_parse_args.asm
```

### 3.4 Coverage Collection

Coverage works at the instruction-address level:

1. **During tests:** A Unicorn `UC_HOOK_CODE` callback records every
   instruction address executed across all tests for a given binary.

2. **After tests:** The coverage module parses the NASM listing file
   (`.lst`, generated with `nasm -l`) which maps addresses to source lines.

3. **Report generation:**
   - Per-routine coverage percentage
   - Overall coverage percentage  
   - Line-by-line hit/miss in HTML report
   - JSON summary for badge generation
   - GitHub Actions Job Summary (markdown table)

4. **GitHub Pages deployment:** The HTML report is published to
   `https://<user>.github.io/mini-os/coverage/`

---

## 4. CI/CD Integration

### 4.1 GitHub Actions Workflow

The existing `build.yml` gains a new `test` job that runs after `build`:

```yaml
test:
  runs-on: ubuntu-latest
  needs: build
  steps:
    - uses: actions/checkout@v4
    - uses: actions/setup-python@v5
      with: { python-version: '3.12' }
    - uses: ilammy/setup-nasm@v1
    - run: pip install -r tests/requirements.txt
    - run: python -m pytest tests/ -v --tb=short --html=coverage/report.html
    - run: python tests/harness/coverage.py --output coverage/
    # Upload coverage report
    - uses: actions/upload-artifact@v4
      with: { name: coverage-report, path: coverage/ }
    # Deploy to GitHub Pages (main branch only)
    - uses: peaceiris/actions-gh-pages@v3
      if: github.ref == 'refs/heads/main'
      with:
        github_token: ${{ secrets.GITHUB_TOKEN }}
        publish_dir: ./coverage
```

### 4.2 Local Execution

```powershell
# Run tests locally (after pip install -r tests/requirements.txt)
python -m pytest tests/ -v

# Run with coverage report
python -m pytest tests/ -v --html=coverage/report.html
python tests/harness/coverage.py --output coverage/
```

The build script (`tools/build.ps1`) will optionally run tests when Python
and the required packages are available, but will not fail the build if
Python is not installed (graceful degradation).

### 4.3 Coverage Badge

The coverage report generates a JSON file consumed by shields.io:

```markdown
![Coverage](https://img.shields.io/endpoint?url=https://USER.github.io/MNOS16/coverage/badge.json)
```

---

## 5. Test Matrix

### 5.1 shell_parse_args (Tier 1 — Current)

| # | Test Case | Input | Expected |
|---|-----------|-------|----------|
| 1 | No args (NULL ptr) | SHELL_ARGS_PTR=0 | argc=0 |
| 2 | No args (empty string) | "" | argc=0 |
| 3 | Single arg | "hello" | argc=1, argv[0]="hello" |
| 4 | Two args | "hello world" | argc=2 |
| 5 | Multiple spaces | "a   b   c" | argc=3, spaces collapsed |
| 6 | Leading spaces | "  hello" | argc=1, argv[0]="hello" |
| 7 | Trailing spaces | "hello  " | argc=1, argv[0]="hello" |
| 8 | Tab separator | "a\tb" | argc=2 |
| 9 | Quoted string | '"hello world" foo' | argc=2, argv[0]="hello world" |
| 10 | Quoted at end | 'foo "bar baz"' | argc=2, argv[1]="bar baz" |
| 11 | Unterminated quote | '"hello world' | argc=1, argv[0]="hello world" |
| 12 | Empty quotes | '"" foo' | argc=2, argv[0]="" |
| 13 | Max args (15) | "1 2 3 ... 15" | argc=15 |
| 14 | Overflow (16+) | "1 2 3 ... 16 17" | argc=15 (truncated) |
| 15 | Storage overflow | Very long args | argc=partial, no crash |

### 5.2 run_parse_filename (Tier 1 — Current)

| # | Test Case | Input | Expected |
|---|-----------|-------|----------|
| 1 | Simple name | "hello" | fname="HELLO   ", no ext |
| 2 | Name + ext | "hello.mnx" | fname="HELLO   MNX", ext_provided=1 |
| 3 | Uppercase passthrough | "HELLO.MNX" | fname="HELLO   MNX" |
| 4 | Mixed case | "HeLLo.MnX" | fname="HELLO   MNX" |
| 5 | Name + args | "test foo bar" | fname="TEST    ", args→"foo bar" |
| 6 | Name.ext + args | "test.mnx foo" | fname="TEST    MNX", args→"foo" |
| 7 | Long name (>8) | "longfilename.mnx" | fname="LONGFILE MNX" (truncated) |
| 8 | Long ext (>3) | "test.abcd" | fname="TEST    ABC" (truncated) |
| 9 | Max padded | "12345678.123" | fname="12345678123" |

### 5.3 strcmp (Tier 1 — Current)

| # | Test Case | Input | Expected |
|---|-----------|-------|----------|
| 1 | Equal strings | "hello", "hello" | ZF set |
| 2 | Different strings | "hello", "world" | ZF clear |
| 3 | Prefix mismatch | "hello", "help" | ZF clear |
| 4 | Empty strings | "", "" | ZF set |
| 5 | One empty | "a", "" | ZF clear |
| 6 | Case sensitive | "Hello", "hello" | ZF clear |

---

## 6. Coverage Targets

### 6.1 Current Testable Code (Tier 1)

| Module | Routine | Tests | Status |
|--------|---------|-------|--------|
| `shell_parse_args.inc` | `shell_parse_args` | 15 | ✅ Tested |
| `shell_cmd_run.inc` | `run_parse_filename` | 9 | ✅ Tested |
| `shell_readline.inc` | `strcmp` | 11 | ✅ Tested |
| `shell_readline.inc` | `cmdmatch` | 12 | ✅ Tested |
| `mm.asm` | `mm_alloc/free/avail/info` | 29 | ✅ Tested |
| `fs.asm` | `fs_write/delete/rename` | 26 | ✅ Tested |
| `edit_gap.inc` | `gap insert/delete/move` | 27 | ✅ Tested |
| `edit_find.inc` | `search/char_at/atoi` | 19 | ✅ Tested |
| `edit_file.inc` | `ed_parse_8_3` | 12 | ✅ Tested |
| `memory.inc` | layout consistency (pure Python) | 16 | ✅ Tested |
| | **Total** | **176** | |

### 6.2 Future Testable Code (Tier 2)

| Module | Routine | Lines | Blocker |
|--------|---------|-------|---------|
| `kernel_syscall.inc` | `fn_get_argc` | ~10 | Needs INT hook |
| `kernel_syscall.inc` | `fn_get_argv` | ~30 | Needs INT hook |
| `kernel_syscall.inc` | `fn_get_args` | ~20 | Needs INT hook |
| `kernel_syscall.inc` | `fn_get_version` | ~8 | Needs INT hook |
| `kernel_syscall.inc` | `fn_get_bib` | ~15 | Needs memory setup |

### 6.3 Integration-Only Code (Tier 3)

| Module | What | Blocker |
|--------|------|---------|
| Boot chain | MBR → VBR → LOADER | Needs disk + BIOS |
| FS module | Directory listing, file read | Needs disk I/O |
| Shell | Command dispatch, readline | Needs keyboard + video |
| MNMON | All commands | Needs full OS running |
| EDIT (UI) | Menu system, dialogs, rendering | Needs VGA + keyboard |

---

## 7. Design Decisions

### 7.1 Why Unicorn Engine?

| Alternative | Why not |
|-------------|---------|
| Run in QEMU | Too slow for unit tests, hard to assert on individual routines |
| Build as COM file + DOSBox | Requires DOS runtime, not CI-friendly |
| Rewrite in C + test | Duplicates logic, tests diverge from real code |
| **Unicorn** | **Fast, Python bindings, CI-friendly, tests real assembled bytes** |

### 7.2 Why Assemble Stubs?

Rather than loading the entire shell binary and jumping to an offset, we
assemble minimal stubs that include only the routine under test plus its
dependencies.  This:

- Eliminates coupling to binary layout (offsets change as code evolves)
- Makes test setup simpler (fewer memory regions to initialize)
- Produces smaller binaries for faster coverage analysis
- Allows testing routines that share label names across files

### 7.3 Why Not Test Everything?

Many routines are tightly coupled to hardware:

- `fn_read_sector` calls `INT 0x13` (BIOS disk services)
- `fn_print_string` calls `INT 0x10` (BIOS video services)
- `fn_reboot` jumps to `0xFFFF:0x0000`

These cannot be meaningfully tested without a full BIOS emulation layer.
Tier 3 (QEMU integration tests) covers these through end-to-end testing.

---

## 8. Maintenance

### 8.1 Adding a New Testable Routine

1. Create a stub in `tests/stubs/stub_<name>.asm`
2. Add test file `tests/test_<name>.py`
3. Run `pytest` locally to verify
4. The CI pipeline auto-discovers new test files

### 8.2 Coverage Regressions

The CI job can optionally enforce a minimum coverage threshold:

```yaml
- run: python tests/harness/coverage.py --min-coverage 80
```

If coverage drops below the threshold, the job fails.

---

## 9. Dependencies

| Package | Version | Purpose |
|---------|---------|---------|
| `unicorn` | ≥2.0 | x86 CPU emulator |
| `capstone` | ≥5.0 | Disassembler for branch coverage analysis |
| `pytest` | ≥7.0 | Test runner |
| `pytest-html` | ≥4.0 | HTML test report |
| NASM | ≥2.15 | Assembler (already in CI) |

All are pip-installable and available on GitHub Actions runners.
