#!/usr/bin/env python3
"""Link and smoke-test the IRIX N32 world: every library, then every program.

    smoke32.py libs              lift, compile and link every N32 library of the standard directories
    smoke32.py progs [PATH ...]  lift, compile and link every N32 program (or the ones given), then start each
    smoke32.py all               both

A program is started with no arguments and no input for a few seconds inside a sandbox (bubblewrap: its own
process and network namespaces, the file system read-only, an empty home directory), because some of these are
system tools that act on the whole machine when run bare (killall, halt).  It can reach the private X display :99.
Recorded per program: missing symbols at link, and what the start did (exit code, signal, still running, first
lines of output).  Results: smoke/libs.txt, smoke/progs.txt, smoke/missing.txt, smoke/outcome.txt.
All paths are literal.
"""
import json
import os
import re
import resource
import signal
import struct
import subprocess
import sys
import threading
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

TOOLS = Path('/home/nekron/Projects/irix-work/irix-re/recomp/tools')
TREE = Path('/home/nekron/Projects/irix-work/IRIX-TREE')
N32 = Path('/home/nekron/opt/claude-work/n32')
WORK = Path('/home/nekron/opt/claude-work/n32/smoke')
OUT = Path('/home/nekron/opt/claude-work/n32/smoke/out')      # lifted libraries
BIN = Path('/home/nekron/opt/claude-work/n32/smoke/bin')      # lifted programs
BASE = Path('/home/nekron/opt/claude-work/n32/out')           # libc layer and the libraries of build32.sh
LIBDIRS = ['usr/lib32', 'lib32', 'usr/lib32/mips3']
sys.path.insert(0, str(TOOLS))
from elf_n32 import Elf                                   # noqa: E402

HEADERS = ('stdio.h stdlib.h string.h strings.h unistd.h fcntl.h errno.h signal.h setjmp.h time.h ctype.h locale.h '
           'math.h pwd.h grp.h dirent.h netdb.h dlfcn.h wchar.h sys/stat.h sys/time.h sys/wait.h sys/select.h '
           'sys/socket.h sys/ioctl.h sys/mman.h netinet/in.h arpa/inet.h X11/Xlib.h X11/Xutil.h X11/Xresource.h '
           'X11/Xatom.h X11/extensions/shape.h GL/gl.h GL/glx.h GL/glu.h').split()
CF = '-m32 -msse2 -mfpmath=sse -malign-double -std=gnu11 -O1 -fPIC -fno-strict-aliasing -fwrapv -w'.split()
NATIVE = {'libX11.so.1': ['-lX11'], 'libXext.so': ['-lXext'], 'libGL.so': ['-lGL'], 'libGLU.so': ['-lGLU'],
          'libm.so': ['-lm'], 'libc.so.1': [], 'libgen.so': [], 'libdl.so': ['-ldl'], 'libpthread.so': ['-lpthread']}
PREBUILT = {'libXt.so', 'libXm.so.1', 'libC.so.2', 'libCsup.so'}
LIBC_NAMES = set('access chmod close execl execv execvp fcntl fdopen fopen fork mkdir pipe rmdir stat vfork waitpid read write '
                 'open free malloc realloc calloc getpid kill dup dup2 unlink sleep wait lseek ioctl select'.split())


# Memory: one compile of a large lifted library takes gigabytes.  Compiles share a budget of BUDGET units; a
# compile costs one unit per 12 MB of C source (at least one, at most all of them), so the big ones run alone.
# Every child runs at low CPU and idle I/O priority, with its address space capped, in the driver's own process
# group, which is killed as a whole when the driver is stopped.
BUDGET = 6
MEM_CAP = 5 << 30
_cv = threading.Condition()
_used = 0


def low_priority():
    os.nice(15)
    resource.setrlimit(resource.RLIMIT_AS, (MEM_CAP, MEM_CAP))


