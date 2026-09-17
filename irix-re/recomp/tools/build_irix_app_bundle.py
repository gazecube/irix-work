#!/usr/bin/env python3
"""Build one IRIX application bundle from the ecosystem manifest."""
import argparse
import json
import os
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path


def run(cmd, **kw):
    print('+', ' '.join(map(str, cmd)), flush=True)
    subprocess.run(list(map(str, cmd)), check=True, **kw)


def load_manifest(path):
    data = json.loads(Path(path).read_text())
    if not isinstance(data.get('apps'), list):
        raise SystemExit('manifest has no apps list')
    if not isinstance(data.get('shared_components'), list):
        raise SystemExit('manifest has no shared_components list')
    return data


def validate_component(row, label):
    for key in ('prefix', 'kind', 'path'):
        if not row.get(key):
            raise SystemExit(f'{label} is missing {key}')
    if row['kind'] not in ('runtime', 'internal'):
        raise SystemExit(f"{label} has invalid kind {row['kind']!r}")
    if not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]*', row['prefix']):
        raise SystemExit(f"{label} has invalid C prefix {row['prefix']!r}")


def find_app(manifest, name):
    matches = [a for a in manifest['apps'] if a.get('name') == name]
    if len(matches) != 1:
        raise SystemExit(f'expected exactly one manifest app named {name!r}')
    app = matches[0]
    validate_component(app, f'app {name}')
    return app


def resolve_binary(row, runtime_tree, internal_tree):
    root = runtime_tree if row['kind'] == 'runtime' else internal_tree
    binary = root / row['path']
    if not binary.is_file():
        raise SystemExit(f"missing {row['prefix']}: {binary}")
    return binary


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--manifest', type=Path, required=True)
    ap.add_argument('--list-apps', action='store_true')
    ap.add_argument('--app')
    ap.add_argument('--runtime-tree', type=Path)
    ap.add_argument('--internal-tree', type=Path)
    ap.add_argument('--core-link', type=Path)
    ap.add_argument('-o', '--output', type=Path)
    ap.add_argument('--cc', default=os.environ.get('CC', 'cc'))
    ap.add_argument('--ar', default=os.environ.get('AR', 'ar'))
    ap.add_argument('-j', '--jobs', type=int, default=max(1, os.cpu_count() or 1))
    ap.add_argument('--generate-only', action='store_true')
    a = ap.parse_args()

    manifest = load_manifest(a.manifest)
    if a.list_apps:
        for app in manifest['apps']:
            name = app.get('name')
            validate_component(app, f'app {name}')
            if not name or not re.fullmatch(r'[A-Za-z0-9_]+', name):
                raise SystemExit(f'invalid app name {name!r}')
            print(f"{name}|{app['prefix']}|{app['kind']}|{app['path']}")
        return

    missing = [name for name in ('app', 'runtime_tree', 'internal_tree', 'core_link', 'output')
               if getattr(a, name) is None]
    if missing:
        ap.error('missing build arguments: ' + ', '.join('--' + x.replace('_', '-') for x in missing))

    app = find_app(manifest, a.app)
    for i, row in enumerate(manifest['shared_components']):
        validate_component(row, f'shared component {i}')

    here = Path(__file__).resolve().parent
    out = a.output.resolve()
    gen = out / 'generated'
    obj = out / 'obj'
    gen.mkdir(parents=True, exist_ok=True)

    rows = [app] + list(manifest['shared_components'])
    components = []
    for row in rows:
        components.append((row['prefix'], resolve_binary(row, a.runtime_tree, a.internal_tree)))

    # Audit the complete app closure first so decoder holes are reported once.
    run([sys.executable, here / 'audit_n32_bundle.py', *[p for _, p in components]])

    bundles = []
    for prefix, binary in components:
        dst = gen / prefix
        run([sys.executable, here / 'recomp_elf_bundle.py', binary,
             '-o', dst, '--prefix', prefix])
        bundles.append(dst / 'bundle.json')

    bind = gen / 'bindings'
    run([sys.executable, here / 'gen_prebound_bindings.py', a.core_link,
         *bundles, '-o', bind])

    descriptor = gen / 'app_components.c'
    with descriptor.open('w') as w:
        w.write('#include "IrixGuest.h"\n#include <stddef.h>\n\n')
        w.write('typedef struct IrixAppGuestComponent {\n')
        w.write('    int kind; /* 0 = runtime tree, 1 = internal tree */\n')
        w.write('    const char *path;\n')
        w.write('} IrixAppGuestComponent;\n\n')
        for row in rows:
            w.write(f"void register_{row['prefix']}(IrixGuestRuntime *rt);\n")
        w.write('\nconst IrixAppGuestComponent irix_app_guest_components[] = {\n')
        for row in rows:
            kind = 0 if row['kind'] == 'runtime' else 1
            w.write(f'    {{{kind}, {json.dumps(row["path"])}}},\n')
        w.write('};\n')
        w.write('const size_t irix_app_guest_component_count =\n')
        w.write('    sizeof(irix_app_guest_components) / sizeof(irix_app_guest_components[0]);\n\n')
        w.write('void irix_register_app_bundle(IrixGuestRuntime *rt)\n{\n')
        for row in rows:
            w.write(f"    register_{row['prefix']}(rt);\n")
        w.write('}\n')

    summary = {
        'app': app,
        'components': [json.loads(p.read_text()) for p in bundles],
        'core_link': str(a.core_link),
        'bindings': str(bind / 'bindings.json'),
    }
    (out / f"{a.app}.json").write_text(json.dumps(summary, indent=2) + '\n')
    if a.generate_only:
        return

    obj.mkdir(parents=True, exist_ok=True)
    sources = []
    for bundle in bundles:
        d = bundle.parent
        sources.extend(sorted(d.glob('*.c')))
    sources.append(bind / 'bindings.c')
    sources.append(descriptor)

    inc = here.parent / 'runtime'
    header = inc / 'IrixGuest.h'

    def compile_one(src):
        rel = src.relative_to(gen)
        dst = obj / (str(rel).replace('/', '__') + '.o')
        newest_input = max(src.stat().st_mtime_ns, header.stat().st_mtime_ns)
        if dst.is_file() and dst.stat().st_mtime_ns >= newest_input:
            return src, dst, None
        cmd = [a.cc, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
               '-I', str(inc), '-c', str(src), '-o', str(dst)]
        p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
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

    lib = out / f"libirix_{a.app}_bundle.a"
    mri = out / 'archive.mri'
    if lib.exists():
        lib.unlink()
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
