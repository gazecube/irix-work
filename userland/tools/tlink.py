#!/usr/bin/env python3
"""tlink [-r] [-v] [-x REGEX]... SRC DST: mirror the tree SRC as DST, with a symbolic link in DST for every file
of SRC (SGI's build uses it to give every ABI variant of a library its own object directory).  Names matching an
-x regular expression are left out; -r goes into subdirectories."""
import os
import re
import sys

args = sys.argv[1:]
rec, excl, rest = False, [], []
i = 0
while i < len(args):
    a = args[i]
    i += 1
    if a == '-r':
        rec = True
    elif a == '-x':
        excl.append(re.compile(args[i]))
        i += 1
    elif a in ('-v', '-q'):
        pass
    else:
        rest.append(a)
src, dst = rest[0], rest[1]


def mirror(s, d):
    os.makedirs(d, exist_ok=True)
    for n in sorted(os.listdir(s)):
        if any(x.search(n) for x in excl):
            continue
        sp, dp = os.path.join(s, n), os.path.join(d, n)
        if os.path.isdir(sp) and not os.path.islink(sp):
            if rec:
                mirror(sp, dp)
            continue
        if not os.path.lexists(dp):
            os.symlink(os.path.relpath(os.path.abspath(sp), os.path.abspath(d)), dp)


mirror(src, dst)