def compile_c(src, obj, log):
    global _used
    cost = min(BUDGET, 1 + os.path.getsize(src) // (12 << 20))
    with _cv:
        while _used + cost > BUDGET:
            _cv.wait()
        _used += cost
    try:
        return run(['gcc'] + CF + ['-c', src, '-o', obj], log)
    finally:
        with _cv:
            _used -= cost
            _cv.notify_all()


def run(cmd, log, timeout=7200):
    r = subprocess.run(['ionice', '-c3'] + [str(c) for c in cmd], capture_output=True, text=True, timeout=timeout, preexec_fn=low_priority)
    log.write('$ %s\n%s%s\n' % (' '.join(map(str, cmd)), r.stdout[-3000:], r.stderr[-3000:]))
    return r


def find_lib(name):
    for d in LIBDIRS:
        p = TREE / d / name
        if p.is_file():
            return p
    return None


def is_n32(p, etype):
    try:
        with open(p, 'rb') as h:
            b = h.read(52)
    except OSError:
        return False
    return (len(b) == 52 and b[:4] == b'\x7fELF' and b[4] == 1 and b[5] == 2 and struct.unpack('>H', b[18:20])[0] == 8
            and struct.unpack('>I', b[36:40])[0] & 0x20 and struct.unpack('>H', b[16:18])[0] == etype)


def lift_compile(src, d, name, deps, log, rec):
    """Prototypes, lift, compile, partial link, libc renames.  Returns the object to link, or None."""
    hl = [x for h in HEADERS for x in ('-h', h)]
    run(['python3', TOOLS / 'n64_protos.py', src, d / 'protos.json', '--m32'] + hl, log)
    if not (d / 'protos.json').exists():
        (d / 'protos.json').write_text('{}')
    (d / 'types.json').write_text('{}')
    cmd = ['python3', TOOLS / 'lift_n64.py', src, d, '--name', name, '--types', d / 'types.json', '--protos', d / 'protos.json',
           '--wrapped', BASE / 'shimdefs.txt']
    for x in deps:
        if (OUT / x).exists() and find_lib(x):
            cmd += ['--lifted', find_lib(x)]
    r = run(cmd, log)
    m = re.search(r'functions=(\d+) lifted=(\d+)', r.stdout)
    if not m:
        rec['status'] = 'lift failed: ' + (r.stderr.strip().splitlines() or ['?'])[-1][:100]
        return None
    rec['funcs'], rec['lifted'] = int(m.group(1)), int(m.group(2))
    if compile_c(d / 'code.c', d / 'code.o', log).returncode:
        rec['status'] = 'compile failed'
        return None
    if run(['gcc', '-m32', '-c', d / 'data.s', '-o', d / 'data.o'], log).returncode:
        rec['status'] = 'assemble failed'
        return None
    run(['ld', '-m', 'elf_i386', '-r', d / 'code.o', d / 'data.o', '-o', d / 'all.o'], log)
    und = set(run(['nm', '-u', d / 'all.o'], log).stdout.split()) - {'U', 'w'}
    shim = set((BASE / 'shimdefs.txt').read_text().split())
    pairs = ['%s irix_%s' % (u, u) for u in sorted(und & shim)]
    pairs += ['%s %s%s' % (u, 'irix_' if u[1:] in shim else '', u[1:]) for u in sorted(und - shim)
              if re.fullmatch(r'_[a-z]\w+', u) and (u[1:] in shim or u[1:] in LIBC_NAMES)]
    (d / 'libc.map').write_text(''.join(x + '\n' for x in pairs))
    run(['objcopy', '--redefine-syms=%s' % (d / 'libc.map'), d / 'all.o', d / 'all_renamed.o'], log)
    for f in ('code.c', 'code.o', 'data.s', 'data.o', 'all.o'):       # keep the disk
        try:
            (d / f).unlink()
        except OSError:
            pass
    return d / 'all_renamed.o'


def link_flags(deps):
    # the libc layer first: it stands in front of the host's Xlib for a few functions (fast quarks)
    fl = ['-L' + str(OUT), '-L' + str(BASE), '-L/usr/lib', '-l:libc.so.1']
    for x in deps:
        fl += ['-l:' + x] if (OUT / x).exists() else NATIVE.get(x, [])
    return fl + ['-l:libc.so.1', '-lX11', '-lm', '-Wl,-z,notext', '-Wl,-z,noexecstack']


def one_lib(name, src, deps):
    d = WORK / 'lib' / re.sub(r'\W', '_', name)
    d.mkdir(parents=True, exist_ok=True)
    rec = {'lib': name, 'funcs': 0, 'lifted': 0, 'status': '', 'missing': []}
    with open(d / 'log.txt', 'w') as log:
        try:
            obj = lift_compile(src, d, re.sub(r'\W', '_', re.sub(r'^lib|\.so.*$', '', name)), deps, log, rec)
            if obj:
                link = ['gcc', '-m32', '-shared', '-Wl,-soname,' + name, '-o', OUT / name, obj] + link_flags(deps) + ['-Wl,-rpath,' + str(OUT), '-Wl,-rpath,' + str(BASE)]
                r = run(link + ['-Wl,--no-undefined'], log)
                if r.returncode:
                    rec['missing'] = sorted(set(re.findall(r"undefined reference to `([^']+)'", r.stderr)))
                    rec['status'] = 'link: %d missing' % len(rec['missing']) if rec['missing'] else 'link failed'
                    run(link, log)                    # with the gaps, so that others can still link against it
                else:
                    rec['status'] = 'ok'
                obj.unlink()
        except Exception as ex:                       # noqa: BLE001
            rec['status'] = 'error: %s' % str(ex)[:100]
    (d / 'tried').write_text(rec['status'] + '\n')
    return rec


def prebuilt():
    OUT.mkdir(parents=True, exist_ok=True)
    for x in PREBUILT:
        if (BASE / x).exists() and not (OUT / x).exists():
            (OUT / x).symlink_to(BASE / x)


def libs():
    prebuilt()
    todo = {}
    for d in LIBDIRS:
        for p in sorted((TREE / d).iterdir()):
            # built by an earlier run, or tried and failed there (remove smoke/lib/NAME/tried to try again)
            if (OUT / p.name).exists() or (WORK / 'lib' / re.sub(r'\W', '_', p.name) / 'tried').exists():
                continue
            if p.name in todo or p.name in PREBUILT or p.name in NATIVE or p.is_symlink() or '.so' not in p.name or not is_n32(p, 3):
                continue
            try:
                todo[p.name] = (p, Elf(str(p)).needed)
            except Exception:                         # noqa: BLE001
                pass
    print('%d N32 libraries' % len(todo), flush=True)
    recs = []
    while todo:
        ready = [k for k, (_, deps) in todo.items() if not any(x in todo for x in deps if x != k)] or list(todo)[:1]
        with ThreadPoolExecutor(max_workers=BUDGET) as ex:
            for rec in ex.map(lambda k: one_lib(k, todo[k][0], todo[k][1]), ready):
                recs.append(rec)
                print('lib  %-30s %6d/%-6d %s' % (rec['lib'], rec['lifted'], rec['funcs'], rec['status']), flush=True)
        for k in ready:
            del todo[k]
    recs.sort(key=lambda r: r['lib'])
    with open(WORK / 'libs.txt', 'a') as w:
        for r in recs:
            w.write('%-30s %6d/%-6d %s\n' % (r['lib'], r['lifted'], r['funcs'], r['status']))
        w.write('\n%d libraries, %d linked clean, %d of %d functions lifted\n' % (
            len(recs), sum(r['status'] == 'ok' for r in recs), sum(r['lifted'] for r in recs), sum(r['funcs'] for r in recs)))
    (WORK / 'libs.json').write_text(json.dumps(recs))
    return recs


SANDBOX = ['bwrap', '--ro-bind', '/', '/', '--dev', '/dev', '--proc', '/proc', '--tmpfs', '/tmp', '--ro-bind', '/tmp/.X11-unix', '/tmp/.X11-unix',
           '--tmpfs', '/home/nekron', '--ro-bind', '/home/nekron/opt/claude-work/n32', '/home/nekron/opt/claude-work/n32',
           '--dir', '/home/nekron/smoke', '--chdir', '/home/nekron/smoke', '--unshare-pid', '--unshare-net', '--unshare-ipc',
           '--die-with-parent', '--new-session', '--setenv', 'HOME', '/home/nekron/smoke', '--setenv', 'DISPLAY', ':99',
           '--setenv', 'LD_PRELOAD', '/home/nekron/opt/claude-work/n32/segv32.so']


def smoke(exe):
    """Start the program bare for five seconds in the sandbox; returns (outcome, first lines of output)."""
    try:
        r = subprocess.run(SANDBOX + ['timeout', '-s', 'KILL', '5', str(exe)], stdin=subprocess.DEVNULL, capture_output=True, timeout=30)
    except subprocess.TimeoutExpired:
        return 'hung past the sandbox timeout', ''
    text = (r.stdout + r.stderr).decode('latin-1', 'replace')
    text = '\n'.join(x for x in text.splitlines() if 'ld.so' not in x)
    rc = r.returncode
    m = re.search(r'SIGNAL (\d+) fault=(\S+)', text)
    if m:
        frames = re.findall(r'^(\S+?)\(([^)]*)\)', text[m.end():], re.M)
        where = next(('%s:%s' % (Path(a).name, b) for a, b in frames if 'segv32' not in a and 'linux-gate' not in a), '?')
        return 'signal %s at %s' % (m.group(1), where[:60]), text[:m.start()].strip()[:300]
    if rc in (124, 137, -9):
        return 'running after 5 s', text.strip()[:300]
    if rc < 0:
        return 'signal %d' % -rc, text.strip()[:300]
    return 'exit %d' % rc, text.strip()[:300]



def one_prog(path):
    """One program, once: the result of an earlier run is reused (remove smoke/prog/NAME/result.json to redo it)."""
    name = re.sub(r'\W', '_', str(path).replace(str(TREE), '').strip('/'))[-90:]
    res = WORK / 'prog' / name / 'result.json'
    if res.exists():
        return json.loads(res.read_text())
    rec = one_prog_run(path)
    res.parent.mkdir(parents=True, exist_ok=True)
    res.write_text(json.dumps(rec))
    return rec


def one_prog_run(path):
    rel = str(path).replace(str(TREE), '')
    name = re.sub(r'\W', '_', rel.strip('/'))[-90:]
    d = WORK / 'prog' / name
    d.mkdir(parents=True, exist_ok=True)
    rec = {'prog': rel, 'funcs': 0, 'lifted': 0, 'status': '', 'missing': [], 'outcome': '', 'output': ''}
    with open(d / 'log.txt', 'w') as log:
        try:
            e = Elf(str(path))
            syms = {s['name'] for s in e.dynsyms if s['shndx']}
            obj = lift_compile(path, d, 'app', e.needed, log, rec)
            if not obj:
                return rec
            if 'main' not in syms:
                rec['status'] = 'no main symbol'
                return rec
            body = ['__Argc = argc;'] if '__Argc' in syms else []
            body += ['__Argv = argv;'] if '__Argv' in syms else []
            body += ['lw___istart();'] if '__istart' in syms else []
            (d / 'start.c').write_text('extern int __Argc; extern char **__Argv; extern unsigned long long lw___istart(void);\n'
                                       'static void __attribute__((constructor(65000))) irix_start(int argc, char **argv, char **envp) { %s }\n' % ' '.join(body))
            run(['gcc', '-m32', '-fPIC', '-w', '-c', d / 'start.c', '-o', d / 'start.o'], log)
            exe = BIN / name
            r = run(['gcc', '-m32', '-o', exe, obj, d / 'start.o'] + link_flags(e.needed) + ['-Wl,-rpath,' + str(OUT), '-Wl,-rpath,' + str(BASE)], log)
            obj.unlink()
            if r.returncode:
                rec['missing'] = sorted(set(re.findall(r"undefined reference to `([^']+)'", r.stderr)))
                rec['status'] = 'link: %d missing' % len(rec['missing']) if rec['missing'] else 'link failed'
                return rec
            rec['status'] = 'ok'
            rec['outcome'], rec['output'] = smoke(exe)
        except Exception as ex:                       # noqa: BLE001
            rec['status'] = 'error: %s' % str(ex)[:100]
    return rec


def progs(paths):
    BIN.mkdir(parents=True, exist_ok=True)
    prebuilt()
    if not paths:
        for dp, dn, fn in os.walk(TREE):
            for f in fn:
                p = os.path.join(dp, f)
                if not os.path.islink(p) and os.path.isfile(p) and is_n32(p, 2):
                    paths.append(p)
        paths.sort()
    print('%d N32 programs' % len(paths), flush=True)
    recs = []
    with ThreadPoolExecutor(max_workers=BUDGET) as ex:
        for rec in ex.map(one_prog, paths):
            recs.append(rec)
            print('prog %-50s %5d/%-5d %-18s %s' % (rec['prog'][-50:], rec['lifted'], rec['funcs'], rec['status'], rec['outcome']), flush=True)
    with open(WORK / 'progs.txt', 'w') as w:
        for r in recs:
            w.write('%-56s %5d/%-5d %-18s %s\n' % (r['prog'][-56:], r['lifted'], r['funcs'], r['status'], r['outcome']))
            if r['output']:
                w.write('        | %s\n' % r['output'].splitlines()[0][:120])
    miss = Counter(m for r in recs for m in r['missing'])
    with open(WORK / 'missing.txt', 'w') as w:
        w.write('%d programs, %d linked, %d not\n\n' % (len(recs), sum(r['status'] == 'ok' for r in recs), sum(r['status'] != 'ok' for r in recs)))
        for sym, n in miss.most_common():
            w.write('%5d %s\n' % (n, sym))
    out = Counter(re.sub(r' at .*', '', r['outcome']) for r in recs if r['outcome'])
    where = Counter(r['outcome'] for r in recs if r['outcome'].startswith('signal'))
    with open(WORK / 'outcome.txt', 'w') as w:
        for k, n in out.most_common():
            w.write('%5d %s\n' % (n, k))
        w.write('\ncrash sites:\n')
        for k, n in where.most_common(40):
            w.write('%5d %s\n' % (n, k))
    (WORK / 'progs.json').write_text(json.dumps(recs))


def stop(signum, frame):
    signal.signal(signal.SIGTERM, signal.SIG_IGN)
    os.killpg(0, signal.SIGTERM)                  # the compilers and sandboxes started from here
    os._exit(1)


if __name__ == '__main__':
    os.setpgrp()
    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)
    what = sys.argv[1] if len(sys.argv) > 1 else 'all'
    WORK.mkdir(parents=True, exist_ok=True)
    if what in ('libs', 'all'):
        libs()
    if what in ('progs', 'all'):
        progs(sys.argv[2:])
