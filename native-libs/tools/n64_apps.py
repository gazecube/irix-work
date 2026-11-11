#!/usr/bin/env python3
"""Lift every IRIX N64 program of the tree and link it as a native executable; write a summary.

    apps.py [PATH ...]           (no paths: every N64 executable found under IRIX-TREE)

Same steps as batch.py for libraries.  A program differs in its start: the IRIX __start is not used; the host's
C start-up calls the lifted main directly, after a small stub has set __Argc/__Argv/environ and run __istart (the
C++ static constructors) when the program has them.  Run batch.py first: programs link against its libraries.
Results: apps/summary.txt and apps/missing.txt; the executables are apps/bin/NAME.  All paths are literal.
"""
import json
import os
import re
import struct
import subprocess
import sys
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

TOOLS = Path('/home/nekron/Projects/irix-work/irix-re/recomp/tools')
TREE = Path('/home/nekron/Projects/irix-work/IRIX-TREE')
LIB64 = Path('/home/nekron/Projects/irix-work/IRIX-TREE/usr/lib64')
WORK = Path('/home/nekron/opt/claude-work/n64/apps')
BIN = Path('/home/nekron/opt/claude-work/n64/apps/bin')
LIBS = Path('/home/nekron/opt/claude-work/n64/batch/out')
BASE = Path('/home/nekron/opt/claude-work/n64/out')
sys.path.insert(0, str(TOOLS))
sys.path.insert(0, '/home/nekron/opt/claude-work/n64')
from elf_n64 import Elf                                   # noqa: E402
from batch import HEADERS, CF, NATIVE, LIBC_NAMES, run                # noqa: E402

STUB = r'''
/* start-up of a lifted IRIX program: what __start did before main */
#include <stdint.h>
extern char **environ;
%(decl)s
static void __attribute__((constructor(65000))) irix_start(int argc, char **argv, char **envp)
{
    (void)argc; (void)argv; (void)envp;
%(body)s
}
'''


def find_programs():
    out = []
    for dp, dn, fn in os.walk(TREE):
        for f in fn:
            p = os.path.join(dp, f)
            try:
                if os.path.islink(p) or not os.path.isfile(p):
                    continue
                with open(p, 'rb') as h:
                    b = h.read(20)
            except OSError:
                continue
            if b[:4] == b'\x7fELF' and b[4] == 2 and b[5] == 2 and struct.unpack('>H', b[16:18])[0] == 2:
                out.append(p)
    return sorted(out)


