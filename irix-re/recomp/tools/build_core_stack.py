#!/usr/bin/env python3
"""Generate and compile the translated IRIX N32 Xt/Motif/SGI core stack."""
import argparse, os, subprocess, sys
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

LIBS = [
    ('Xt', 'usr/lib32/libXt.a'),
    ('Xm', 'usr/Motif-1.2/lib32/libXm.a'),
    ('Sgm', 'usr/Motif-1.2/lib32/libSgm.a'),
    ('vk', 'usr/Motif-1.2/lib32/libvk.a'),
    ('Mrm', 'usr/Motif-1.2/lib32/libMrm.a'),
    ('Uil', 'usr/Motif-1.2/lib32/libUil.a'),
]

def run(cmd, **kw):
    print('+', ' '.join(map(str, cmd)), flush=True)
    subprocess.run(list(map(str, cmd)), check=True, **kw)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('development_tree', type=Path)
    ap.add_argument('-o', '--output', type=Path, required=True)
    ap.add_argument('--cc', default=os.environ.get('CC', 'cc'))
    ap.add_argument('--ar', default=os.environ.get('AR', 'ar'))
    ap.add_argument('-j', '--jobs', type=int, default=max(1, os.cpu_count() or 1))
    ap.add_argument('--generate-only', action='store_true')
    a = ap.parse_args()

    here = Path(__file__).resolve().parent
    root = a.development_tree.resolve()
    out = a.output.resolve()
    gen = out / 'generated'
    obj = out / 'obj'
    archives = []
    for label, rel in LIBS:
        p = root / rel
        if not p.is_file():
            raise SystemExit(f'missing N32 {label} archive: {p}')
        archives.append(p)

    out.mkdir(parents=True, exist_ok=True)
    run([sys.executable, here / 'recomp_linked_stack.py', *archives, '-o', gen])
    if a.generate_only:
        return

    obj.mkdir(parents=True, exist_ok=True)
    sources = sorted((gen / 'c').rglob('*.c')) + [gen / 'registry.c']
    inc = here.parent / 'runtime'

    def compile_one(src):
        rel = src.relative_to(gen)
        dst = obj / (str(rel).replace('/', '__') + '.o')
        cmd = [a.cc, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
               '-I', str(inc), '-c', str(src), '-o', str(dst)]
        p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                           text=True)
        return src, dst, p

    objects = []
    with ThreadPoolExecutor(max_workers=a.jobs) as ex:
        futures = [ex.submit(compile_one, s) for s in sources]
        for i, f in enumerate(as_completed(futures), 1):
            src, dst, p = f.result()
            if p.returncode:
                sys.stderr.write(p.stdout + p.stderr)
                raise SystemExit(f'compile failed: {src}')
            objects.append(dst)
            if i % 250 == 0 or i == len(futures):
                print(f'compiled {i}/{len(futures)}', flush=True)

    lib = out / 'libirix_recompiled_stack.a'
    if lib.exists():
        lib.unlink()
    mri = out / 'archive.mri'
    with mri.open('w') as w:
        w.write(f'CREATE {lib}\n')
        for p in sorted(objects):
            w.write(f'ADDMOD {p}\n')
        w.write('SAVE\nEND\n')
    with mri.open('rb') as f:
        subprocess.run([a.ar, '-M'], stdin=f, check=True)
    print(lib)

if __name__ == '__main__':
    main()
