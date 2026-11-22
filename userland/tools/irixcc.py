#!/usr/bin/env python3
"""cc / CC / ld for IRIX Makefiles: MIPSpro command lines turned into gcc ones for x86.

Called through symlinks in the tool root (usr/bin/cc, usr/bin/CC, usr/bin/ld).  -64 builds x86_64, everything
else i386 (the N32 programs).  MIPSpro-only options are dropped; -signed becomes -fsigned-char.  Every compile gets
the compatibility prelude and headers (../compat), every link the compatibility library.  IRIX header and library
paths under the tool root are dropped, so the host's headers and libraries are used.  Libraries that are part of
the host C library on Linux (-lgen, -lw, -lmalloc, ...) are dropped; libcurses becomes ncurses.
"""
import os
import sys

U = '/home/nekron/Projects/irix-work/userland'
TOOLROOT = U + '/toolroot'
ROOT = '/home/nekron/opt/claude-work/eoe/root'             # the staging root (setup-root.sh)
DROP = {'-fullwarn', '-cckr', '-xansi', '-xgot', '-ansi', '-ansiposix', '-KPIC', '-elf', '-call_shared', '-non_shared', '-nokpicopt',
        '-nostdinc', '-nostdlib', '-quickstart_info', '-no_unresolved', '-Xcpluscomm', '-prototypes', '-dollar', '-common', '-use_readonly_const',
        '-wlint', '-show', '-n32', '-32', '-o32', '-mips1', '-mips2', '-mips3', '-mips4', '-r4000', '-r5000', '-r8000',
        '-r10000', '-shared_rpath', '-ignore_unresolved', '-transitive_link', '-no_transitive_link', '-demangle', '-cfront',
        '-ptused', '-ptnone', '-pta', '-LD_MSG:off=85', '-nocpp', '-signed', '-unsigned', '-woff_all', '-Wf,-XNl8192'}
DROP_ARG = {'-woff', '-MDupdate', '-MDtarget', '-G', '-set_version', '-exports_file', '-hiddens_file', '-update_registry', '-check_registry',
            '-delay_load', '-rdata_shared', '-rpath_link', '-Wl,-I'}
DROP_PREFIX = ('-OPT:', '-LNO:', '-TARG:', '-TENV:', '-IPA', '-LANG:', '-diag_', '-Wf,', '-Wb,', '-Wc,', '-Wl,-woff',
               '-Wl,-I,', '-Wl,-rdata_shared', '-Wl,-x', '-Wl,-set_version', '-Wl,-exports_file', '-Wl,-no_unresolved',
               '-Wl,-quickstart', '-Wl,-T', '-Wl,-D', '-Wl,-elf', '-Wl,-LD_', '-LD_', '-CG:', '-INLINE', '-WOPT:', '-DEBUG:', '-FE:')
HOST_LIBC = {'gen', 'w', 'malloc', 'sun', 'bsd', 'mutex', 'mld', 'mpc', 'socket', 'nsl', 'dl', 'C', 'Csup', 'nanothread',
             'fpe', 'fastm', 'm43', 'exc', 'rpcsvc', 'abi', 'cmd'}
MAP_LIB = {'curses': 'ncurses', 'termcap': 'ncurses', 'termlib': 'ncurses', 'l': 'fl', 'y': ''}


def built(n, m64):
    """An IRIX library built from source into the staging root."""
    p = '%s/usr/%s/lib%s.a' % (ROOT, 'lib64' if m64 else 'lib32', n)
    return p if os.path.exists(p) else None


def main():
    tool = os.path.basename(sys.argv[0])
    args = sys.argv[1:]
    m64 = '-64' in args
    out = []
    i = 0
    link = True
    whole = False
    while i < len(args):
        a = args[i]
        i += 1
        if a in ('-c', '-S', '-E', '-M', '-MM'):
            link = False
        if a == '-64':
            continue
        if a == '-signed':
            out.append('-fsigned-char')
            continue
        if a in DROP or a.startswith(DROP_PREFIX) or (a.startswith('-woff') and a != '-woff'):
            continue
        if a in DROP_ARG:
            i += 1
            continue
        if a.startswith('-I' + ROOT + '/usr/include'):
            out += ['-idirafter', a[2:]]                 # IRIX headers after the host's
            continue
        if a.startswith('-I' + TOOLROOT) or a.startswith('-L' + TOOLROOT):
            continue
        if a == '-I' and i < len(args) and args[i].startswith(TOOLROOT):
            i += 1
            continue
        if a.startswith('-l'):
            n = a[2:]
            if n in HOST_LIBC and not built(n, m64):
                continue
            n = MAP_LIB.get(n, n)
            if n:
                out.append('-l' + n)
            continue
        if a.endswith('.a') and (a.startswith(TOOLROOT) or a.startswith(ROOT)):
            n = os.path.basename(a)[3:-2]
            if built(n, m64):
                out.append(built(n, m64))
            elif n not in HOST_LIBC:
                n = MAP_LIB.get(n, n)
                if n:
                    out.append('-l' + n)
            continue
        if a in ('-soname', '-rpath') and i < len(args):         # linker options MIPSpro took directly
            out.append('-Wl,%s,%s' % (a, args[i]))
            i += 1
            continue
        if a == '-all':                                          # the archives that follow go in whole (a DSO from .a)
            out.append('-Wl,--whole-archive')
            whole = True
            continue
        if a == '-none':
            out.append('-Wl,--no-whole-archive')
            whole = False
            continue
        if a == '-g3':
            a = '-g'
        out.append(a)
    if whole:
        out.append('-Wl,--no-whole-archive')
    arch = '-m64' if m64 else '-m32'
    if tool == 'ld':
        if '-r' in out:
            os.execvp('ld', ['ld', '-m', 'elf_x86_64' if m64 else 'elf_i386'] + out)
        tool = 'cc'
    cc = 'g++' if tool == 'CC' else 'gcc'
    flags = [arch, '-fPIC', '-fcommon', '-fno-strict-aliasing', '-w', '-I' + U + '/compat/include', '-I' + U + '/compat/irix-include']
    if tool != 'CC':
        flags += ['-std=gnu89', '-include', U + '/compat/include/prelude.h']
    else:
        flags += ['-fpermissive']
    if link:
        out += ['-L%s/usr/%s' % (ROOT, 'lib64' if m64 else 'lib32'), '-L' + U + '/compat/lib/' + ('64' if m64 else '32'), '-lirixc', '-lirixcompat']
    os.execvp(cc, [cc] + flags + out)


if __name__ == '__main__':
    main()