def one(path):
    name = re.sub(r'[^A-Za-z0-9]', '_', Path(path).name)
    d = WORK / name
    d.mkdir(parents=True, exist_ok=True)
    rec = {'app': path.replace(str(TREE), ''), 'funcs': 0, 'lifted': 0, 'status': '', 'missing': [], 'failed': []}
    with open(d / 'log.txt', 'w') as log:
        try:
            e = Elf(path)
            hl = [x for h in HEADERS for x in ('-h', h)]
            run(['python3', TOOLS / 'n64_protos.py', path, d / 'protos.json'] + hl, log)
            if not (d / 'protos.json').exists():
                (d / 'protos.json').write_text('{}')
            (d / 'types.json').write_text('{}')
            cmd = ['python3', TOOLS / 'lift_n64.py', path, d, '--name', name, '--types', d / 'types.json',
                   '--protos', d / 'protos.json', '--wrapped', BASE / 'shimdefs.txt']
            for x in e.needed:
                if (LIBS / x).exists() and (LIB64 / x).exists():
                    cmd += ['--lifted', LIB64 / x]
            r = run(cmd, log)
            m = re.search(r'functions=(\d+) lifted=(\d+)', r.stdout)
            if not m:
                rec['status'] = 'lift failed: ' + (r.stderr.strip().splitlines() or ['?'])[-1][:120]
                return rec
            rec['funcs'], rec['lifted'] = int(m.group(1)), int(m.group(2))
            try:
                rec['failed'] = ['%s: %s' % (a, b) for a, b in json.loads((d / 'needed.json').read_text())['failed']]
            except Exception:                         # noqa: BLE001
                pass
            if run(['gcc'] + CF + ['-c', d / 'code.c', '-o', d / 'code.o'], log).returncode:
                rec['status'] = 'compile failed'
                return rec
            if run(['gcc', '-m64', '-c', d / 'data.s', '-o', d / 'data.o'], log).returncode:
                rec['status'] = 'assemble failed'
                return rec
            syms = {s['name'] for s in e.dynsyms if s['shndx']}
            if 'main' not in syms:
                rec['status'] = 'no main symbol'
                return rec
            decl, body = [], []
            if '__Argc' in syms:
                decl.append('extern int __Argc;'); body.append('    __Argc = argc;')
            if '__Argv' in syms:
                decl.append('extern char **__Argv;'); body.append('    __Argv = argv;')
            if '__istart' in syms:
                decl.append('extern uint64_t __istart(void);'); body.append('    __istart();')
            (d / 'start.c').write_text(STUB % {'decl': '\n'.join(decl), 'body': '\n'.join(body)})
            run(['gcc', '-m64', '-fPIC', '-w', '-c', d / 'start.c', '-o', d / 'start.o'], log)
            run(['ld', '-r', d / 'code.o', d / 'data.o', '-o', d / 'all.o'], log)
            und = set(run(['nm', '-u', d / 'all.o'], log).stdout.split()) - {'U', 'w'}
            shim = set((BASE / 'shimdefs.txt').read_text().split())
            # _name is the IRIX libc's internal spelling of name
            pairs = ['%s irix_%s' % (u, u) for u in sorted(und & shim)]
            pairs += ['%s %s%s' % (u, 'irix_' if u[1:] in shim else '', u[1:]) for u in sorted(und - shim) if re.fullmatch(r'_[a-z]\w+', u) and (u[1:] in shim or u[1:] in LIBC_NAMES)]
            (d / 'libc.map').write_text(''.join(x + '\n' for x in pairs))
            run(['objcopy', '--redefine-syms=%s' % (d / 'libc.map'), d / 'all.o', d / 'all_renamed.o'], log)
            link = ['gcc', '-m64', '-o', BIN / name, d / 'all_renamed.o', d / 'start.o', '-L' + str(LIBS), '-L' + str(BASE)]
            for x in e.needed:
                if (LIBS / x).exists():
                    link.append('-l:' + x)
                else:
                    link += NATIVE.get(x, [])
            link += ['-l:libc.so.1', '-lX11', '-lm', '-Wl,-z,notext', '-Wl,-z,noexecstack', '-Wl,-rpath,' + str(LIBS), '-Wl,-rpath,' + str(BASE)]
            r = run(link, log)
            if r.returncode:
                rec['missing'] = sorted(set(re.findall(r"undefined reference to `([^']+)'", r.stderr)))
                rec['status'] = 'link: %d missing' % len(rec['missing']) if rec['missing'] else 'link failed'
            else:
                rec['status'] = 'ok'
        except Exception as ex:                       # noqa: BLE001
            rec['status'] = 'error: %s' % str(ex)[:120]
    return rec


def main():
    BIN.mkdir(parents=True, exist_ok=True)
    progs = sys.argv[1:] or find_programs()
    recs = []
    with ThreadPoolExecutor(max_workers=6) as ex:
        for rec in ex.map(one, progs):
            recs.append(rec)
            print('%-44s %5d/%-5d %s' % (rec['app'][-44:], rec['lifted'], rec['funcs'], rec['status']), flush=True)
    with open(WORK / 'summary.txt', 'w') as w:
        for r in recs:
            w.write('%-52s %5d/%-5d %s\n' % (r['app'][-52:], r['lifted'], r['funcs'], r['status']))
            for f in r['failed'][:6]:
                w.write('      not lifted: %s\n' % f)
        w.write('\n%d programs, %d linked\n' % (len(recs), sum(r['status'] == 'ok' for r in recs)))
    miss = Counter(m for r in recs for m in r['missing'])
    with open(WORK / 'missing.txt', 'w') as w:
        for sym, n in miss.most_common():
            w.write('%4d %s\n' % (n, sym))
    (WORK / 'summary.json').write_text(json.dumps(recs, indent=1))


if __name__ == '__main__':
    main()
