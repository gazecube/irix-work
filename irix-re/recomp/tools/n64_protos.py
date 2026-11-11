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
and a trailing '...' in the argument list for a variadic function.  'ptr' lists [argument, size] for the arguments
that point to a writable object of known size (XEvent *, int *, Window **): the callee reads and writes it with
native widths, so the lifter brings that memory to its native layout before the call (lm_native).  A third element
lists the object's scalar fields as [offset, size] (unions left out) and a fourth the argument that counts the
elements when the pointer is an array (XPoint, XRectangle, XColor and the like: the int that follows it), or -1.  The lifter stores
each field with its own width before the call (lm_conv) and records those widths after it (lm_mark): lifted code
writes two shorts with one store and reads the low 16 bits of an int by a 16-bit load.
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
        return 'f' if size == 4 else 'd' if size == 8 else 'q'      # q: long double, not the IRIX format
    if size >= 8:
        return 'p'
    if n['tag'] == 'enumeration_type':
        return 'u4' if size == 4 else 'u%d' % size
    return ('u' if 'unsigned' in enc or 'boolean' in enc or enc.endswith('_UTF)') else 'i') + str(size)


def scalars(d, t, base, out):
    """Scalar fields outside unions and bit fields."""
    t = d.strip(t)
    if t is None or len(out) > 64:
        return
    n = d.nodes[t]
    tag = n['tag']
    if tag in ('pointer_type', 'reference_type'):
        out.append([base, 8])
    elif tag in ('base_type', 'enumeration_type'):
        if d.size(t) in (1, 2, 4, 8):
            out.append([base, d.size(t)])
    elif tag in ('structure_type', 'class_type'):
        for k in n['kids']:
            if k['tag'] == 'member' and 'bit_size' not in k['a'] and 'type' in k['a']:
                scalars(d, d.ref(k['a']['type']), base + d.num(k, 'data_member_location', 0), out)
    elif tag == 'array_type':
        et = d.ref(n['a']['type'])
        for i in range(min(d.count(n), 64)):
            scalars(d, et, base + i * d.size(et), out)


ARRAYS = {'XPoint', 'XSegment', 'XRectangle', 'XArc', 'XColor', 'XTextItem', 'XTextItem16', 'XChar2b'}


def array_type(d, t):
    """True for a pointer to one of the Xlib types that are passed as counted arrays."""
    t = d.strip(t)
    if t is None or d.nodes[t]['tag'] != 'pointer_type' or 'type' not in d.nodes[t]['a']:
        return False
    t = d.ref(d.nodes[t]['a']['type'])
    while t is not None and t in d.nodes:
        n = d.nodes[t]
        m = re.search(r'"([^"]*)"', n['a'].get('name', ''))
        if m and m.group(1) in ARRAYS:
            return True
        if n['tag'] not in ('typedef', 'const_type', 'volatile_type') or 'type' not in n['a']:
            return False
        t = d.ref(n['a']['type'])
    return False


def pointee(d, t):
    """Type and size of the object a pointer parameter points to; size 0 for const, char, void, function and
    incomplete types."""
    t = d.strip(t)
    if t is None or d.nodes[t]['tag'] != 'pointer_type' or 'type' not in d.nodes[t]['a']:
        return None, 0
    t = d.ref(d.nodes[t]['a']['type'])
    while t is not None and t in d.nodes and d.nodes[t]['tag'] in ('typedef', 'volatile_type', 'restrict_type'):
        t = d.ref(d.nodes[t]['a']['type']) if 'type' in d.nodes[t]['a'] else None
    if t is None or t not in d.nodes:
        return None, 0
    n = d.nodes[t]
    if n['tag'] == 'const_type':             # the callee only reads it; a structure still has to be in its layout
        t = d.strip(t)
        if t is None or t not in d.nodes or d.nodes[t]['tag'] not in ('structure_type', 'class_type'):
            return None, 0
        n = d.nodes[t]
    if n['tag'] == 'subroutine_type' or 'declaration' in n['a']:
        return None, 0
    if n['tag'] in ('pointer_type', 'reference_type'):
        return t, 8
    size = d.size(t)
    if n['tag'] == 'base_type' and size == 1:
        return None, 0
    return t, (size if 1 < size <= 4096 else 0)


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
        params, ptr = [], []
        formals = [k for k in fn['kids'] if k['tag'] in ('formal_parameter', 'unspecified_parameters')]
        for j, k in enumerate(formals):
            if k['tag'] == 'formal_parameter':
                params.append(klass(d, d.ref(k['a']['type'])) if 'type' in k['a'] else 'p')
                pt, size = pointee(d, d.ref(k['a']['type'])) if 'type' in k['a'] else (None, 0)
                if size:
                    fields = []
                    scalars(d, pt, 0, fields)
                    # an array of drawing primitives or colours: the int that follows is the element count
                    cnt = -1
                    if array_type(d, d.ref(k['a']['type'])) and j + 1 < len(formals) and formals[j + 1]['tag'] == 'formal_parameter' and 'type' in formals[j + 1]['a']:
                        if klass(d, d.ref(formals[j + 1]['a']['type'])) in ('i4', 'u4'):
                            cnt = j + 1
                    ptr.append([j, size, fields if 0 < len(fields) <= 64 else [], cnt])
            else:
                params.append('...')
        if ret == 'q' or 'q' in params:
            continue                          # called without a prototype; its results are not usable
        out[nm.group(1)] = {'ret': ret, 'args': params}
        if ptr:
            out[nm.group(1)]['ptr'] = ptr
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
