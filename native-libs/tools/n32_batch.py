#!/usr/bin/env python3
"""Stage 1 for N32: lift every N32 library and program of the tree and compile the result for i386.

    batch32.py [PATH ...]

No linking yet (the i386 calling convention between lifted and native code and the N32 libc layer come next), so
this measures how much of the N32 world translates: functions lifted, and whether the C compiles.
Results: n32/summary.txt, n32/reasons.txt (why functions did not lift).  All paths are literal.
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
WORK = Path('/home/nekron/opt/claude-work/n32/work')
OUT = Path('/home/nekron/opt/claude-work/n32')
CF = '-m32 -msse2 -mfpmath=sse -std=gnu11 -O0 -fPIC -fno-strict-aliasing -fwrapv -w -fsyntax-only'.split()


def find():
    out = []
    for dp, dn, fn in os.walk(TREE):
        for f in fn:
            p = os.path.join(dp, f)
            try:
                if os.path.islink(p) or not os.path.isfile(p):
                    continue
                with open(p, 'rb') as h:
                    b = h.read(52)
            except OSError:
                continue
            # ELF32, big-endian, MIPS, N32 flag (EF_MIPS_ABI2) set
            if len(b) == 52 and b[:4] == b'\x7fELF' and b[4] == 1 and b[5] == 2 and struct.unpack('>H', b[18:20])[0] == 8 \
                    and struct.unpack('>I', b[36:40])[0] & 0x20 and struct.unpack('>H', b[16:18])[0] in (2, 3):
                out.append((p, struct.unpack('>H', b[16:18])[0]))
    return sorted(out)


def one(item):
    path, etype = item
    rel = path.replace(str(TREE), '')
    name = re.sub(r'[^A-Za-z0-9]', '_', rel)[-80:]
    d = WORK / name
    d.mkdir(parents=True, exist_ok=True)
    rec = {'path': rel, 'kind': 'prog' if etype == 2 else 'lib', 'funcs': 0, 'lifted': 0, 'status': '', 'reasons': {}}
    try:
        (d / 'e.json').write_text('{}')
        r = subprocess.run(['python3', str(TOOLS / 'lift_n64.py'), path, str(d), '--name', 'x', '--types', str(d / 'e.json'), '--protos', str(d / 'e.json')],
                           capture_output=True, text=True, timeout=3600)
        m = re.search(r'functions=(\d+) lifted=(\d+)', r.stdout)
        if not m:
            rec['status'] = 'lift failed: ' + (r.stderr.strip().splitlines() or ['?'])[-1][:100]
            return rec
        rec['funcs'], rec['lifted'] = int(m.group(1)), int(m.group(2))
        rec['reasons'] = dict(Counter(b for a, b in json.loads((d / 'needed.json').read_text())['failed']))
        c = subprocess.run(['gcc'] + CF + [str(d / 'code.c')], capture_output=True, text=True, timeout=3600)
        rec['status'] = 'ok' if c.returncode == 0 else 'compile failed: ' + (re.findall(r'error: (.*)', c.stderr) or ['?'])[0][:90]
        for f in ('code.c', 'data.s'):                # keep the disk: the sources are reproducible
            try:
                (d / f).unlink()
            except OSError:
                pass
    except Exception as ex:                           # noqa: BLE001
        rec['status'] = 'error: %s' % str(ex)[:100]
    return rec


def main():
    WORK.mkdir(parents=True, exist_ok=True)
    items = [(p, 3 if '.so' in p else 2) for p in sys.argv[1:]] or find()
    print('%d N32 objects' % len(items), flush=True)
    recs = []
    with ThreadPoolExecutor(max_workers=8) as ex:
        for rec in ex.map(one, items):
            recs.append(rec)
            print('%-4s %-56s %6d/%-6d %s' % (rec['kind'], rec['path'][-56:], rec['lifted'], rec['funcs'], rec['status']), flush=True)
    with open(OUT / 'summary.txt', 'w') as w:
        for r in recs:
            w.write('%-4s %-64s %6d/%-6d %s\n' % (r['kind'], r['path'][-64:], r['lifted'], r['funcs'], r['status']))
        for kind in ('lib', 'prog'):
            k = [r for r in recs if r['kind'] == kind]
            w.write('\n%s: %d objects, %d compile clean, %d of %d functions lifted, %d with every function lifted\n' % (
                kind, len(k), sum(r['status'] == 'ok' for r in k), sum(r['lifted'] for r in k), sum(r['funcs'] for r in k),
                sum(r['funcs'] > 0 and r['lifted'] == r['funcs'] for r in k)))
    why = Counter()
    for r in recs:
        why.update(r['reasons'])
    with open(OUT / 'reasons.txt', 'w') as w:
        for reason, n in why.most_common():
            w.write('%7d %s\n' % (n, reason))
        st = Counter(re.sub(r'[0-9a-fx_]{6,}', '..', r['status']) for r in recs if r['status'] != 'ok')
        w.write('\n')
        for reason, n in st.most_common(30):
            w.write('%7d %s\n' % (n, reason))
    (OUT / 'summary.json').write_text(json.dumps(recs))


if __name__ == '__main__':
    main()
