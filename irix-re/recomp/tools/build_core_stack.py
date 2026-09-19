#!/usr/bin/env python3
"""Generate and compile the translated IRIX N32 Xt/Motif/SGI core stack."""
import argparse, json, os, subprocess, sys
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

def discover_support_archives(root, primary):
    primary_real = {p.resolve() for p in primary}
    roots = [
        root / 'usr/lib32',
        root / 'usr/Motif-1.2/lib32',
    ]
    found = []
    seen = set()
    for base in roots:
        if not base.is_dir():
            continue
        for p in sorted(base.rglob('*.a')):
            # Architecture-tuned subtrees are alternate MIPS3/MIPS4 runtime
            # universes, not generic N32 support archives for this closure.
            rel_parts = p.relative_to(base).parts[:-1]
            if any(part in ('mips3', 'mips4') for part in rel_parts):
                continue
            real = p.resolve()
            if real in primary_real or real in seen:
                continue
            try:
                with p.open('rb') as f:
                    if f.read(8) != b'!<arch>\n':
                        continue
            except OSError:
                continue
            seen.add(real)
            found.append(p)
    return found

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

    support_archives = discover_support_archives(root, archives)
    out.mkdir(parents=True, exist_ok=True)
    print(f'core archives: primary={len(archives)} support_candidates={len(support_archives)}', flush=True)
    cmd = [sys.executable, here / 'recomp_linked_stack.py', *archives]
    for p in support_archives:
        cmd += ['--support-archive', p]
    cmd += ['-o', gen]
    run(cmd)
    if a.generate_only:
        return

    obj.mkdir(parents=True, exist_ok=True)
    manifest = json.loads((gen / 'functions.json').read_text())
    sources = []
    seen_sources = set()
    for row in manifest.get('functions', []):
        src = Path(row['source'])
        if src not in seen_sources:
            sources.append(src)
            seen_sources.add(src)
    sources.append(gen / 'registry.c')
    inc = here.parent / 'runtime'
    header = inc / 'IrixGuest.h'
    compile_flags = '-std=c11 -O0 -Wall -Wextra -Werror\n'
    flags_stamp = out / 'compile_flags.txt'
    if not flags_stamp.is_file() or flags_stamp.read_text() != compile_flags:
        flags_stamp.write_text(compile_flags)

    def compile_one(src):
        rel = src.relative_to(gen)
        dst = obj / (str(rel).replace('/', '__') + '.o')
        # Generated sources are written only when their contents change. Reuse
        # an object whenever it is newer than both its source and runtime ABI.
        if dst.is_file() and dst.stat().st_mtime_ns >= max(src.stat().st_mtime_ns, header.stat().st_mtime_ns, flags_stamp.stat().st_mtime_ns):
            return src, dst, None
        cmd = [a.cc, '-std=c11', '-O0', '-Wall', '-Wextra', '-Werror',
               '-I', str(inc), '-c', str(src), '-o', str(dst)]
        p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                           text=True)
        return src, dst, p

    objects = []
    rebuilt = 0
    reused = 0
    with ThreadPoolExecutor(max_workers=a.jobs) as ex:
        futures = [ex.submit(compile_one, s) for s in sources]
        for i, f in enumerate(as_completed(futures), 1):
            src, dst, p = f.result()
            if p is None:
                reused += 1
            else:
                if p.returncode:
                    sys.stderr.write(p.stdout + p.stderr)
                    raise SystemExit(f'compile failed: {src}')
                rebuilt += 1
            objects.append(dst)
            if i % 250 == 0 or i == len(futures):
                print(f'processed {i}/{len(futures)} rebuilt={rebuilt} reused={reused}', flush=True)

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
    print(f'archive rebuilt from objects: rebuilt={rebuilt} reused={reused}')
    print(lib)

if __name__ == '__main__':
    main()
