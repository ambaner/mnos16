# Contributing to Mini-OS

Thanks for your interest in contributing! This is an educational project building an OS from scratch, and contributions of all kinds are welcome.

## How to Contribute

### Reporting Bugs

1. Open an [Issue](../../issues/new?template=bug_report.md) using the bug report template.
2. Include steps to reproduce, expected vs. actual behavior, and your environment.
3. If the bug involves boot behavior, describe what appears on the VM console.

### Suggesting Features

1. Open an [Issue](../../issues/new?template=feature_request.md) using the feature request template.
2. Describe the feature and why it would be valuable.
3. Link to relevant OS-dev resources if applicable.

### Submitting Code Changes

1. **Fork** the repository.
2. **Create a branch** from `main`:
   ```bash
   git checkout -b feature/your-feature-name
   ```
3. **Make your changes** following the guidelines below.
4. **Build and test**:
   ```cmd
   build.bat
   ```
   The build runs the asm-lint pass on `src/programs/basic/` and the full
   unit-test suite (~338 tests) by default. Verify the VHD also boots
   correctly in Hyper-V — `setup-vm.bat` updates the VM image in-place.
5. **Commit** with a clear, descriptive message:
   ```
   Add second-stage bootloader with disk read
   ```
6. **Push** and open a **Pull Request** against `main`.

## Coding Guidelines

### Assembly (src/boot/)

- **NASM syntax** — Intel-style, `[BITS 16]` / `[BITS 32]` directives
- **4-space indentation**, no tabs
- **Comment non-obvious instructions** — especially BIOS interrupts and hardware I/O
- **Label naming**: `snake_case` for routines, `.local` for local labels

### PowerShell Scripts (tools/)

- **PowerShell 7+** required — all scripts include `#Requires -Version 7.0`
- **4-space indentation**
- Use `Write-Step` for user-facing status messages
- Fail fast with `$ErrorActionPreference = 'Stop'`

### File Organization

- **Boot code** (`src/boot/`) — MBR, second-stage loader, real-mode utilities
- **Tools** (`tools/`) — build scripts, VHD creation, VM setup
- **Documentation** (`doc/`) — design document, architecture diagrams

### Adding New Files

1. Add source files to the appropriate `src/` subdirectory.
2. Update `tools/build.ps1` to include the new file in the build.
3. Update `doc/DESIGN.md` (or the relevant deep-dive doc) with
   architecture changes.
4. Update `CHANGELOG.md` under an `[Unreleased]` section.
5. If you added a new shipped `.MNX` user program, also update
   `tests/test_mnx_size_budgets.py`, `.github/workflows/build.yml`
   (verification + artifact upload), and `.github/workflows/release.yml`
   (staging + README.txt) so CI guards and release packages stay in
   sync.
6. If you touched `src/programs/basic/`, the asm-lint runs automatically;
   fix any reported issues or annotate per-label (`;@stack-merge`,
   `;@no-stack-check`, `;@owner`, `;@returns cf`, `;@noreturn`) as
   appropriate.

## Architecture Overview

```
src/boot/mbr.asm  →  NASM  →  build/boot/mbr.bin  →  create-vhd  →  build/boot/mnos16.vhd
                                                                            ↓
                                                                      setup-vm.ps1
                                                                            ↓
                                                                      Hyper-V VM
```

See [doc/DESIGN.md](doc/DESIGN.md) for the full architecture documentation.

## What Makes a Good Pull Request

- **Focused** — one feature or fix per PR
- **Tested** — verified to boot in Hyper-V
- **Documented** — updates DESIGN.md and CHANGELOG.md if needed
- **Builds clean** — `build.bat` succeeds with no errors

## Questions?

Open a [Discussion](../../discussions) or file an Issue. Happy hacking! 💾
