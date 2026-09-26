#!/usr/bin/env python3
"""Generate and compile the translated IRIX N32 Xt/Motif/SGI core stack."""
import argparse, json, os, re, subprocess, sys
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

def c_ident(s):
    return re.sub(r'[^A-Za-z0-9_]', '_', s)

def write_if_changed(path, text):
    path = Path(path)
    if path.is_file() and path.read_text() == text:
        return False
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)
    return True

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('development_tree', type=Path)
    ap.add_argument('-o', '--output', type=Path, required=True)
    ap.add_argument('--cc', default=os.environ.get('CC', 'cc'))
    ap.add_argument('--ar', default=os.environ.get('AR', 'ar'))
    ap.add_argument('--runtime-lib', type=Path,
                    help='native libirixre shared runtime to link generated DSOs against')
    ap.add_argument('-j', '--jobs', type=int, default=max(1, os.cpu_count() or 1))
    ap.add_argument('--generate-only', action='store_true')
    a = ap.parse_args()

    here = Path(__file__).resolve().parent
    root = a.development_tree.resolve()
    out = a.output.resolve()
    gen = out / 'generated'
    obj = out / 'obj'
    shared_obj = out / 'shared-obj'
    archives = []
    archive_labels = {}
    for label, rel in LIBS:
        p = root / rel
        if not p.is_file():
            raise SystemExit(f'missing N32 {label} archive: {p}')
        p = p.resolve()
        archives.append(p)
        archive_labels[p] = label

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
    shared_obj.mkdir(parents=True, exist_ok=True)
    manifest = json.loads((gen / 'functions.json').read_text())
    sources = []
    seen_sources = set()
    source_archive = {}
    for row in manifest.get('functions', []):
        src = Path(row['source'])
        if src not in seen_sources:
            sources.append(src)
            seen_sources.add(src)
        source_archive[src] = Path(row['archive']).resolve()
    sources.append(gen / 'registry.c')
    inc = here.parent / 'runtime'
    header = inc / 'IrixGuest.h'
    compile_flags = '-std=c11 -O0 -march=native -mtune=native -pipe -Wall -Wextra -Werror -fPIC\n'
    flags_stamp = out / 'compile_flags.txt'
    if not flags_stamp.is_file() or flags_stamp.read_text() != compile_flags:
        flags_stamp.write_text(compile_flags)

    def object_path(src, base):
        rel = src.relative_to(gen)
        return base / (str(rel).replace('/', '__') + '.o')

    def compile_one(src, base=obj):
        dst = object_path(src, base)
        if dst.is_file() and dst.stat().st_mtime_ns >= max(src.stat().st_mtime_ns, header.stat().st_mtime_ns, flags_stamp.stat().st_mtime_ns):
            return src, dst, None
        cmd = [a.cc, '-std=c11', '-O0', '-march=native', '-mtune=native', '-pipe', '-Wall', '-Wextra', '-Werror', '-fPIC',
               '-I', str(inc), '-c', str(src), '-o', str(dst)]
        p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        return src, dst, p

    objects = []
    source_objects = {}
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
            source_objects[src] = dst
            if i % 10 == 0 or i == len(futures):
                print(f'processed {i}/{len(futures)} rebuilt={rebuilt} reused={reused}', flush=True)

    runtime_lib = a.runtime_lib.resolve() if a.runtime_lib else None
    if runtime_lib is not None and not runtime_lib.is_file():
        raise SystemExit(f'missing shared recomp runtime: {runtime_lib}')

    # Keep the monolithic archive as a bootstrap/debug artifact for the current
    # runners. The canonical library-shaped outputs below are PIC ELF DSOs.
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

    # Split the statically translated core back into the original library
    # boundaries. These DSOs deliberately expose a registration entry point:
    # translated N32 functions still execute through IrixGuestCpu rather than
    # pretending to have the host ABI of Xt/Motif.
    shared_outputs = []
    for archive, label in archive_labels.items():
        rows = [row for row in manifest.get('functions', [])
                if Path(row['archive']).resolve() == archive]
        if not rows:
            print(f'warning: no translated functions for {archive}; skipping lib{label}.so',
                  file=sys.stderr)
            continue

        unique_sources = []
        seen = set()
        for row in rows:
            src = Path(row['source'])
            if src not in seen:
                unique_sources.append(src)
                seen.add(src)

        reg = gen / 'shared' / label / 'registry.c'
        reg_fn = f'irix_register_recompiled_{c_ident(label)}'
        text = ['#include "IrixGuest.h"\n\n']
        for row in rows:
            text.append(f'extern void {row["c_name"]}(IrixGuestRuntime *, IrixGuestCpu *);\n')
        text.append(f'\nvoid {reg_fn}(IrixGuestRuntime *rt)\n{{\n')
        for row in rows:
            name = json.dumps(row['name'])
            text.append(f'    irix_guest_register_recompiled_function(rt, 0x{row["address"]:08x}u, {row["c_name"]}, {name});\n')
        text.append('}\n')
        write_if_changed(reg, ''.join(text))

        reg_src, reg_obj, p = compile_one(reg, shared_obj)
        if p is not None and p.returncode:
            sys.stderr.write(p.stdout + p.stderr)
            raise SystemExit(f'compile failed: {reg_src}')

        dso = out / f'lib{label}.so'
        dso_objects = [source_objects[src] for src in unique_sources] + [reg_obj]
        link_cmd = [a.cc, '-shared', f'-Wl,-soname,lib{label}.so',
                    '-Wl,-z,defs', '-o', dso, *dso_objects]
        if runtime_lib is not None:
            # Use the exact build-tree runtime and record a stable DT_NEEDED
            # identity (libirixre.so.1), not an accidental executable export.
            link_cmd += [str(runtime_lib)]
        link_cmd += ['-lm']
        run(link_cmd)
        shared_outputs.append(dso)

    print(f'archive rebuilt from PIC objects: rebuilt={rebuilt} reused={reused}')
    print(lib)
    for dso in shared_outputs:
        print(dso)

if __name__ == '__main__':
    main()
