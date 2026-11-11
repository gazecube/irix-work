#!/usr/bin/env python3
"""Lift every IRIX N64 shared library of the tree in one run and write a summary.

    batch.py [NAME ...]          (no names: every library under usr/lib64)

For each library: prototypes of its imports from the host headers, lift, compile, link against the libraries
already built (in dependency order) and the libc layer.  Nothing stops the run: a failure is recorded with its
reason.  Results: batch/summary.txt (one line per library) and batch/missing.txt (undefined symbols, most wanted
first).  Work directories are batch/NAME, the linked libraries go to batch/out.
All paths are literal on purpose.
"""
import json
import re
import subprocess
import sys
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

TOOLS = Path('/home/nekron/Projects/irix-work/irix-re/recomp/tools')
LIB64 = Path('/home/nekron/Projects/irix-work/IRIX-TREE/usr/lib64')
WORK = Path('/home/nekron/opt/claude-work/n64/batch')
OUT = Path('/home/nekron/opt/claude-work/n64/batch/out')
BASE = Path('/home/nekron/opt/claude-work/n64/out')          # libc layer and the libraries built by build.sh
sys.path.insert(0, str(TOOLS))
from elf_n64 import Elf                                   # noqa: E402

HEADERS = ('stdio.h stdlib.h string.h strings.h unistd.h fcntl.h errno.h signal.h setjmp.h time.h ctype.h locale.h '
           'math.h pwd.h grp.h dirent.h netdb.h dlfcn.h wchar.h sys/stat.h sys/time.h sys/wait.h sys/select.h '
           'sys/socket.h sys/ioctl.h sys/mman.h netinet/in.h arpa/inet.h X11/Xlib.h X11/Xutil.h X11/Xresource.h '
           'X11/Xatom.h X11/extensions/shape.h GL/gl.h GL/glx.h GL/glu.h').split()
CF = '-m64 -std=gnu11 -O1 -fPIC -fno-strict-aliasing -fwrapv -w -g1'.split()
# IRIX library -> what stands for it on the host when it is not lifted
NATIVE = {'libX11.so.1': ['-lX11'], 'libXext.so': ['-lXext'], 'libGL.so': ['-lGL'], 'libGLU.so': ['-lGLU'],
          'libm.so': ['-lm'], 'libc.so.1': [], 'libgen.so': [], 'libdl.so': ['-ldl'], 'libpthread.so': ['-lpthread'],
          'libXpm.so.1': ['-lXpm'], 'libXi.so': ['-lXi']}
PREBUILT = {'libXt.so', 'libXm.so.1', 'libSgm.so.1', 'libC.so.2', 'libXmu.so', 'libvk.so.1'}


LIBC_NAMES = set('access chmod close execl execv execvp fcntl fdopen fopen fork mkdir pipe rmdir stat vfork waitpid read write open free malloc realloc calloc getpid kill dup dup2 unlink sleep wait lseek ioctl select'.split())


def run(cmd, log, timeout=1800):
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    log.write('$ %s\n%s%s\n' % (' '.join(map(str, cmd)), r.stdout[-4000:], r.stderr[-4000:]))
    return r


def stem(lib):
    return re.sub(r'[^A-Za-z0-9]', '_', re.sub(r'^lib|\.so.*$', '', lib))


