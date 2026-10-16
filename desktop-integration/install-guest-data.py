#!/usr/bin/env python3
import argparse
import os
import shutil
import sys

DEFAULT_ROOTS = [
    "var",
    "usr/lib/desktop",
    "usr/lib/filetype",
    "usr/lib/X11",
    "usr/lib/images",
    "usr/include/X11/bitmaps",
    "usr/share",
]
NEVER = ("dev", "proc", "sys", "tmp", "etc", "stand", "unix")
SKIP_PREFIXES = ("var/tmp", "var/spool", "var/adm", "var/opt", "var/named",
                 "var/www")
SKIP_NAMES = {"xm_"}


def is_mips_elf(path):
    try:
        with open(path, "rb") as f:
            h = f.read(20)
    except OSError:
        return False
    return h[:4] == b"\x7fELF" and h[5] == 2 and h[18:20] == b"\x00\x08"


def host_target(dest, rel):
    parent = os.path.realpath(os.path.join(dest, os.path.dirname(rel)))
    return os.path.join(parent, os.path.basename(rel))


def main():
    ap = argparse.ArgumentParser(
        description="Install IRIX data trees that guest paths refer to into the "
                    "host filesystem. Never overwrites; dry run by default.")
    ap.add_argument("--src", default=os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "..", "IRIX-TREE"))
    ap.add_argument("--dest", default="/")
    ap.add_argument("--root", action="append",
                    help="tree below --src to install (repeatable)")
    ap.add_argument("--manifest", default="usr/lib/irix-guest-data.manifest")
    ap.add_argument("--apply", action="store_true")
    ap.add_argument("-v", "--verbose", action="store_true")
    a = ap.parse_args()

    src = os.path.realpath(a.src)
    dest = a.dest.rstrip("/") or "/"
    roots = a.root or DEFAULT_ROOTS
    if a.apply and os.geteuid() != 0:
        print("--apply needs root (sudo -n %s --apply)" % sys.argv[0],
              file=sys.stderr)
        return 1

    stats = {"new": 0, "exists": 0, "elf": 0, "skip": 0}
    made = []
    for root in roots:
        top = os.path.join(src, root)
        if not os.path.isdir(top):
            continue
        for cur, dirs, files in os.walk(top):
            rel_dir = os.path.relpath(cur, src)
            if rel_dir.startswith(NEVER) or rel_dir.startswith(SKIP_PREFIXES):
                dirs[:] = []
                continue
            for name in list(dirs):
                p = os.path.join(cur, name)
                if os.path.islink(p):
                    files.append(name)
                    dirs.remove(name)
            for name in sorted(files):
                rel = os.path.join(rel_dir, name)
                s = os.path.join(src, rel)
                if any(name.startswith(x) for x in SKIP_NAMES):
                    stats["skip"] += 1
                    continue
                t = host_target(dest, rel)
                if os.path.lexists(t):
                    stats["exists"] += 1
                    continue
                if not os.path.islink(s) and is_mips_elf(s):
                    stats["elf"] += 1
                    continue
                stats["new"] += 1
                if a.verbose or not a.apply:
                    if a.verbose:
                        print("NEW", t)
                if a.apply:
                    os.makedirs(os.path.dirname(t), exist_ok=True)
                    if os.path.islink(s):
                        os.symlink(os.readlink(s), t)
                    else:
                        shutil.copyfile(s, t)
                        os.chmod(t, os.stat(s).st_mode & 0o7755)
                        os.chown(t, 0, 0)
                    made.append(os.path.relpath(t, dest) if dest != "/" else t[1:])

    print("new=%(new)d already-on-host=%(exists)d mips-elf-skipped=%(elf)d "
          "other-skipped=%(skip)d" % stats)
    if a.apply:
        mp = a.manifest if os.path.isabs(a.manifest) else os.path.join(dest, a.manifest)
        os.makedirs(os.path.dirname(mp), exist_ok=True)
        old = []
        if os.path.exists(mp):
            old = open(mp).read().split("\n")
        with open(mp, "w") as f:
            f.write("\n".join(sorted(set(x for x in old + made if x))) + "\n")
        print("manifest:", mp)
    else:
        print("dry run only; add --apply (as root) to install, -v to list files")
    return 0


if __name__ == "__main__":
    sys.exit(main())
