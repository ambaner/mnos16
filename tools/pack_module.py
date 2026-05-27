#!/usr/bin/env python3
"""pack_module.py — Package a relocatable MNOS16 system module.

Takes a raw assembled binary (ORG 0, no header) and a relocation table,
and produces a final .SYS file with the MNEX v2 header.

Usage:
    python pack_module.py <raw.bin> <relocs.rel> --magic MNFS -o fs.sys

Output format (MNEX v2):
    Offset 0:  4 bytes  magic
    Offset 4:  2 bytes  sector_count (total including header)
    Offset 6:  2 bytes  flags (0x0001 = has relocs)
    Offset 8:  2 bytes  reloc_count
    Offset 10: 2 bytes  entry_offset (from start of file to code entry)
    Offset 12: N*2 bytes relocation table
    Offset 12+N*2: raw binary payload (code + data)

Relocation semantics:
    Each reloc entry is a file-relative offset (from the start of the loaded
    image). At that offset, a 16-bit word holds an address that was assembled
    with ORG 0. pack_module pre-biases these values by adding prefix_size
    (= 12 + reloc_count * 2), so they become file-relative addresses.

    At load time, the kernel simply adds load_base to each relocated word:
        final_value = pre_biased_value + load_base
                    = (code_offset + prefix_size) + load_base
                    = offset_within_loaded_image + load_base
                    = physical_address ✓
"""

import argparse
import math
import struct
import sys
from pathlib import Path


MNEX_V2_FLAG_RELOC = 0x0001


def main():
    parser = argparse.ArgumentParser(
        description='Package MNOS16 relocatable system module')
    parser.add_argument('raw_bin', type=Path,
                        help='Raw assembled binary (ORG 0, no header)')
    parser.add_argument('relocs', type=Path,
                        help='Relocation table (.rel binary, array of u16 LE)')
    parser.add_argument('--magic', required=True,
                        help='4-char magic string (e.g., MNFS)')
    parser.add_argument('-o', '--output', type=Path, required=True,
                        help='Output .SYS file')
    parser.add_argument('--pad-sectors', action='store_true',
                        help='Pad output to exact sector boundary')
    args = parser.parse_args()

    # Read inputs
    raw = bytearray(args.raw_bin.read_bytes())
    rel_data = args.relocs.read_bytes()

    if len(args.magic) != 4:
        print(f"ERROR: Magic must be exactly 4 characters, got '{args.magic}'",
              file=sys.stderr)
        sys.exit(1)

    # Parse relocation table (array of 16-bit LE offsets into raw binary)
    if len(rel_data) % 2 != 0:
        print("ERROR: Relocation file size must be even", file=sys.stderr)
        sys.exit(1)

    reloc_count = len(rel_data) // 2
    raw_relocs = list(struct.unpack_from(f'<{reloc_count}H', rel_data))

    # Calculate prefix size (header + reloc table)
    header_fixed = 12
    reloc_table_size = reloc_count * 2
    prefix_size = header_fixed + reloc_table_size
    entry_offset = prefix_size  # Code starts immediately after reloc table

    # Pre-bias relocated values in the raw code.
    # Each relocation points to a 16-bit word in raw code that holds a
    # zero-based address. We add prefix_size so it becomes file-relative.
    # At load time, the kernel adds load_base for the final physical address.
    for raw_off in raw_relocs:
        if raw_off + 1 >= len(raw):
            print(f"ERROR: Reloc at raw offset {raw_off:#06x} exceeds "
                  f"raw binary size {len(raw)}", file=sys.stderr)
            sys.exit(1)
        value = struct.unpack_from('<H', raw, raw_off)[0]
        biased = (value + prefix_size) & 0xFFFF
        struct.pack_into('<H', raw, raw_off, biased)

    # Compute file-relative reloc entries (for the kernel's patch loop)
    file_relocs = [r + prefix_size for r in raw_relocs]

    # Calculate total size and sector count
    total_size = prefix_size + len(raw)
    sector_count = math.ceil(total_size / 512)

    # Build the output
    output = bytearray()

    # Header (12 bytes)
    output.extend(args.magic.encode('ascii'))            # magic (4)
    output.extend(struct.pack('<H', sector_count))       # sector_count (2)
    output.extend(struct.pack('<H', MNEX_V2_FLAG_RELOC)) # flags (2)
    output.extend(struct.pack('<H', reloc_count))        # reloc_count (2)
    output.extend(struct.pack('<H', entry_offset))       # entry_offset (2)

    # Relocation table (file-relative offsets)
    for r in file_relocs:
        output.extend(struct.pack('<H', r))

    # Raw binary payload (pre-biased)
    output.extend(raw)

    # Pad to sector boundary
    pad_needed = (sector_count * 512) - len(output)
    if pad_needed > 0:
        output.extend(b'\x00' * pad_needed)

    # Write output
    args.output.write_bytes(bytes(output))

    # Summary
    print(f"Module: {args.magic}")
    print(f"  Raw code size: {len(raw)} bytes")
    print(f"  Relocations: {reloc_count}")
    print(f"  Prefix size: {prefix_size} bytes (header={header_fixed} + relocs={reloc_table_size})")
    print(f"  Total size: {len(output)} bytes ({sector_count} sectors)")
    print(f"  Entry offset: {entry_offset:#06x}")
    print(f"  Output: {args.output}")


if __name__ == '__main__':
    main()

