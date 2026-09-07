#!/usr/bin/env python3
"""Compare the exported ABI surface of two libSgm shared objects.

The original IRIX library is MIPS n32 while the reconstructed library is a
native host ELF, so symbol sizes are informational by default. Name and symbol
type are the useful cross-architecture compatibility checks. Use --strict-size
only when comparing two builds for the same ABI.
"""

import argparse
import re
import subprocess
import sys


def exported_symbols(path):
    text = subprocess.check_output(
        ["readelf", "-Ws", path], text=True, errors="replace"
    )
    symbols = {}
    pattern = re.compile(
        r"^\s*\d+:\s+([0-9A-Fa-f]+)\s+(\S+)\s+(\S+)\s+"
        r"(GLOBAL|WEAK)\s+(\S+)\s+(\S+)\s+(.+)$"
    )

    for line in text.splitlines():
        match = pattern.match(line)
        if not match:
            continue

        _value, size, symbol_type, _binding, visibility, section, name = (
            match.groups()
        )
        if section == "UND":
            continue
        if visibility == "HIDDEN":
            continue

        name = name.split("@", 1)[0]
        try:
            parsed_size = int(size, 0)
        except ValueError:
            parsed_size = 0

        # Some ELF files expose the same symbol through more than one table.
        symbols[name] = (symbol_type, parsed_size)

    return symbols


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("original")
    parser.add_argument("reconstructed")
    parser.add_argument("--strict-size", action="store_true")
    args = parser.parse_args()

    original = exported_symbols(args.original)
    reconstructed = exported_symbols(args.reconstructed)

    missing = sorted(set(original) - set(reconstructed))
    extra = sorted(set(reconstructed) - set(original))
    type_mismatch = []
    size_mismatch = []

    for name in sorted(set(original) & set(reconstructed)):
        original_type, original_size = original[name]
        rebuilt_type, rebuilt_size = reconstructed[name]
        if original_type != rebuilt_type:
            type_mismatch.append(
                (name, original_type, rebuilt_type)
            )
        if args.strict_size and original_size != rebuilt_size:
            size_mismatch.append(
                (name, original_size, rebuilt_size)
            )

    print("original exports:     %d" % len(original))
    print("reconstructed exports: %d" % len(reconstructed))
    print("missing:              %d" % len(missing))
    print("extra:                %d" % len(extra))
    print("type mismatches:      %d" % len(type_mismatch))
    if args.strict_size:
        print("size mismatches:      %d" % len(size_mismatch))

    if missing:
        print("\nMISSING")
        for name in missing:
            print("  " + name)

    if type_mismatch:
        print("\nTYPE MISMATCH")
        for name, original_type, rebuilt_type in type_mismatch:
            print("  %s: original=%s rebuilt=%s" %
                  (name, original_type, rebuilt_type))

    if size_mismatch:
        print("\nSIZE MISMATCH")
        for name, original_size, rebuilt_size in size_mismatch:
            print("  %s: original=%d rebuilt=%d" %
                  (name, original_size, rebuilt_size))

    if extra:
        print("\nEXTRA")
        for name in extra:
            print("  " + name)

    # Extra exports are harmless during bring-up. Missing exports and type
    # mismatches are the compatibility failures.
    return 1 if missing or type_mismatch or size_mismatch else 0


if __name__ == "__main__":
    sys.exit(main())
