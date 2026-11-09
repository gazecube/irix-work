#!/usr/bin/env python3
"""Lift one function out of an already generated tree (raw words from the C comments) and compile it.
usage: lift_test.py GENDIR NAME [OUT.c]"""
import glob, json, re, sys
sys.path.insert(0, '.')
import lift_n32 as L
gen, name = sys.argv[1], sys.argv[2]
fs = [f for f in json.load(open(gen + '/functions.json'))['functions'] if f['name'] == name]
f = fs[0]
words = {}
for p in glob.glob(gen + '/c/*/recomp_*.c'):
    for m in re.finditer(r'/\* 0x([0-9a-f]{8}): 0x([0-9a-f]{8})', open(p).read()):
        a = int(m[1], 16)
        if f['address'] <= a < f['address'] + f['size']:
            words[a] = int(m[2], 16)
ws = [words[f['address'] + 4 * k] for k in range(f['size'] // 4)]
text, params = L.lift(ws, f['address'], f['name'] + '_lifted')
hdr = '#include <stdint.h>\n'
open(sys.argv[3] if len(sys.argv) > 3 else '/dev/stdout', 'w').write(hdr + text + '\n')
print('params:', params, file=sys.stderr)
