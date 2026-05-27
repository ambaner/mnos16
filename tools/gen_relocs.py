#!/usr/bin/env python3
"""gen_relocs.py — Delta-comparison relocation table generator for MNOS16.

Generates relocation entries for MNOS16 system modules by assembling a module
at two different base addresses (ORG 0 and ORG 0x100) and comparing the output.
Every 16-bit word location where the difference equals the base delta (0x100)
is an absolute reference that needs load-time patching.

Usage:
    python gen_relocs.py <source.asm> [--nasm PATH] [--include DIR] [--define MACRO]
                         [--header-size N] [--output FILE] [--verbose]

The tool:
  1. Assembles the module with -DRELOC_BASE=0x0000 → binary_base0
  2. Assembles the module with -DRELOC_BASE=0x0100 → binary_base1
  3. Compares word-by-word (skipping the header region)
  4. Emits a sorted list of 16-bit offsets where fixups are needed
  5. Outputs as binary (for embedding) or text (for inspection)

The source module must use: [ORG RELOC_BASE] instead of a hardcoded ORG.

Requirements:
  - NASM must be available (default: tools/nasm/nasm.exe or nasm on PATH)
  - Module must not use split-byte address references (db label & 0xFF)
  - Module must not use 32-bit absolute label references (dd label)
"""

import argparse
import struct
import subprocess
import sys
import tempfile
from pathlib import Path


# Delta between the two ORG values used for comparison
DELTA = 0x0100

# Base addresses for the two passes
BASE_LO = 0x0000
BASE_HI = BASE_LO + DELTA


def assemble(nasm: str, source: Path, base: int, includes: list,
             defines: list, output: Path) -> bool:
    """Assemble source with a given RELOC_BASE value."""
    cmd = [
        nasm, '-f', 'bin',
        f'-DRELOC_BASE={base:#06x}',
        '-o', str(output),
    ]
    for inc in includes:
        cmd.extend(['-I', str(inc)])
    for d in defines:
        cmd.extend(['-D', d])
    cmd.append(str(source))

    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"NASM error (base={base:#06x}):", file=sys.stderr)
        print(result.stderr, file=sys.stderr)
        return False
    return True


def find_relocations(bin_lo: bytes, bin_hi: bytes, header_size: int,
                     verbose: bool = False) -> list:
    """Compare two binaries and find 16-bit relocation offsets.

    Scans from header_size to end, checking each 16-bit word position.
    A relocation is detected when: word_hi - word_lo == DELTA.

    Returns sorted list of offsets (relative to start of binary, not payload).
    """
    if len(bin_lo) != len(bin_hi):
        print(f"WARNING: Binary sizes differ ({len(bin_lo)} vs {len(bin_hi)}). "
              f"Using shorter length.", file=sys.stderr)

    length = min(len(bin_lo), len(bin_hi))
    relocs = []

    # Scan every byte position (relocs can be at odd offsets in theory,
    # but x86-16 absolute references in instructions are always word-aligned
    # relative to the instruction, though not necessarily to the file)
    for i in range(header_size, length - 1):
        word_lo = struct.unpack_from('<H', bin_lo, i)[0]
        word_hi = struct.unpack_from('<H', bin_hi, i)[0]
        diff = (word_hi - word_lo) & 0xFFFF

        if diff == DELTA:
            # Verify this isn't a false positive by checking surrounding bytes
            # haven't also shifted (which would indicate a 32-bit reference)
            is_32bit = False
            if i + 3 < length:
                dword_lo = struct.unpack_from('<I', bin_lo, i)[0]
                dword_hi = struct.unpack_from('<I', bin_hi, i)[0]
                if (dword_hi - dword_lo) & 0xFFFFFFFF == DELTA:
                    # Upper 16 bits also differ — might be 32-bit ref
                    # But in 16-bit code, only the low word matters
                    pass

            # Check we're not overlapping with a previous relocation
            if relocs and relocs[-1] >= i - 1:
                if verbose:
                    print(f"  OVERLAP at {i:#06x} (prev={relocs[-1]:#06x}), "
                          f"skipping", file=sys.stderr)
                continue

            relocs.append(i)
            if verbose:
                print(f"  RELOC at offset {i:#06x}: "
                      f"{word_lo:#06x} → {word_hi:#06x}", file=sys.stderr)

    return relocs


