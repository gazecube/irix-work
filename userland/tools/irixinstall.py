#!/usr/bin/env python3
"""The IRIX install command (toolroot/etc/install) for building into a staging root.

    install [-F dir | -f dir] [-m mode] [-u user] [-g group] [-idb tag] [-O] [-src file]
            [-ln target | -lns target] [-dir] [-blk/-chr maj,min] [-fifo] files...

Files go to $IRIX_DESTROOT/dir.  Owners, groups and idb tags are not applied (an unprivileged build), but every
installed path is appended to $IRIX_DESTROOT/.manifest with its mode, owner, group and idb tag, so the package that
installs the staging tree can apply them.  Device nodes and FIFOs are only recorded.
"""
import os
import shutil
import sys


def main():
    dest = os.environ.get('IRIX_DESTROOT')
    if not dest:
        sys.exit('install: IRIX_DESTROOT is not set')
    a = sys.argv[1:]
    d, mode, user, group, idb, src, ln, lns, isdir, dev = '', None, 'root', 'sys', '', None, None, None, False, None
    files = []
    i = 0
    while i < len(a):
        x = a[i]
        i += 1
        if x in ('-F', '-f'):
            d = a[i]; i += 1
        elif x == '-m':
            mode = a[i]; i += 1
        elif x == '-u':
            user = a[i]; i += 1
        elif x == '-g':
            group = a[i]; i += 1
        elif x == '-idb':
            idb = a[i]; i += 1
        elif x == '-src':
            src = a[i]; i += 1
        elif x == '-ln':
            ln = a[i]; i += 1
        elif x == '-lns':
            lns = a[i]; i += 1
        elif x == '-dir':
            isdir = True
        elif x in ('-blk', '-chr'):
            dev = (x, a[i]); i += 1
        elif x == '-fifo':
            dev = (x, '')
        elif x in ('-O', '-v', '-o', '-t', '-rawidb', '-s'):
            if x == '-rawidb':
                i += 1
        else:
            files.append(x)
    man = open(os.path.join(dest, '.manifest'), 'a')
    for f in files:
        name = os.path.basename(f)
        rel = os.path.join(d, name) if not isdir else (d + '/' + f if d else f)
        rel = '/' + rel.lstrip('/')
        target = dest + rel
        os.makedirs(os.path.dirname(target) if not isdir else target, exist_ok=True)
        kind = 'f'
        if isdir:
            kind = 'd'
        elif dev:
            kind = dev[0][1:] + ' ' + dev[1]
        elif lns:
            if os.path.lexists(target):
                os.remove(target)
            os.symlink(lns, target)
            kind = 'l ' + lns
        elif ln:
            if os.path.lexists(target):
                os.remove(target)
            t = ln if ln.startswith('/') else os.path.join(os.path.dirname(rel), ln)
            os.link(dest + t, target)
            kind = 'h ' + t
        else:
            s = src or f
            if os.path.lexists(target):
                os.remove(target)
            shutil.copy2(s, target)
            os.chmod(target, int(mode, 8) if mode else 0o755)
        man.write('%s\t%s\t%s\t%s\t%s\t%s\n' % (rel, kind, mode or '', user, group, idb))
    man.close()


if __name__ == '__main__':
    main()
