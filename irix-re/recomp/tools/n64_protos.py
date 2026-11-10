#!/usr/bin/env python3
"""Return and argument classes of the functions an IRIX N64 library imports, from the host headers.

    n64_protos.py LIB.so OUT.json -I DIR ... -h HEADER ...

Lifted code calls native functions directly (lift_n64.py).  Two things differ between the ABIs and need the
callee's prototype:
  * a 32-bit (or narrower) integer result: x86_64 leaves the upper bits of the register undefined, MIPS code
    expects the value sign-extended (or zero-extended for unsigned char and short);
  * floating-point arguments and results travel in xmm registers, not in the integer argument positions.
For every imported function the headers declare, the probe takes its type; the DWARF gives the classes:
    v void, p pointer or 64-bit integer, i4 u4 i2 u2 i1 u1 narrower integers, f float, d double,
and a trailing '...' in the argument list for a variadic function.
"""
import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path

here = Path(__file__).resolve().parent
sys.path.insert(0, str(here))
from elf_n64 import Elf                                   # noqa: E402
from n64_types import Layout                              # noqa: E402


def klass(d, t):
    t = d.strip(t)
    if t is None:
        return 'v'
    n = d.nodes[t]
    if n['tag'] in ('pointer_type', 'reference_type'):
        return 'p'
    size = d.size(t)
    enc = n['a'].get('encoding', '')
    if 'float' in enc:
        return 'f' if size == 4 else 'd'
    if size >= 8:
        return 'p'
    if n['tag'] == 'enumeration_type':
        return 'u4' if size == 4 else 'u%d' % size
    return ('u' if 'unsigned' in enc or 'boolean' in enc or enc.endswith('_UTF)') else 'i') + str(size)


def main():
    args = sys.argv[1:]
    lib, outp = args[0], args[1]
    incs = [args[i + 1] for i, a in enumerate(args) if a == '-I']
    hdrs = [args[i + 1] for i, a in enumerate(args) if a == '-h']
    e = Elf(lib)
    names = sorted({s['name'] for s in e.dynsyms if not s['shndx'] and s['name'] and re.fullmatch(r'[A-Za-z_]\w*', s['name'])})
    tmp = Path(tempfile.mkdtemp(prefix='n64protos', dir=str(Path(outp).resolve().parent)))
    src, obj = tmp / 'probe.c', tmp / 'probe.o'
    cc = ['gcc', '-m64', '-g', '-O0', '-w', '-fmax-errors=0', '-D_GNU_SOURCE', '-c', str(src), '-o', str(obj)] + ['-I' + i for i in incs]
    for _ in range(8):
        src.write_text(''.join('#include <%s>\n' % h for h in hdrs) +
                       ''.join('__typeof__(&(%s)) P_%s;\n' % (n, n) for n in names))
        r = subprocess.run(cc, capture_output=True, text=True)
        if r.returncode == 0:
            break
        bad = set(re.findall(r"[‘'`](\w+)[’'] undeclared", r.stderr))
        bad |= {m for m in re.findall(r"probe\.c:\d+:\d+: error: .*?[‘'`](\w+)[’']", r.stderr) if m in names}
        if not bad:
            # drop the names on the lines that fail for any other reason
            lines = {int(m) for m in re.findall(r'probe\.c:(\d+):\d+: error', r.stderr)}
            text = src.read_text().splitlines()
            bad = {re.search(r'P_(\w+);', text[k - 1]).group(1) for k in lines if k <= len(text) and 'P_' in text[k - 1]}
            if not bad:
                sys.stderr.write(r.stderr[:3000])
                sys.exit('probe does not compile')
        names = [n for n in names if n not in bad]
    d = Layout(str(obj))
    out = {}
    for n in d.nodes.values():
        if n['tag'] != 'variable' or 'type' not in n['a']:
            continue
        nm = re.search(r'"P_(\w+)"', n['a'].get('name', ''))
        if not nm:
            continue
        t = d.strip(d.ref(n['a']['type']))
        if t is None or d.nodes[t]['tag'] != 'pointer_type' or 'type' not in d.nodes[t]['a']:
            continue
        f = d.strip(d.ref(d.nodes[t]['a']['type']))
        if f is None or d.nodes[f]['tag'] != 'subroutine_type':
            continue
        fn = d.nodes[f]
        ret = klass(d, d.ref(fn['a']['type'])) if 'type' in fn['a'] else 'v'
        params = []
        for k in fn['kids']:
            if k['tag'] == 'formal_parameter':
                params.append(klass(d, d.ref(k['a']['type'])) if 'type' in k['a'] else 'p')
            elif k['tag'] == 'unspecified_parameters':
                params.append('...')
        out[nm.group(1)] = {'ret': ret, 'args': params}
    Path(outp).write_text(json.dumps(out))
    funcs = [s['name'] for s in e.dynsyms if not s['shndx'] and s['type'] == 2 and s['name']]
    print('%d imported functions, %d with a prototype; narrow results: %d, floating point: %d' % (
        len(funcs), sum(1 for f in funcs if f in out),
        sum(1 for v in out.values() if v['ret'][0] in 'iu'),
        sum(1 for v in out.values() if v['ret'] in 'fd' or any(a in ('f', 'd') for a in v['args']))))
    missing = [f for f in funcs if f not in out]
    if missing:
        print('   no prototype: ' + ' '.join(missing[:60]))


if __name__ == '__main__':
    main()
