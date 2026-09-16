#!/usr/bin/env python3
"""Translate every sized function in an IRIX N32 executable or DSO."""
import argparse, contextlib, json, re, struct, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent))
import translate_n32_regimm as t

SHT_SYMTAB=2; SHT_DYNSYM=11; STT_FUNC=2

def ident(s): return re.sub(r'[^A-Za-z0-9_]', '_', s)

def raw_function_symbols(path):
    p=Path(path).read_bytes(); h=struct.unpack_from('>16sHHIIIIIHHHHHH',p,0)
    shoff,esz,num=h[6],h[11],h[12]
    secs=[struct.unpack_from('>IIIIIIIIII',p,shoff+i*esz) for i in range(num)]
    rows=[]
    for s in secs:
        if s[1] not in (SHT_SYMTAB,SHT_DYNSYM) or not s[9]: continue
        ss=secs[s[6]]; strings=p[ss[4]:ss[4]+ss[5]]
        for i in range(s[5]//s[9]):
            n,v,z,info,other,sh=struct.unpack_from('>IIIBBH',p,s[4]+i*s[9])
            if (info&15)!=STT_FUNC: continue
            e=strings.find(b'\0',n); name=strings[n:e].decode('ascii','replace') if n else ''
            if name: rows.append(dict(name=name,address=v,size=z,shndx=sh,bind=info>>4))
    return rows

def main():
    ap=argparse.ArgumentParser(); ap.add_argument('binary'); ap.add_argument('-o','--output',required=True); ap.add_argument('--prefix')
    a=ap.parse_args(); path=Path(a.binary); out=Path(a.output); prefix=a.prefix or path.stem; out.mkdir(parents=True,exist_ok=True)
    elf=t.Elf32BE(str(path)); defs=[]
    for n,addr,size,sh in elf.symbols():
        if size: defs.append((n,addr,size,sh))
    seen=set(); functions=[]
    for i,(n,addr,size,sh) in enumerate(sorted(defs,key=lambda x:(x[1],x[0]))):
        if (n,addr) in seen: continue
        seen.add((n,addr)); dst=out/(f'{i:04d}_{ident(n)}.c')
        with dst.open('w') as f, contextlib.redirect_stdout(f): t.emit(str(path),n,c_prefix=prefix+'_')
        functions.append(dict(name=n,address=addr,size=size,c_name='irix_recomp_'+ident(prefix+'_')+ident(n),source=str(dst)))
    imports=[s for s in raw_function_symbols(path) if s['shndx']==0 and s['address']]
    meta={'path':str(path),'prefix':prefix,'functions':functions,'imports':imports}
    (out/'bundle.json').write_text(json.dumps(meta,indent=2)+'\n')
    with (out/'registry.c').open('w') as w:
        w.write('#include "IrixGuest.h"\n')
        for r in functions: w.write(f'extern void {r["c_name"]}(IrixGuestRuntime*, IrixGuestCpu*);\n')
        w.write(f'void register_{ident(prefix)}(IrixGuestRuntime *rt){{\n')
        for r in functions: w.write(f'  irix_guest_register_function(rt,0x{r["address"]:08x}u,{r["c_name"]},"{r["name"]}");\n')
        w.write('}\n')
    print(f'{path}: functions={len(functions)} imports={len(imports)}')
if __name__=='__main__': main()
