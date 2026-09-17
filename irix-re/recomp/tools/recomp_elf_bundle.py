#!/usr/bin/env python3
"""Translate every recoverable function in an IRIX N32 executable or DSO."""
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

def recover_defined_functions(elf, path):
    """Return defined STT_FUNC symbols, inferring zero sizes conservatively.

    IRIX/EDG emits local compiler-generated ctor/dtor helpers with a valid
    function symbol/address but st_size == 0.  These are commonly reached only
    through function pointers, so dropping them makes indirect guest calls fail
    even though the code is present in the image.

    Infer a zero-sized symbol only from the next distinct function address in
    the same section, or the containing section end.  Never cross sections,
    never synthesize an unnamed function, and require a positive word-aligned
    extent.
    """
    raw=[s for s in raw_function_symbols(path) if s['shndx']!=0 and s['address']]
    by_section={}
    for s in raw:
        by_section.setdefault(s['shndx'],[]).append(s)

    recovered=[]
    for shndx, rows in by_section.items():
        rows=sorted(rows,key=lambda s:(s['address'], -s['size'], s['name']))
        distinct=sorted(set(s['address'] for s in rows))
        next_addr={distinct[i]:distinct[i+1] for i in range(len(distinct)-1)}
        sec=elf.sections[shndx] if 0 <= shndx < len(elf.sections) else None
        sec_end=(sec['addr']+sec['size']) if sec else 0
        for s in rows:
            size=s['size']
            inferred=False
            if size==0:
                end=next_addr.get(s['address'],sec_end)
                if end > s['address'] and (end-s['address']) % 4 == 0:
                    size=end-s['address']
                    inferred=True
            if size:
                recovered.append((s['name'],s['address'],size,s['shndx'],inferred))
    return recovered

def main():
    ap=argparse.ArgumentParser(); ap.add_argument('binary'); ap.add_argument('-o','--output',required=True); ap.add_argument('--prefix')
    a=ap.parse_args(); path=Path(a.binary); out=Path(a.output); prefix=a.prefix or path.stem; out.mkdir(parents=True,exist_ok=True)
    elf=t.Elf32BE(str(path)); defs=recover_defined_functions(elf,path)
    seen_addr=set(); functions=[]; inferred_count=0
    for i,(n,addr,size,sh,inferred) in enumerate(sorted(defs,key=lambda x:(x[1],x[0]))):
        # Multiple symbol tables often contain aliases for the same address.
        # One translated body/register entry per address is sufficient; prefer
        # the first stable name after address/name sorting.
        if addr in seen_addr: continue
        seen_addr.add(addr)
        dst=out/(f'{i:04d}_{ident(n)}.c')
        with dst.open('w') as f, contextlib.redirect_stdout(f):
            t.emit(str(path),n,c_prefix=prefix+'_',symbol_override=(n,addr,size,sh))
        functions.append(dict(name=n,address=addr,size=size,c_name='irix_recomp_'+ident(prefix+'_')+ident(n),source=str(dst),inferred_size=inferred))
        if inferred: inferred_count += 1
    # Keep every undefined function symbol, even when st_value is zero.  IRIX
    # quickstart/prebound binaries may leave the dynamic symbol value unset
    # while the corresponding GOT entry already contains a callable prebound
    # address.  gen_prebound_bindings.py recovers that address from the GOT.
    imports=[s for s in raw_function_symbols(path) if s['shndx']==0]
    meta={'path':str(path),'prefix':prefix,'functions':functions,'imports':imports}
    (out/'bundle.json').write_text(json.dumps(meta,indent=2)+'\n')
    with (out/'registry.c').open('w') as w:
        w.write('#include "IrixGuest.h"\n')
        for r in functions: w.write(f'extern void {r["c_name"]}(IrixGuestRuntime*, IrixGuestCpu*);\n')
        w.write(f'void register_{ident(prefix)}(IrixGuestRuntime *rt){{\n')
        for r in functions: w.write(f'  irix_guest_register_function(rt,0x{r["address"]:08x}u,{r["c_name"]},"{r["name"]}");\n')
        w.write('}\n')
    print(f'{path}: functions={len(functions)} inferred_zero_size={inferred_count} imports={len(imports)}')
if __name__=='__main__': main()