def validate_relocs(relocs: list, bin_lo: bytes, header_size: int) -> list:
    """Filter out likely false positives."""
    # A relocation at offset i means bin_lo[i:i+2] should be a reasonable
    # address (0x0000..0xFFFF relative to module start). Since BASE_LO=0,
    # the value at that position in bin_lo is the zero-based address.
    # Filter: value should be >= header_size (pointing into code/data)
    # and < len(bin_lo) (pointing within the module)
    validated = []
    for offset in relocs:
        value = struct.unpack_from('<H', bin_lo, offset)[0]
        if value < len(bin_lo):
            validated.append(offset)
        else:
            print(f"  WARNING: Reloc at {offset:#06x} points to {value:#06x} "
                  f"(outside module, {len(bin_lo)} bytes). Keeping anyway.",
                  file=sys.stderr)
            validated.append(offset)
    return validated


def write_binary_relocs(relocs: list, output: Path):
    """Write relocation table as binary (array of 16-bit LE words)."""
    with open(output, 'wb') as f:
        for offset in relocs:
            f.write(struct.pack('<H', offset))


def write_text_relocs(relocs: list, output):
    """Write relocation table as text (one hex offset per line)."""
    for offset in relocs:
        output.write(f"{offset:#06x}\n")


def main():
    parser = argparse.ArgumentParser(
        description='Generate relocation table for MNOS16 system modules')
    parser.add_argument('source', type=Path,
                        help='NASM source file (must use [ORG RELOC_BASE])')
    parser.add_argument('--nasm', default=None,
                        help='Path to NASM executable')
    parser.add_argument('-I', '--include', action='append', default=[],
                        dest='includes', help='Include directory (repeatable)')
    parser.add_argument('-D', '--define', action='append', default=[],
                        dest='defines', help='NASM define (repeatable)')
    parser.add_argument('--header-size', type=int, default=12,
                        help='Bytes to skip at start (v2 header, default=12)')
    parser.add_argument('-o', '--output', type=Path, default=None,
                        help='Output file (binary .rel format)')
    parser.add_argument('--text', action='store_true',
                        help='Output as text instead of binary')
    parser.add_argument('-v', '--verbose', action='store_true',
                        help='Print each detected relocation')
    args = parser.parse_args()

    # Find NASM
    if args.nasm:
        nasm = args.nasm
    else:
        # Try local tools/nasm/nasm.exe first
        local_nasm = Path(__file__).parent / 'nasm' / 'nasm.exe'
        if local_nasm.exists():
            nasm = str(local_nasm)
        else:
            nasm = 'nasm'

    # Create temp files for the two assembled binaries
    with tempfile.NamedTemporaryFile(suffix='_base0.bin', delete=False) as f0, \
         tempfile.NamedTemporaryFile(suffix='_base1.bin', delete=False) as f1:
        path_lo = Path(f0.name)
        path_hi = Path(f1.name)

    try:
        # Assemble at BASE_LO
        if not assemble(nasm, args.source, BASE_LO, args.includes,
                        args.defines, path_lo):
            sys.exit(1)

        # Assemble at BASE_HI
        if not assemble(nasm, args.source, BASE_HI, args.includes,
                        args.defines, path_hi):
            sys.exit(1)

        # Read binaries
        bin_lo = path_lo.read_bytes()
        bin_hi = path_hi.read_bytes()

        if len(bin_lo) != len(bin_hi):
            print(f"ERROR: Binaries differ in size "
                  f"({len(bin_lo)} vs {len(bin_hi)}). "
                  f"Conditional code may depend on RELOC_BASE value.",
                  file=sys.stderr)
            sys.exit(1)

        print(f"Module size: {len(bin_lo)} bytes ({len(bin_lo)//512} sectors)")
        print(f"Header size: {args.header_size} bytes (skipped)")

        # Find relocations
        relocs = find_relocations(bin_lo, bin_hi, args.header_size,
                                  verbose=args.verbose)
        relocs = validate_relocs(relocs, bin_lo, args.header_size)

        print(f"Relocations found: {len(relocs)}")

        # Output
        if args.output:
            if args.text:
                with open(args.output, 'w') as f:
                    write_text_relocs(relocs, f)
            else:
                write_binary_relocs(relocs, args.output)
            print(f"Written to: {args.output}")
        else:
            # Print to stdout as text
            write_text_relocs(relocs, sys.stdout)

    finally:
        path_lo.unlink(missing_ok=True)
        path_hi.unlink(missing_ok=True)


if __name__ == '__main__':
    main()
