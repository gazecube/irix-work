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


def elf_interp(path):
    """Return an ELF32 big-endian PT_INTERP path, or None."""
    data = Path(path).read_bytes()
    if len(data) < 52 or data[:6] != b'\x7fELF\x01\x02':
        return None
    h = struct.unpack_from('>16sHHIIIIIHHHHHH', data, 0)
    phoff, phentsz, phnum = h[5], h[9], h[10]
    if not phoff or phentsz < 32 or not phnum:
        return None
    if phoff + phentsz * phnum > len(data):
        return None
    for i in range(phnum):
        off = phoff + i * phentsz
        p_type, p_offset, _, _, p_filesz, _, _, _ = struct.unpack_from('>IIIIIIII', data, off)
        if p_type != 3:  # PT_INTERP
            continue
        if not p_filesz or p_offset + p_filesz > len(data):
            return None
        raw = data[p_offset:p_offset + p_filesz]
        end = raw.find(b'\0')
        if end >= 0:
            raw = raw[:end]
        return raw.decode('ascii', 'replace') or None
    return None


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
    # Prefer SGI's generic N32 SERVER graphics personality for the initial
    # desktop bring-up.  This keeps the original IRIS GL -> IGLOO -> OpenGL
    # binary chain intact without pulling in the IP30/MGRAS kernel interface.
    # libgl.so itself remains the normal runtime-tree IGLOO DSO; its libGL
    # dependency is resolved from SERVER32 here.
    add('runtime', runtime_tree, 'usr/gfx/arch/SERVER32')
    add('runtime', runtime_tree, 'usr/lib32')
    add('runtime', runtime_tree, 'usr/lib32/internal')
    add('runtime', runtime_tree, 'usr/Motif-1.2/lib32')
    add('runtime', runtime_tree, 'lib32')
    # N32 ISA-runtime fallback.  Keep dependency discovery strictly inside
    # the lib32 ABI universe; /usr/lib and /usr/lib64 are O32/N64.
    add('runtime', runtime_tree, 'usr/lib32/mips3')
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
    ap.add_argument('--runtime-lib', type=Path,
                    help='native libirixre shared runtime to link generated DSOs against')
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
    dso_dir = out / 'lib'
    gen.mkdir(parents=True, exist_ok=True)

    seed_rows = [app] + list(manifest['shared_components'])

    # PT_INTERP is not DT_NEEDED.  For IRIX N32 executables this is normally
    # /usr/lib32/libc.so.1, whose entry point is the actual kernel transfer
    # target.  Seed it explicitly so the runner can enter the original
    # libc/rld startup path instead of jumping straight to the executable.
    app_binary = resolve_binary(app, a.runtime_tree, a.internal_tree, a.dev_tree)
    interp = elf_interp(app_binary)
    if interp:
        interp_rel = interp[1:] if interp.startswith('/') else interp
        interp_candidates = [interp_rel]
        # IRIX N32 executables may name /usr/lib32/libc.so.1 in PT_INTERP
        # while the extracted runtime tree carries that interpreter at
        # /lib32/libc.so.1. Accept the extracted-layout variant without
        # rewriting the guest-visible interpreter identity.
        if interp_rel == 'usr/lib32/libc.so.1':
            interp_candidates.append('lib32/libc.so.1')

        interp_found = None
        for candidate in interp_candidates:
            candidate_path = a.runtime_tree / candidate
            if candidate_path.is_file():
                interp_found = (candidate, candidate_path)
                break

        if interp_found is not None:
            candidate, interp_path = interp_found
            seed_rows.append({
                'prefix': 'irix_interp',
                'kind': 'runtime',
                'path': candidate,
                'implicit_runtime': True,
                'interp_name': interp,
            })
            print(f'IRIX PT_INTERP: {interp} -> {interp_path}', file=sys.stderr)
        else:
            checked = ', '.join(str(a.runtime_tree / p)
                                for p in interp_candidates)
            print(f'warning: missing IRIX PT_INTERP {interp}; checked: {checked}',
                  file=sys.stderr)

    # rld is the IRIX N32 runtime linker, not an ordinary DT_NEEDED DSO.  SGI
    # libc reaches its private _rld_* interface directly, so DT_NEEDED-only
    # closure discovery cannot pull it in.  Keep the original N32 rld binary
    # in the guest closure rather than replacing that interface with a host
    # shim.
    # IRIX installs the N32 runtime linker in /lib32, not /usr/lib32.
    # Prefer the normal runtime image; retain the /usr/lib32 and debug
    # locations only as fallbacks for alternate/extracted media layouts.
    rld_rel = None
    rld_candidates = (
        'lib32/rld',
        'lib32/rld.debug',
        'usr/lib32/rld',
        'usr/lib32/rld.debug',
    )
    for candidate in rld_candidates:
        if (a.runtime_tree / candidate).is_file():
            rld_rel = candidate
            break
    if rld_rel is not None:
        seed_rows.append({
            'prefix': 'irix_rld',
            'kind': 'runtime',
            'path': rld_rel,
            'implicit_runtime': True,
        })
        print(f'IRIX N32 runtime linker: {a.runtime_tree / rld_rel}', file=sys.stderr)
    else:
        checked = ', '.join(str(a.runtime_tree / p) for p in rld_candidates)
        print(f'warning: missing N32 IRIX runtime linker; checked: {checked}',
              file=sys.stderr)

    rows = expand_dso_closure(seed_rows,
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
    descriptor_text.append('    int role; /* 0 = ordinary, 1 = PT_INTERP, 2 = rld */\n')
    descriptor_text.append('    const char *path;\n')
    descriptor_text.append('} IrixAppGuestComponent;\n\n')
    process_rows = [row for row in rows
                    if row is app or row.get('implicit_runtime')]
    for row in process_rows:
        descriptor_text.append(f"void register_{row['prefix']}(IrixGuestRuntime *rt);\n")
    descriptor_text.append('\nconst IrixAppGuestComponent irix_app_guest_components[] = {\n')
    for row in rows:
        kind = {'runtime': 0, 'internal': 1, 'dev': 2}[row['kind']]
        role = 1 if row['prefix'] == 'irix_interp' else (2 if row['prefix'] == 'irix_rld' else 0)
        descriptor_text.append(f'    {{{kind}, {role}, {json.dumps(row["path"])}}},\n')
    descriptor_text.append('};\n')
    descriptor_text.append('const size_t irix_app_guest_component_count =\n')
    descriptor_text.append('    sizeof(irix_app_guest_components) / sizeof(irix_app_guest_components[0]);\n\n')
    descriptor_text.append('void irix_register_app_bundle(IrixGuestRuntime *rt)\n{\n')
    for row in process_rows:
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
    compile_flags = '-std=c11 -O0 -Wall -Wextra -Werror -fPIC\n'
    flags_stamp = out / 'compile_flags.txt'
    if not flags_stamp.is_file() or flags_stamp.read_text() != compile_flags:
        flags_stamp.write_text(compile_flags)

    def compile_one(src):
        rel = src.relative_to(gen)
        dst = obj / (str(rel).replace('/', '__') + '.o')
        newest_input = max(src.stat().st_mtime_ns, header.stat().st_mtime_ns, flags_stamp.stat().st_mtime_ns)
        if dst.is_file() and dst.stat().st_mtime_ns >= newest_input:
            return src, dst, None
        cmd = [a.cc, '-std=c11', '-O0', '-Wall', '-Wextra', '-Werror', '-fPIC',
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

    # Emit each original IRIX shared component as its own native ELF DSO.
    # Keep the app, PT_INTERP and rld in the bootstrap archive for now: those
    # are process-image components, not libraries.
    runtime_lib = a.runtime_lib.resolve() if a.runtime_lib else None
    if runtime_lib is not None and not runtime_lib.is_file():
        raise SystemExit(f'missing shared recomp runtime: {runtime_lib}')
    dso_dir.mkdir(parents=True, exist_ok=True)
    object_by_source = {src: dst for src, dst in
                        ((src, obj / (str(src.relative_to(gen)).replace('/', '__') + '.o'))
                         for src in sources)}
    # First assign every translated DSO its output path. The second pass can
    # then reproduce the source ELF's DT_NEEDED edges without depending on
    # manifest/closure order.
    dso_rows = []
    dso_by_soname = {}
    for row, bundle in zip(rows, bundles):
        if row.get('implicit_runtime') or row is app:
            continue
        binary = resolve_binary(row, a.runtime_tree, a.internal_tree, a.dev_tree)
        soname = elf_soname(binary) or binary.name
        if '.so' not in soname:
            continue
        dso = dso_dir / soname
        dso_rows.append((row, bundle, binary, soname, dso))
        dso_by_soname[soname] = dso

    shared_outputs = []
    for row, bundle, binary, soname, dso in dso_rows:
        meta = json.loads(bundle.read_text())
        component_sources = []
        seen = set()
        for fn in meta.get('functions', []):
            src = Path(fn['source'])
            if src not in seen:
                component_sources.append(src)
                seen.add(src)
        registry = bundle.parent / 'registry.c'
        component_sources.append(registry)
        component_objects = [object_by_source[src] for src in component_sources]
        link_cmd = [a.cc, '-shared', f'-Wl,-soname,{soname}', '-Wl,-z,defs',
                    '-Wl,--no-as-needed', '-o', dso, *component_objects]
        if runtime_lib is not None:
            link_cmd.append(str(runtime_lib))

        # Preserve every original IRIX DT_NEEDED edge whose provider is part
        # of this translated closure. --no-as-needed matters here: translated
        # guest calls dispatch by guest address, so the host linker cannot see
        # a normal ELF symbol reference even though the dependency is real.
        for needed in elf_needed(binary):
            dep = dso_by_soname.get(needed)
            if dep is not None and dep != dso:
                link_cmd.append(str(dep))
            elif needed not in dso_by_soname:
                print(f'warning: native DSO {soname}: translated provider for '
                      f'DT_NEEDED {needed} is not in this closure',
                      file=sys.stderr)

        link_cmd.append('-lm')
        run(link_cmd)
        shared_outputs.append(dso)

    # The bootstrap archive now contains only process-image translation
    # (application, PT_INTERP, rld) plus bindings/descriptor glue. Shared
    # libraries stay exclusively in their native DSO artifacts above.
    process_sources = []
    process_seen = set()
    for row, bundle in zip(rows, bundles):
        if row is not app and not row.get('implicit_runtime'):
            continue
        meta = json.loads(bundle.read_text())
        for fn in meta.get('functions', []):
            src = Path(fn['source'])
            if src not in process_seen:
                process_sources.append(src)
                process_seen.add(src)
        registry = bundle.parent / 'registry.c'
        if registry not in process_seen:
            process_sources.append(registry)
            process_seen.add(registry)
    process_sources += [bind / 'bindings.c', descriptor]
    process_objects = [object_by_source[src] for src in process_sources]

    lib = out / f"libirix_{a.app}_bundle.a"
    mri = out / 'archive.mri'
    if lib.exists():
        lib.unlink()
    with mri.open('w') as w:
        w.write(f'CREATE {lib}\n')
        for p in sorted(process_objects):
            w.write(f'ADDMOD {p}\n')
        w.write('SAVE\nEND\n')
    with mri.open('rb') as f:
        subprocess.run([a.ar, '-M'], stdin=f, check=True)

    print(f'archive rebuilt from objects: rebuilt={rebuilt} reused={reused}')
    print(lib)
    for dso in shared_outputs:
        print(dso)


if __name__ == '__main__':
    main()