def one(lib, deps, lifted):
    """Lift, compile and link one library; returns the summary record."""
    name = stem(lib)
    d = WORK / name
    d.mkdir(parents=True, exist_ok=True)
    rec = {'lib': lib, 'funcs': 0, 'lifted': 0, 'status': '', 'missing': []}
    with open(d / 'log.txt', 'w') as log:
        try:
            src = LIB64 / lib
            hl = [x for h in HEADERS for x in ('-h', h)]
            run(['python3', TOOLS / 'n64_protos.py', src, d / 'protos.json'] + hl, log)
            if not (d / 'protos.json').exists():
                (d / 'protos.json').write_text('{}')
            (d / 'types.json').write_text('{}')
            cmd = ['python3', TOOLS / 'lift_n64.py', src, d, '--name', name, '--types', d / 'types.json',
                   '--protos', d / 'protos.json', '--wrapped', BASE / 'shimdefs.txt']
            for x in deps:
                if x in lifted or x in PREBUILT:
                    cmd += ['--lifted', LIB64 / x]
            r = run(cmd, log)
            m = re.search(r'functions=(\d+) lifted=(\d+)', r.stdout)
            if not m:
                rec['status'] = 'lift failed: ' + (r.stderr.strip().splitlines() or ['?'])[-1][:120]
                return rec
            rec['funcs'], rec['lifted'] = int(m.group(1)), int(m.group(2))
            if run(['gcc'] + CF + ['-c', d / 'code.c', '-o', d / 'code.o'], log).returncode:
                rec['status'] = 'compile failed'
                return rec
            if run(['gcc', '-m64', '-c', d / 'data.s', '-o', d / 'data.o'], log).returncode:
                rec['status'] = 'assemble failed'
                return rec
            run(['ld', '-r', d / 'code.o', d / 'data.o', '-o', d / 'all.o'], log)
            und = set(run(['nm', '-u', d / 'all.o'], log).stdout.split()) - {'U', 'w'}
            shim = set((BASE / 'shimdefs.txt').read_text().split())
            # _name is the IRIX libc's internal spelling of name
            pairs = ['%s irix_%s' % (u, u) for u in sorted(und & shim)]
            pairs += ['%s %s%s' % (u, 'irix_' if u[1:] in shim else '', u[1:]) for u in sorted(und - shim) if re.fullmatch(r'_[a-z]\w+', u) and (u[1:] in shim or u[1:] in LIBC_NAMES)]
            (d / 'libc.map').write_text(''.join(x + '\n' for x in pairs))
            run(['objcopy', '--redefine-syms=%s' % (d / 'libc.map'), d / 'all.o', d / 'all_renamed.o'], log)
            link = ['gcc', '-m64', '-shared', '-Wl,-soname,' + lib, '-o', OUT / lib, d / 'all_renamed.o', '-L' + str(OUT), '-L' + str(BASE)]
            for x in deps:
                if x in lifted or x in PREBUILT:
                    link.append('-l:' + x)
                else:
                    link += NATIVE.get(x, [])
            link += ['-l:libc.so.1', '-lX11', '-lm', '-Wl,-z,notext', '-Wl,-z,noexecstack', '-Wl,--no-undefined', '-Wl,-rpath,$ORIGIN']
            r = run(link, log)
            if r.returncode:
                rec['missing'] = sorted(set(re.findall(r"undefined reference to `([^']+)'", r.stderr)))
                rec['status'] = 'link: %d missing' % len(rec['missing']) if rec['missing'] else 'link failed'
                # a second link that tolerates the gaps, so libraries that depend on this one can still be tried
                run([x for x in link if x != '-Wl,--no-undefined'], log)
            else:
                rec['status'] = 'ok'
        except Exception as ex:                       # noqa: BLE001
            rec['status'] = 'error: %s' % str(ex)[:120]
    return rec


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    want = sys.argv[1:]
    libs = {}
    for p in sorted(LIB64.iterdir()):
        if p.is_symlink() or not p.is_file() or '.so' not in p.name or p.name in PREBUILT:
            continue
        try:
            e = Elf(str(p))
        except Exception:                             # noqa: BLE001
            continue
        if want and p.name not in want:
            continue
        libs[p.name] = e.needed
    # the libraries build.sh made are there for everyone
    for x in PREBUILT:
        if (BASE / x).exists() and not (OUT / x).exists():
            (OUT / x).symlink_to(BASE / x)
    done, recs = set(), []
    todo = dict(libs)
    while todo:
        ready = [k for k, deps in todo.items() if not any(x in todo for x in deps if x != k)]
        if not ready:                                 # a cycle: take what is left as it comes
            ready = list(todo)[:1]
        with ThreadPoolExecutor(max_workers=6) as ex:
            for rec in ex.map(lambda k: one(k, todo[k], done), ready):
                recs.append(rec)
                print('%-28s %5d/%-5d %s' % (rec['lib'], rec['lifted'], rec['funcs'], rec['status']), flush=True)
        for k in ready:
            done.add(k)
            del todo[k]
    recs.sort(key=lambda r: r['lib'])
    with open(WORK / 'summary.txt', 'w') as w:
        for r in recs:
            w.write('%-28s %5d/%-5d %s\n' % (r['lib'], r['lifted'], r['funcs'], r['status']))
        w.write('\n%d libraries, %d linked clean, %d of %d functions lifted\n' % (
            len(recs), sum(r['status'] == 'ok' for r in recs), sum(r['lifted'] for r in recs), sum(r['funcs'] for r in recs)))
    miss = Counter(m for r in recs for m in r['missing'])
    with open(WORK / 'missing.txt', 'w') as w:
        for sym, n in miss.most_common():
            w.write('%4d %s\n' % (n, sym))
    (WORK / 'summary.json').write_text(json.dumps(recs, indent=1))


if __name__ == '__main__':
    main()
