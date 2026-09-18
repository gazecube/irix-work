#!/usr/bin/env python3
"""Build one IRIX application bundle from the ecosystem manifest."""
import argparse
import json
import os
import re
import subprocess
import sys
import struct
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path


def run(cmd, **kw):
    print('+', ' '.join(map(str, cmd)), flush=True)
    subprocess.run(list(map(str, cmd)), check=True, **kw)


def write_if_changed(path, text):
    path = Path(path)
    if path.is_file() and path.read_text() == text:
        return False
    path.write_text(text)
    return True


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
    if row['kind'] not in ('runtime', 'internal', 'dev'):
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


def root_for_kind(kind, runtime_tree, internal_tree, dev_tree):
    if kind == 'runtime':
        return runtime_tree
    if kind == 'internal':
        return internal_tree
    if kind == 'dev':
        return dev_tree
    raise SystemExit(f'unknown component kind {kind!r}')


def resolve_binary(row, runtime_tree, internal_tree, dev_tree):
    root = root_for_kind(row['kind'], runtime_tree, internal_tree, dev_tree)
    binary = root / row['path']
    if not binary.is_file():
        raise SystemExit(f"missing {row['prefix']}: {binary}")
    return binary


def elf_needed(path):
    data = Path(path).read_bytes()
    if len(data) < 52 or data[:6] != b'\x7fELF\x01\x02':
        return []
    h = struct.unpack_from('>16sHHIIIIIHHHHHH', data, 0)
    shoff, shentsz, shnum = h[6], h[11], h[12]
    if not shoff or not shentsz or not shnum:
        return []
    secs = [struct.unpack_from('>IIIIIIIIII', data, shoff + i * shentsz)
            for i in range(shnum)]
    out = []
    for s in secs:
        if s[1] != 6 or not s[9]:  # SHT_DYNAMIC
            continue
        if s[6] >= len(secs):
            continue
        strings = secs[s[6]]
        strtab = data[strings[4]:strings[4] + strings[5]]
        for off in range(s[4], s[4] + s[5], s[9]):
            tag, value = struct.unpack_from('>II', data, off)
            if tag == 0:
                break
            if tag != 1:  # DT_NEEDED
                continue
            end = strtab.find(b'\0', value)
            if end < 0:
                continue
            name = strtab[value:end].decode('ascii', 'replace')
            if name and name not in out:
                out.append(name)
    return out


def elf_soname(path):
    data = Path(path).read_bytes()
    if len(data) < 52 or data[:6] != b'\x7fELF\x01\x02':
        return None
    h = struct.unpack_from('>16sHHIIIIIHHHHHH', data, 0)
    shoff, shentsz, shnum = h[6], h[11], h[12]
    if not shoff or not shentsz or not shnum:
        return None
    secs = [struct.unpack_from('>IIIIIIIIII', data, shoff + i * shentsz)
            for i in range(shnum)]
    for s in secs:
        if s[1] != 6 or not s[9]:  # SHT_DYNAMIC
            continue
        if s[6] >= len(secs):
            continue
        strings = secs[s[6]]
        strtab = data[strings[4]:strings[4] + strings[5]]
        for off in range(s[4], s[4] + s[5], s[9]):
            tag, value = struct.unpack_from('>II', data, off)
            if tag == 0:
                break
            if tag != 14:  # DT_SONAME
                continue
            end = strtab.find(b'\0', value)
            if end < 0:
                return None
            return strtab[value:end].decode('ascii', 'replace')
    return None


def dependency_search_dirs(runtime_tree, internal_tree, dev_tree):
    rows = []
    def add(kind, root, rel):
        if root is None:
            return
        d = root / rel
        if d.is_dir():
            rows.append((kind, root, d))
    add('runtime', runtime_tree, 'usr/lib32')
    add('runtime', runtime_tree, 'usr/lib32/internal')
    add('runtime', runtime_tree, 'usr/Motif-1.2/lib32')
    add('runtime', runtime_tree, 'lib32')
    add('internal', internal_tree, 'usr/lib32/internal')
    add('internal', internal_tree, 'usr/lib32')
    add('dev', dev_tree, 'usr/lib32')
    add('dev', dev_tree, 'usr/Motif-1.2/lib32')
    return rows


def resolve_needed_library(name, runtime_tree, internal_tree, dev_tree):
    search = dependency_search_dirs(runtime_tree, internal_tree, dev_tree)
    for kind, root, directory in search:
        candidate = directory / name
        if candidate.is_file():
            return kind, candidate.relative_to(root), candidate

    # IRIX trees are not always laid out with a filename identical to the
    # DT_NEEDED string. Fall back to the ELF DT_SONAME so an unversioned
    # libC.so carrying SONAME libC.so.2 still resolves correctly.
    stem = name.split('.so', 1)[0] + '.so'
    for kind, root, directory in search:
        for candidate in sorted(directory.glob(stem + '*')):
            if not candidate.is_file():
                continue
            try:
                if elf_soname(candidate) == name:
                    return kind, candidate.relative_to(root), candidate
            except (OSError, ValueError, struct.error):
                continue
    return None


def dependency_prefix(name, used):
    stem = 'dso_' + re.sub(r'[^A-Za-z0-9_]', '_', name)
    if not re.match(r'[A-Za-z_]', stem):
        stem = 'dso_' + stem
    prefix = stem
    n = 2
    while prefix in used:
        prefix = f'{stem}_{n}'
        n += 1
    used.add(prefix)
    return prefix


