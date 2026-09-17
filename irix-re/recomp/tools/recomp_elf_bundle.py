#!/usr/bin/env python3
"""Translate callable functions in an IRIX N32 executable or DSO."""
import argparse, contextlib, json, re, struct, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent))
import translate_n32_regimm as t

SHT_SYMTAB=2; SHT_DYNSYM=11; STT_FUNC=2; SHF_EXECINSTR=0x4

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

def emit_explicit(path, name, start, size, c_prefix):
    """Emit one desktop/runtime function from an explicit address range.

    This intentionally lives here instead of changing translate_n32.py so
    metadata recovery cannot invalidate the static core translator.
    """
    elf=t.Elf32BE(str(path))
    words=elf.words(start,size)
    decoded=[]
    for i,word in enumerate(words):
        addr=start+i*4
        lines,control=t.decode(addr,word)
        decoded.append((addr,word,lines,control))
    fn='irix_recomp_%s%s'%(t.c_name(c_prefix),t.c_name(name))
    print('/* Generated from %s: %s @ 0x%08x, 0x%x bytes (explicit range). */'%(path,name,start,size))
    print('#include "IrixGuest.h"'); print('#include <math.h>'); print('')
    print('enum { IRIX_PENDING_NONE, IRIX_PENDING_JUMP, IRIX_PENDING_CALL, IRIX_PENDING_RETURN };'); print('')
    print('void %s(IrixGuestRuntime *rt, IrixGuestCpu *cpu)'%fn); print('{')
    print('    IrixGuestAddr pc = 0x%08xu;'%start)
    print('    IrixGuestAddr pending_target = 0;')
    print('    IrixGuestAddr pending_return = 0;')
    print('    int pending_kind = IRIX_PENDING_NONE;')
    print('    int old_pending;')
    print('    IrixGuestAddr old_target;')
    print('    IrixGuestAddr old_return;'); print('')
    print('    for (;;) {')
    print('        old_pending = pending_kind;')
    print('        old_target = pending_target;')
    print('        old_return = pending_return;')
    print('        pending_kind = IRIX_PENDING_NONE;')
    print('        cpu->pc = pc;'); print('')
    print('        switch (pc) {')
    for addr,word,lines,_ in decoded:
        print('        case 0x%08xu: /* 0x%08x */'%(addr,word))
        for line in lines: print('            '+line)
        print('            break;')
    print('        default:'); print('            return;'); print('        }'); print('')
    print('        cpu->gpr[0] = 0;')
    print('        if (old_pending == IRIX_PENDING_RETURN)')
    print('            return;')
    print('        if (old_pending == IRIX_PENDING_CALL) {')
    print('            if (irix_guest_call(rt, cpu, old_target) != 0)')
    print('                return;')
    print('            pc = old_return;')
    print('            continue;')
    print('        }')
    print('        if (old_pending == IRIX_PENDING_JUMP) {')
    print('            pc = old_target;')
    print('            continue;')
    print('        }')
    print('        pc += 4;')
    print('    }')
    print('}')

def zero_size_function_ranges(elf, symbol_rows):
    """Recover named zero-sized STT_FUNC symbols conservatively.

    IRIX/EDG can leave compiler-generated ctor/dtor helpers with a valid
    STT_FUNC address but st_size == 0.  Derive their extent only from the next
    distinct defined function start in the same executable section, or the
    section end.  Never cross sections and never invent a function start.
    """
    out=[]
    defined=[s for s in symbol_rows if s['shndx']!=0 and s['address']]
    for sec_index,sec in enumerate(elf.sections):
        if not (sec['flags']&SHF_EXECINSTR) or not sec['size']:
            continue
        lo=sec['addr']; hi=lo+sec['size']
        rows=[s for s in defined if s['shndx']==sec_index and lo<=s['address']<hi]
        if not rows: continue
        starts=sorted(set(s['address'] for s in rows)+{hi})
        next_boundary={starts[i]:starts[i+1] for i in range(len(starts)-1)}
        for s in rows:
            if s['size']!=0: continue
            end=next_boundary.get(s['address'])
            if end is None: continue
            size=end-s['address']
            if size<=0 or size%4: continue
            out.append((s['name'],s['address'],size,sec_index))
    return out

