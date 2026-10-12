#!/usr/bin/env python3
"""Create <outdir>/<name> symlinks (to irix-cmd) for every command-line
IRIX N32 program in the tree, so they can be run by name.

  gen-irix-bin.py [--tree DIR] [--out DIR] [--link-to PATH]

Put <outdir> at the END of PATH: host programs of the same name win."""
import argparse, os, struct, sys

DIRS = ["usr/sbin", "sbin", "usr/bin", "bin", "usr/bsd", "usr/etc", "etc"]
EF_MIPS_ABI2 = 0x20

def is_n32_exec(path):
    try:
        with open(path, "rb") as f:
            h = f.read(52)
    except OSError:
        return False
    if len(h) < 52 or h[:4] != b"\x7fELF" or h[4] != 1 or h[5] != 2:
        return False
    e_type, e_machine = struct.unpack(">HH", h[16:20])
    e_flags = struct.unpack(">I", h[36:40])[0]
    return e_type == 2 and e_machine == 8 and bool(e_flags & EF_MIPS_ABI2)

def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser()
    ap.add_argument("--tree", default=os.path.join(here, "..", "IRIX-TREE"))
    ap.add_argument("--out", default=os.path.join(here, "irix-bin"))
    ap.add_argument("--link-to", default=os.path.join(here, "irix-cmd"))
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    seen, made = set(), 0
    for d in DIRS:
        base = os.path.join(a.tree, d)
        if not os.path.isdir(base):
            continue
        for name in sorted(os.listdir(base)):
            p = os.path.join(base, name)
            if name in seen or not os.path.isfile(p) or os.path.islink(p) \
               or name.endswith(".o32") or not os.access(p, os.X_OK):
                continue
            if not is_n32_exec(p):
                continue
            seen.add(name)
            dst = os.path.join(a.out, name)
            if os.path.lexists(dst):
                os.remove(dst)
            os.symlink(os.path.relpath(a.link_to, a.out) if not os.path.isabs(a.link_to)
                       else a.link_to, dst)
            made += 1
    print("%d commands linked into %s" % (made, a.out))

if __name__ == "__main__":
    main()
