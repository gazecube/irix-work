#!/usr/bin/env python3
"""Compare struct layouts (size, member offsets) between an N32 build and a native
i386 build of the same sources, using their DWARF.   nm_layout_diff.py N32.so I386.so"""
import re
import sys
sys.path.insert(0, __file__.rsplit('/', 1)[0])
from nm_swap import Dwarf


def structs(path):
    d = Dwarf(path)
    out = {}
    for n in d.nodes.values():
        if n['tag'] not in ('structure_type', 'union_type'):
            continue
        m = re.search(r'"([^"]*)"', n['a'].get('name', ''))
        if not m or 'declaration' in n['a']:
            continue
        mem = {}
        for k in n['kids']:
            if k['tag'] == 'member':
                km = re.search(r'"([^"]*)"', k['a'].get('name', ''))
                if km:
                    mem[km.group(1)] = (d.num(k, 'data_member_location', 0), d.size(d.ref(k['a']['type'])) if 'type' in k['a'] else 0)
        out.setdefault(m.group(1), (d.num(n, 'byte_size', 0), mem))
    return out


a, b = structs(sys.argv[1]), structs(sys.argv[2])
bad = 0
for name in sorted(set(a) & set(b)):
    sa, ma = a[name]
    sb, mb = b[name]
    diffs = [k for k in ma if k in mb and ma[k][0] != mb[k][0]]
    if sa != sb or diffs:
        bad += 1
        print('%-28s size %4d vs %4d  first member offset diffs: %s' % (name, sa, sb, ', '.join('%s %d/%d' % (k, ma[k][0], mb[k][0]) for k in diffs[:3])))
print('compared %d structs, %d differ' % (len(set(a) & set(b)), bad))
