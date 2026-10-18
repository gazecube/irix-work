#!/usr/bin/env python3
"""Compile the translated C, embed the swapped image, export guest symbols as
absolute symbols and link a native i386 shared library.

    nm_pack.py GENDIR OUT.so [-m32]
"""
import json
import subprocess
import sys
from pathlib import Path

here = Path(__file__).resolve().parent.parent
gen = Path(sys.argv[1])
out = Path(sys.argv[2])
cc = ['gcc', '-m32']
link = json.load(open(gen / 'image' / 'link.json'))
funcs = json.load(open(gen / 'functions.json'))
fnames = {f['name'] for f in funcs['functions']}
work = out.parent / (out.stem + '.build')
work.mkdir(parents=True, exist_ok=True)

exp = ['.text']
for name, v in sorted(link['globals'].items()):
    addr = v[0] if isinstance(v, list) else v
    exp.append('.globl %s\n.type %s,%s\n.set %s, 0x%x' % (name, name, '@function' if name in fnames else '@object', name, addr))
(work / 'exports.S').write_text('\n'.join(exp) + '\n.section .note.GNU-stack,"",@progbits\n')

(work / 'image.S').write_text(
    '.section .rodata\n.globl nm_main_blob, nm_small_blob\n.balign 16\n'
    'nm_main_blob: .incbin "%s"\nnm_main_end:\n.balign 16\nnm_small_blob: .incbin "%s"\nnm_small_end:\n'
    '.globl nm_main_blob_size, nm_small_blob_size\n.balign 4\n'
    'nm_main_blob_size: .long nm_main_end - nm_main_blob\nnm_small_blob_size: .long nm_small_end - nm_small_blob\n'
    '.section .note.GNU-stack,"",@progbits\n' % (gen / 'image' / 'main.bin', gen / 'image' / 'small.bin'))

(work / 'init.c').write_text('''
#include "nm_rt.h"
extern const unsigned char nm_main_blob[], nm_small_blob[];
extern const unsigned nm_main_blob_size, nm_small_blob_size;
static void __attribute__((constructor)) nm_ctor(void)
{
    NmImage img = { 0x%x, 0x%x, 0x%x, nm_main_blob, nm_small_blob, nm_main_blob_size, nm_small_blob_size };
    nm_init(&img);
}
''' % (link['base'], link['small_base'], link['gp']))

objs = []
srcs = sorted(gen.glob('c/*/recomp_*.c')) + [gen / 'registry.c', here / 'rt' / 'nm_rt.c', work / 'init.c']
for s in srcs:
    o = work / (s.parent.name + '_' + s.stem + '.o')
    subprocess.run(cc + (['-g', '-O0'] if __import__('os').environ.get('NM_DEBUG') else ['-O1']) + ['-fPIC', '-w', '-std=gnu11', '-I', str(here / 'include'), '-I', str(here / 'rt'),
                         '-c', str(s), '-o', str(o)], check=True)
    objs.append(str(o))
for s in ('exports', 'image'):
    o = work / (s + '.o')
    subprocess.run(cc + ['-c', str(work / (s + '.S')), '-o', str(o)], check=True)
    objs.append(str(o))
subprocess.run(cc + ['-shared', '-o', str(out)] + objs + ['-ldl'], check=True)
print('built', out)
