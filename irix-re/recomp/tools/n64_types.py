#!/usr/bin/env python3
"""Field layouts of the exported data of an IRIX N64 library, from the headers that declare it.

    n64_types.py LIB.so OUT.json -I DIR ... -h HEADER ... [--like NAME=DECLARED ...]

--like gives a record that no header declares the type of one that a header does declare.

N64 and x86_64 are both LP64 with the same alignment rules, so the layout the host compiler gives a type is the
layout the IRIX compiler gave it.  For every exported data symbol that the headers declare, a probe file defines a
variable of the same type; its DWARF gives the offset and size of every scalar field.  Members that point to an
array of structures and have a sibling count (resources / num_resources, syn_resources / num_syn_resources, ...)
are recorded too, so the lifter can type the static tables the records point to.

The lifter uses this to turn the big-endian initial data into native data field by field (lift_n64.py --types).
"""
import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path

here = Path(__file__).resolve().parent
sys.path.insert(0, str(here))
sys.path.insert(0, str(here.parents[2] / 'native-libs' / 'nativemem' / 'tools'))
from lift_n64 import Elf                                  # noqa: E402  (either ELF class)
import nm_swap                                            # noqa: E402


class Layout(nm_swap.Dwarf):
    """LP64 view of the type tree (the base class was written for N32)."""
    size_of_pointer = 8
    def size(self, t):
        t = self.strip(t)
        if t is None:
            return 1
        n = self.nodes[t]
        if n['tag'] in ('pointer_type', 'reference_type', 'subroutine_type'):
            return 8
        bs = self.num(n, 'byte_size')
        if bs is not None:
            return bs
        if n['tag'] == 'array_type':
            return self.count(n) * self.size(self.ref(n['a']['type']))
        return 1

    def fields(self, t, base, out):
        t = self.strip(t)
        if t is None:
            return
        n = self.nodes[t]
        tag = n['tag']
        if tag in ('pointer_type', 'reference_type'):
            out.append((base, 8))
        elif tag in ('base_type', 'enumeration_type'):
            out.append((base, self.size(t)))
        elif tag in ('structure_type', 'class_type'):
            for k in n['kids']:
                if k['tag'] == 'member' and 'bit_size' not in k['a'] and 'type' in k['a']:
                    self.fields(self.ref(k['a']['type']), base + self.num(k, 'data_member_location', 0), out)
        elif tag == 'union_type':
            best = None
            for k in n['kids']:
                if k['tag'] == 'member' and 'type' in k['a']:
                    sz = self.size(self.ref(k['a']['type']))
                    if best is None or sz > best[0]:
                        best = (sz, k)
            if best:
                self.fields(self.ref(best[1]['a']['type']), base, out)
        elif tag == 'array_type':
            et = self.ref(n['a']['type'])
            es = self.size(et)
            for i in range(min(self.count(n), 1 << 16)):
                self.fields(et, base + i * es, out)

    def extensions(self, t, base, out):
        """Offsets of the `extension` pointers of a class record: each points to a chain of extension records."""
        t = self.strip(t)
        if t is None:
            return
        n = self.nodes[t]
        if n['tag'] not in ('structure_type', 'class_type'):
            return
        for k in n['kids']:
            if k['tag'] != 'member' or 'type' not in k['a']:
                continue
            nm = re.search(r'"([^"]*)"', k['a'].get('name', ''))
            kt = self.strip(self.ref(k['a']['type']))
            off = base + self.num(k, 'data_member_location', 0)
            if kt is not None and self.nodes[kt]['tag'] in ('structure_type', 'class_type'):
                self.extensions(kt, off, out)
            elif nm and nm.group(1) == 'extension' and kt is not None and self.nodes[kt]['tag'] == 'pointer_type':
                out.append(off)

    def follows(self, t, base, out):
        """(pointer offset, count offset, count size, element type) for counted pointers to structures."""
        t = self.strip(t)
        if t is None:
            return
        n = self.nodes[t]
        if n['tag'] not in ('structure_type', 'class_type'):
            return
        members = {}
        for k in n['kids']:
            if k['tag'] == 'member' and 'type' in k['a']:
                nm = re.search(r'"([^"]*)"', k['a'].get('name', ''))
                members[nm.group(1) if nm else ''] = k
        for name, k in members.items():
            kt = self.strip(self.ref(k['a']['type']))
            off = base + self.num(k, 'data_member_location', 0)
            if kt is None:
                continue
            kn = self.nodes[kt]
            if kn['tag'] in ('structure_type', 'class_type'):
                self.follows(kt, off, out)
            elif kn['tag'] == 'pointer_type' and 'type' in kn['a']:
                pt = self.strip(self.ref(kn['a']['type']))
                cnt = members.get('num_' + name)
                if pt is not None and cnt is not None and self.nodes[pt]['tag'] in ('structure_type', 'class_type'):
                    out.append((off, base + self.num(cnt, 'data_member_location', 0), self.size(self.ref(cnt['a']['type'])), pt))


