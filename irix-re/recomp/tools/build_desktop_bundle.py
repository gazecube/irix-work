#!/usr/bin/env python3
"""Generate and compile IRIX 6.5 desktop plus its internal IMD DSOs."""
import argparse, json, os, subprocess, sys
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

COMPONENTS=[
    ('desktop','runtime','usr/sbin/desktop'),
    ('imdApp','internal','usr/lib32/internal/libimdApp.so'),
    ('imdUtil','internal','usr/lib32/internal/libimdUtil.so'),
    ('imdXutil','internal','usr/lib32/internal/libimdXutil.so'),
    ('imdModel','internal','usr/lib32/internal/libimdModel.so'),
    ('imdIconPanel','internal','usr/lib32/internal/libimdIconPanel.so'),
    ('imdFtrDB','internal','usr/lib32/internal/libimdFtrDB.so'),
    ('imdFtrGallery','internal','usr/lib32/internal/libimdFtrGallery.so'),
    ('imdFtrIcon','internal','usr/lib32/internal/libimdFtrIcon.so'),
]

def run(cmd,**kw):
    print('+',' '.join(map(str,cmd)),flush=True); subprocess.run(list(map(str,cmd)),check=True,**kw)

def main():
    ap=argparse.ArgumentParser(); ap.add_argument('--runtime-tree',type=Path,required=True); ap.add_argument('--internal-tree',type=Path,required=True); ap.add_argument('--core-link',type=Path,required=True); ap.add_argument('-o','--output',type=Path,required=True)
    ap.add_argument('--cc',default=os.environ.get('CC','cc')); ap.add_argument('--ar',default=os.environ.get('AR','ar')); ap.add_argument('-j','--jobs',type=int,default=max(1,os.cpu_count() or 1)); ap.add_argument('--generate-only',action='store_true'); a=ap.parse_args()
    here=Path(__file__).resolve().parent; out=a.output.resolve(); gen=out/'generated'; obj=out/'obj'; gen.mkdir(parents=True,exist_ok=True)
    components=[]
    for prefix,kind,rel in COMPONENTS:
        root=a.runtime_tree if kind=='runtime' else a.internal_tree; binary=root/rel
        if not binary.is_file(): raise SystemExit(f'missing {prefix}: {binary}')
        components.append((prefix,binary))

    # Fail once with the complete unsupported-instruction inventory instead of
    # discovering decoder holes one DSO/function at a time during generation.
    run([sys.executable,here/'audit_n32_bundle.py',*[p for _,p in components]])

    bundles=[]
    for prefix,binary in components:
        dst=gen/prefix; run([sys.executable,here/'recomp_elf_bundle.py',binary,'-o',dst,'--prefix',prefix]); bundles.append(dst/'bundle.json')
    bind=gen/'bindings'; run([sys.executable,here/'gen_prebound_bindings.py',a.core_link,*bundles,'-o',bind])
    summary={'components':[json.loads(p.read_text()) for p in bundles],'core_link':str(a.core_link),'bindings':str(bind/'bindings.json')}; (out/'desktop.json').write_text(json.dumps(summary,indent=2)+'\n')
    if a.generate_only:return
    obj.mkdir(parents=True,exist_ok=True); sources=[]
    for b in bundles:
        d=b.parent; sources.extend(sorted(d.glob('*.c'))); sources.append(d/'registry.c')
    sources.append(bind/'bindings.c'); inc=here.parent/'runtime'
    def compile_one(src):
        rel=src.relative_to(gen); dst=obj/(str(rel).replace('/','__')+'.o'); cmd=[a.cc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-I',str(inc),'-c',str(src),'-o',str(dst)]
        p=subprocess.run(cmd,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True); return src,dst,p
    objects=[]
    with ThreadPoolExecutor(max_workers=a.jobs) as ex:
        futures=[ex.submit(compile_one,s) for s in sources]
        for i,f in enumerate(as_completed(futures),1):
            src,dst,p=f.result()
            if p.returncode: sys.stderr.write(p.stdout+p.stderr); raise SystemExit(f'compile failed: {src}')
            objects.append(dst)
            if i%250==0 or i==len(futures): print(f'compiled {i}/{len(futures)}',flush=True)
    lib=out/'libirix_desktop_bundle.a'; mri=out/'archive.mri'
    if lib.exists():lib.unlink()
    with mri.open('w') as w:
        w.write(f'CREATE {lib}\n'); [w.write(f'ADDMOD {p}\n') for p in sorted(objects)]; w.write('SAVE\nEND\n')
    with mri.open('rb') as f: subprocess.run([a.ar,'-M'],stdin=f,check=True)
    print(lib)
if __name__=='__main__':main()
