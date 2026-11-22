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
import subprocess
import sys
import tempfile


def plain_aliases(archive):
    """IRIX libraries define their functions under libc-internal names (_makekey); IRIX's libc naming layer gave
    programs the plain ones.  Add a weak plain-name alias for every global _name of the archive whose plain name
    the archive does not define."""
    with tempfile.TemporaryDirectory(dir=os.path.dirname(archive)) as tmp:
        names = subprocess.run(['ar', 't', archive], capture_output=True, text=True).stdout.split()
        if not names:
            return
        subprocess.run(['ar', 'x', archive], cwd=tmp, check=True)
        defined = set()
        syms = {}
        for n in set(names):
            out = subprocess.run(['objdump', '-t', n], cwd=tmp, capture_output=True, text=True).stdout
            for line in out.splitlines():
                parts = line.split()
                # "00000000 g     F .text  00000123 _makekey": global (g) or weak (w) definitions only
                if len(parts) < 5 or not (' g ' in line or ' w ' in line) or parts[-3] == '*UND*':
                    continue
                sym, sec, val = parts[-1], parts[-3], parts[0]
                defined.add(sym)
                syms.setdefault(n, []).append((sym, sec, val, 'function' if ' F ' in line else 'object'))
        changed = False
        for n, lst in syms.items():
            adds = []
            for sym, sec, val, kind in lst:
                if sym.startswith('_') and not sym.startswith('__') and sym[1:] not in defined:
                    adds += ['--add-symbol', '%s=%s:0x%s,weak,%s' % (sym[1:], sec, val, kind)]
                    defined.add(sym[1:])
            if adds:
                subprocess.run(['objcopy'] + adds + [n], cwd=tmp, check=True)
                changed = True
        if changed:
            os.remove(archive)
            subprocess.run(['ar', 'rcs', archive] + names, cwd=tmp, check=True)


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
            if target.endswith('.a') and '/lib' in rel:
                plain_aliases(target)
        man.write('%s\t%s\t%s\t%s\t%s\t%s\n' % (rel, kind, mode or '', user, group, idb))
    man.close()


if __name__ == '__main__':
    main()