def expand_dso_closure(rows, runtime_tree, internal_tree, dev_tree):
    expanded = list(rows)
    used_prefixes = {row['prefix'] for row in expanded}
    selected_paths = set()
    selected_sonames = set()
    queue = []

    for row in expanded:
        binary = resolve_binary(row, runtime_tree, internal_tree, dev_tree)
        selected_paths.add(binary.resolve())
        selected_sonames.add(binary.name)
        queue.append((row, binary))

    while queue:
        parent, binary = queue.pop(0)
        for soname in elf_needed(binary):
            if soname in selected_sonames:
                continue
            found = resolve_needed_library(soname, runtime_tree, internal_tree, dev_tree)
            if found is None:
                print(f'warning: unresolved DT_NEEDED {soname} required by {binary}', file=sys.stderr)
                selected_sonames.add(soname)
                continue
            kind, relpath, dep = found
            real = dep.resolve()
            selected_sonames.add(soname)
            if real in selected_paths:
                continue
            row = {
                'prefix': dependency_prefix(soname, used_prefixes),
                'kind': kind,
                'path': str(relpath),
                'needed_by': str(binary),
            }
            expanded.append(row)
            selected_paths.add(real)
            queue.append((row, dep))

    return expanded


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--manifest', type=Path, required=True)
    ap.add_argument('--list-apps', action='store_true')
    ap.add_argument('--app')
    ap.add_argument('--runtime-tree', type=Path)
    ap.add_argument('--internal-tree', type=Path)
    ap.add_argument('--dev-tree', type=Path)
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

    missing = [name for name in ('app', 'runtime_tree', 'internal_tree', 'dev_tree', 'core_link', 'output')
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

    rows = expand_dso_closure([app] + list(manifest['shared_components']),
                              a.runtime_tree, a.internal_tree, a.dev_tree)
    components = []
    for row in rows:
        components.append((row['prefix'],
                           resolve_binary(row, a.runtime_tree, a.internal_tree, a.dev_tree)))
    print(f'DSO closure: components={len(rows)} auto_dependencies={len(rows) - 1 - len(manifest["shared_components"])}',
          flush=True)

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
    descriptor_text = []
    descriptor_text.append('#include "IrixGuest.h"\n#include <stddef.h>\n\n')
    descriptor_text.append('typedef struct IrixAppGuestComponent {\n')
    descriptor_text.append('    int kind; /* 0 = runtime tree, 1 = internal tree, 2 = development tree */\n')
    descriptor_text.append('    const char *path;\n')
    descriptor_text.append('} IrixAppGuestComponent;\n\n')
    for row in rows:
        descriptor_text.append(f"void register_{row['prefix']}(IrixGuestRuntime *rt);\n")
    descriptor_text.append('\nconst IrixAppGuestComponent irix_app_guest_components[] = {\n')
    for row in rows:
        kind = {'runtime': 0, 'internal': 1, 'dev': 2}[row['kind']]
        descriptor_text.append(f'    {{{kind}, {json.dumps(row["path"])}}},\n')
    descriptor_text.append('};\n')
    descriptor_text.append('const size_t irix_app_guest_component_count =\n')
    descriptor_text.append('    sizeof(irix_app_guest_components) / sizeof(irix_app_guest_components[0]);\n\n')
    descriptor_text.append('void irix_register_app_bundle(IrixGuestRuntime *rt)\n{\n')
    for row in rows:
        descriptor_text.append(f"    register_{row['prefix']}(rt);\n")
    descriptor_text.append('}\n')
    write_if_changed(descriptor, ''.join(descriptor_text))

    summary = {
        'app': app,
        'components': [json.loads(p.read_text()) for p in bundles],
        'core_link': str(a.core_link),
        'bindings': str(bind / 'bindings.json'),
    }
    write_if_changed(out / f"{a.app}.json", json.dumps(summary, indent=2) + '\n')
    if a.generate_only:
        return

    obj.mkdir(parents=True, exist_ok=True)
    sources = []
    seen_sources = set()
    for bundle in bundles:
        meta = json.loads(bundle.read_text())
        for row in meta.get('functions', []):
            src = Path(row['source'])
            if src not in seen_sources:
                sources.append(src)
                seen_sources.add(src)
        registry = bundle.parent / 'registry.c'
        if registry not in seen_sources:
            sources.append(registry)
            seen_sources.add(registry)
    sources.append(bind / 'bindings.c')
    sources.append(descriptor)

    inc = here.parent / 'runtime'
    header = inc / 'IrixGuest.h'
    compile_flags = '-std=c11 -O0 -Wall -Wextra -Werror\n'
    flags_stamp = out / 'compile_flags.txt'
    if not flags_stamp.is_file() or flags_stamp.read_text() != compile_flags:
        flags_stamp.write_text(compile_flags)

    def compile_one(src):
        rel = src.relative_to(gen)
        dst = obj / (str(rel).replace('/', '__') + '.o')
        newest_input = max(src.stat().st_mtime_ns, header.stat().st_mtime_ns, flags_stamp.stat().st_mtime_ns)
        if dst.is_file() and dst.stat().st_mtime_ns >= newest_input:
            return src, dst, None
        cmd = [a.cc, '-std=c11', '-O0', '-Wall', '-Wextra', '-Werror',
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