def pdr_function_ranges(elf, path, symbol_rows):
    """Recover procedure starts/ranges from the MIPS ELF .pdr section.

    SGI/MIPS runtime PDR entries are 32 bytes.  The first word is the
    procedure start address.  Use only starts that fall inside executable
    sections, and derive each extent from the next known callable boundary in
    the same executable section or the section end.
    """
    p=Path(path).read_bytes()
    pdr=next((s for s in elf.sections if s.get('name')=='.pdr'),None)
    if not pdr or not pdr['size'] or pdr['size']%32:
        return []

    starts=[]
    for off in range(pdr['offset'],pdr['offset']+pdr['size'],32):
        if off+32>len(p): break
        addr=struct.unpack_from('>I',p,off)[0]
        if addr and addr!=0xffffffff:
            starts.append(addr)
    starts=sorted(set(starts))

    named_by_addr={}
    known_starts=set()
    for s in symbol_rows:
        if s['shndx']!=0 and s['address']:
            named_by_addr.setdefault(s['address'],s['name'])
            known_starts.add(s['address'])

    out=[]
    for sec_index,sec in enumerate(elf.sections):
        if not (sec['flags']&SHF_EXECINSTR) or not sec['size']:
            continue
        lo=sec['addr']; hi=lo+sec['size']
        pstarts=[a for a in starts if lo<=a<hi]
        if not pstarts: continue
        boundaries=sorted(set(pstarts+[a for a in known_starts if lo<=a<hi]+[hi]))
        next_boundary={boundaries[i]:boundaries[i+1] for i in range(len(boundaries)-1)}
        for addr in pstarts:
            end=next_boundary.get(addr,hi)
            size=end-addr
            if size<=0 or size%4: continue
            name=named_by_addr.get(addr,'__pdr_%08x'%addr)
            out.append((name,addr,size,sec_index))
    return out

def main():
    ap=argparse.ArgumentParser(); ap.add_argument('binary'); ap.add_argument('-o','--output',required=True); ap.add_argument('--prefix')
    a=ap.parse_args(); path=Path(a.binary); out=Path(a.output); prefix=a.prefix or path.stem; out.mkdir(parents=True,exist_ok=True)
    elf=t.Elf32BE(str(path)); defs=[]
    for n,addr,size,sh in elf.symbols():
        if size: defs.append((n,addr,size,sh,'symbol'))

    raw_syms=raw_function_symbols(path)
    symbol_addrs={addr for _,addr,_,_,_ in defs}

    zero_defs=[]
    for n,addr,size,sh in zero_size_function_ranges(elf,raw_syms):
        if addr not in symbol_addrs:
            zero_defs.append((n,addr,size,sh,'zero_size'))

    recovered_addrs=symbol_addrs|{addr for _,addr,_,_,_ in zero_defs}
    pdr_defs=[]
    for n,addr,size,sh in pdr_function_ranges(elf,path,raw_syms):
        if addr not in recovered_addrs:
            pdr_defs.append((n,addr,size,sh,'pdr'))

    seen_addr=set(); functions=[]; pdr_count=0; zero_count=0
    all_defs=sorted(defs+zero_defs+pdr_defs,key=lambda x:(x[1],x[0]))
    for i,(n,addr,size,sh,source_kind) in enumerate(all_defs):
        if addr in seen_addr: continue
        seen_addr.add(addr); dst=out/(f'{i:04d}_{ident(n)}.c')
        with dst.open('w') as f, contextlib.redirect_stdout(f):
            if source_kind in ('pdr','zero_size'):
                emit_explicit(str(path),n,addr,size,prefix+'_')
            else:
                t.emit(str(path),n,c_prefix=prefix+'_')
        functions.append(dict(name=n,address=addr,size=size,c_name='irix_recomp_'+ident(prefix+'_')+ident(n),source=str(dst),source_kind=source_kind))
        if source_kind=='pdr': pdr_count+=1
        if source_kind=='zero_size': zero_count+=1

    # Keep every undefined function symbol, even when st_value is zero.  IRIX
    # quickstart/prebound binaries may leave the dynamic symbol value unset
    # while the corresponding GOT entry already contains a callable prebound
    # address.  gen_prebound_bindings.py recovers that address from the GOT.
    imports=[s for s in raw_syms if s['shndx']==0]
    meta={'path':str(path),'prefix':prefix,'functions':functions,'imports':imports}
    (out/'bundle.json').write_text(json.dumps(meta,indent=2)+'\n')
    with (out/'registry.c').open('w') as w:
        w.write('#include "IrixGuest.h"\n')
        for r in functions: w.write(f'extern void {r["c_name"]}(IrixGuestRuntime*, IrixGuestCpu*);\n')
        w.write(f'void register_{ident(prefix)}(IrixGuestRuntime *rt){{\n')
        for r in functions: w.write(f'  irix_guest_register_function(rt,0x{r["address"]:08x}u,{r["c_name"]},"{r["name"]}");\n')
        w.write('}\n')
    print(f'{path}: functions={len(functions)} zero_size_recovered={zero_count} pdr_recovered={pdr_count} imports={len(imports)}')
if __name__=='__main__': main()
