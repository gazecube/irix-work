#!/usr/bin/env python3
"""Verify native32 DSOs and decode coverage of their generated MIPS corpus."""
import argparse
import collections
import re
import subprocess
import sys
from pathlib import Path


def output(*args):
    return subprocess.check_output(args, text=True, stderr=subprocess.STDOUT)


def verify_sources(root):
    # Generated functions retain each source instruction in a comment. Audit
    # those words with the same fail-closed decoder used by the core builder.
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import translate_n32_core

    pattern = re.compile(r"/\* 0x([0-9a-fA-F]{8}): 0x([0-9a-fA-F]{8}) \*/")
    families = collections.Counter()
    count = 0
    for source in sorted(Path(root).rglob("recomp_*.c")):
        for line_number, line in enumerate(source.read_text(errors="replace").splitlines(), 1):
            match = pattern.search(line)
            if not match:
                continue
            address, word = (int(value, 16) for value in match.groups())
            try:
                translate_n32_core.decode(address, word)
            except Exception as error:
                raise SystemExit(
                    f"{source}:{line_number}: unsupported MIPS word "
                    f"0x{word:08x} at 0x{address:08x}: {error}") from error
            opcode = word >> 26
            if opcode == 0:
                family = f"SPECIAL/{word & 63:02x}"
            elif opcode == 1:
                family = f"REGIMM/{(word >> 16) & 31:02x}"
            elif opcode == 0x11:
                family = f"COP1/{(word >> 21) & 31:02x}/{word & 63:02x}"
            else:
                family = f"OP/{opcode:02x}"
            families[family] += 1
            count += 1
    if not count:
        raise SystemExit(f"{root}: no generated MIPS instruction comments found")
    print(f"verified MIPS decoder coverage: {count} words, {len(families)} encoding forms")


def main(argv):
    parser = argparse.ArgumentParser()
    parser.add_argument("--sources", type=Path,
                        help="generated C tree whose MIPS instruction comments should be audited")
    parser.add_argument("libraries", nargs="+", help="generated native32 DSOs")
    args = parser.parse_args(argv)
    for name in args.libraries:
        path = Path(name)
        header = output("readelf", "-h", str(path))
        undefined = output("nm", "-D", "-u", str(path))
        dynamic = output("readelf", "-d", str(path))
        if "Class:                             ELF32" not in header:
            raise SystemExit(f"{path}: expected ELF32 output")
        if "Machine:                           Intel 80386" not in header:
            raise SystemExit(f"{path}: expected Intel i386 output")
        if "irix_mips_interp_call" in undefined:
            raise SystemExit(f"{path}: generated code calls the MIPS interpreter")
        if "libirixre.so.1" not in dynamic:
            raise SystemExit(f"{path}: expected current guest-memory/dispatch runtime")
        print(f"verified {path}: i386, no direct interpreter call, runtime dispatch retained")
    if args.sources:
        verify_sources(args.sources)


if __name__ == "__main__":
    main(sys.argv[1:])