class Layout32(Layout):
    """ILP32 view: pointers are 4 bytes."""
    size_of_pointer = 4
    def size(self, t):
        t = self.strip(t)
        if t is not None and self.nodes[t]['tag'] in ('pointer_type', 'reference_type', 'subroutine_type'):
            return 4
        return Layout.size(self, t)

    def fields(self, t, base, out):
        k = len(out)
        Layout.fields(self, t, base, out)
        st = self.strip(t)
        if st is not None and self.nodes[st]['tag'] in ('pointer_type', 'reference_type'):
            out[k:] = [(base, 4)]


def main():
    args = sys.argv[1:]
    lib, outp = args[0], args[1]
    incs = [args[i + 1] for i, a in enumerate(args) if a == '-I']
    hdrs = [args[i + 1] for i, a in enumerate(args) if a == '-h']
    like = dict(args[i + 1].split('=') for i, a in enumerate(args) if a == '--like')
    e = Elf(lib)
    names = sorted({s['name'] for s in e.dynsyms if s['type'] == 1 and s['shndx'] and re.fullmatch(r'[A-Za-z_]\w*', s['name'])})
    tmp = Path(tempfile.mkdtemp(prefix='n64types', dir=str(Path(outp).resolve().parent)))
    src, obj = tmp / 'probe.c', tmp / 'probe.o'
    m32 = '--m32' in args                     # N32 objects: the ILP32 layout of gcc -m32 with 8-byte alignment of doubles
    cc = ['gcc', '-m32', '-malign-double'] if m32 else ['gcc', '-m64']
    cc += ['-g', '-O0', '-w', '-fmax-errors=0', '-c', str(src), '-o', str(obj)] + ['-I' + i for i in incs]
    for _ in range(6):
        src.write_text(''.join('#include <%s>\n' % h for h in hdrs) +
                       ''.join('__typeof__(%s) T_%s;\n' % (like.get(n, n), n) for n in names))
        r = subprocess.run(cc, capture_output=True, text=True)
        if r.returncode == 0:
            break
        bad = set(re.findall(r"[‘'`](\w+)[’'] undeclared", r.stderr))
        bad |= {n for n in names if like.get(n) in bad}
        if not bad:
            sys.stderr.write(r.stderr[:3000])
            sys.exit('probe does not compile')
        names = [n for n in names if n not in bad]
    d = Layout32(str(obj)) if m32 else Layout(str(obj))
    out = {}
    for n in d.nodes.values():
        if n['tag'] != 'variable' or 'type' not in n['a']:
            continue
        nm = re.search(r'"T_(\w+)"', n['a'].get('name', ''))
        if not nm:
            continue
        t = d.ref(n['a']['type'])
        fields, fol = [], []
        d.fields(t, 0, fields)
        d.follows(t, 0, fol)
        follow = []
        for poff, coff, csz, pt in fol:
            ef = []
            d.fields(pt, 0, ef)
            follow.append({'ptr': poff, 'count': coff, 'count_size': csz, 'elem_size': d.size(pt), 'fields': ef})
        ext = []
        d.extensions(t, 0, ext)
        out[nm.group(1)] = {'size': d.size(t), 'fields': fields, 'follow': follow, 'ext': ext}
    Path(outp).write_text(json.dumps(out))
    sizes = {s['name']: s['size'] for s in e.dynsyms if s['type'] == 1 and s['shndx']}
    diff = [(k, v['size'], sizes[k]) for k, v in out.items() if sizes.get(k) and sizes[k] != v['size']]
    print('%d exported data symbols, %d typed from the headers, %d with a size different from the library' % (
        len(sizes), len(out), len(diff)))
    for k, a, b in diff[:12]:
        print('   %-40s headers %d, library %d' % (k, a, b))


if __name__ == '__main__':
    main()
