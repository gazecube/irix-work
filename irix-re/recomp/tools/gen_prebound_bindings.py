#!/usr/bin/env python3
"""Bind IRIX executable/DSO quickstart and prebound GOT imports by symbol name."""
import argparse, json, struct
from pathlib import Path

STT_NOTYPE=0
STT_OBJECT=1
STT_FUNC=2

def dyn_got(path):
    p=Path(path).read_bytes(); h=struct.unpack_from('>16sHHIIIIIHHHHHH',p,0); phoff,phentsz,phnum=h[5],h[9],h[10]
    ph=[struct.unpack_from('>IIIIIIII',p,phoff+i*phentsz) for i in range(phnum)]
    def voff(v):
        for typ,off,va,pa,fs,ms,fl,al in ph:
            if typ==1 and va<=v<va+fs: return off+v-va
        raise KeyError(v)
    dyn={}
    for typ,off,va,pa,fs,ms,fl,al in ph:
        if typ!=2: continue
        for q in range(off,off+fs,8):
            tag,val=struct.unpack_from('>II',p,q); dyn[tag]=val
            if tag==0: break
    if 3 not in dyn or 0x7000000a not in dyn or 0x70000013 not in dyn: return {}
    got,local_gotno,gotsym,symtab,strtab,syment=dyn[3],dyn[0x7000000a],dyn[0x70000013],dyn[6],dyn[5],dyn[11]
    nsyms=dyn.get(0x70000011,0); out={}
    for i in range(gotsym,nsyms):
        off=voff(symtab+i*syment); n,v,z,info,other,sh=struct.unpack_from('>IIIBBH',p,off)
        if not n: continue
        so=voff(strtab+n); e=p.find(b'\0',so); name=p[so:e].decode('ascii','replace')
        ge=got+(local_gotno+i-gotsym)*4
        try: val=struct.unpack_from('>I',p,voff(ge))[0]
        except KeyError: continue
        out[name]=dict(value=val,got=ge,type=info&15,bind=info>>4,shndx=sh,size=z,sym_value=v)
    return out

def main():
    ap=argparse.ArgumentParser(); ap.add_argument('stack_link_json'); ap.add_argument('bundles',nargs='+'); ap.add_argument('-o','--output',required=True)
    a=ap.parse_args(); bundles=[json.loads(Path(p).read_text()) for p in a.bundles]; link=json.loads(Path(a.stack_link_json).read_text()); defs={}
    for b in bundles:
        for f in b['functions']: defs.setdefault(f['name'],f['address'])
    for name,addr in link['globals'].items(): defs.setdefault(name,addr)

    entries=[]; data_patches=[]
    for b in bundles:
        got=dyn_got(b['path'])
        for imp in b['imports']:
            name=imp['name']; addrs=[]; row=got.get(name)
            if imp['address']: addrs.append(imp['address'])
            if row and row['value']: addrs.append(row['value'])
            for stub in addrs: entries.append((stub,defs.get(name,0),name,b['prefix']))

        # The desktop/internal DSOs are loaded at their original virtual
        # addresses, but their prebound GOT may still contain pointers into
        # SGI shared libraries that are replaced by the synthetic core image.
        # Rebind undefined data objects to the matching core global before
        # guest execution. Function imports remain handled by the forwarding
        # table above.
        for name,row in got.items():
            if row['shndx']!=0 or row['type']!=STT_OBJECT:
                continue
            target=link['globals'].get(name,0)
            if target:
                data_patches.append((row['got'],target,name,b['prefix'],row['value']))

    by={}; conflicts=[]
    for e in entries:
        addr,target,name,owner=e; old=by.get(addr)
        if old is None or (target and not old[1]): by[addr]=e
        elif target and old[1] and target!=old[1]: conflicts.append((addr,old,e))
    if conflicts:
        for x in conflicts[:20]: print('CONFLICT',x)
        raise SystemExit(f'{len(conflicts)} binding conflicts')

    # A GOT slot is unique within one loaded image. If duplicate metadata
    # reaches us, require it to resolve to the same target rather than silently
    # choosing one.
    data_by={}
    for e in data_patches:
        got_addr,target,name,owner,old=e; prev=data_by.get(got_addr)
        if prev and prev[1]!=target:
            raise SystemExit(f'data binding conflict at 0x{got_addr:08x}: {prev} vs {e}')
        data_by[got_addr]=e
    data_patches=sorted(data_by.values())

    resolved=sorted(e for e in by.values() if e[1]); missing=sorted(e for e in by.values() if not e[1]); all_entries=sorted(by.values())
    target_entries=sorted({(target,name) for addr,target,name,owner in resolved if target})
    missing_index={addr:i for i,(addr,target,name,owner) in enumerate(missing)}
    out=Path(a.output); out.mkdir(parents=True,exist_ok=True)
    with (out/'bindings.c').open('w') as w:
        w.write('#include "IrixGuest.h"\n#include <stdio.h>\n#include <stddef.h>\n#include <string.h>\n')
        w.write('typedef IrixGuestFunction (*IrixGuestImportResolver)(const char *);\n')
        w.write('typedef struct { IrixGuestAddr stub,target; const char *name; } Fwd;\nstatic const Fwd fwds[]={\n')
        for addr,target,name,owner in resolved: w.write(f'{{0x{addr:08x}u,0x{target:08x}u,"{owner}:{name}"}},\n')
        w.write('};\n')
        w.write('typedef struct { IrixGuestAddr address; const char *name; const char *owner; unsigned native; } Missing;\n')
        w.write('static Missing missing_imports[]={\n')
        for addr,target,name,owner in missing:
            w.write(f'{{0x{addr:08x}u,{json.dumps(name)},{json.dumps(owner)},0u}},\n')
        w.write('};\n')
        w.write('static void mark_native(IrixGuestAddr address){size_t i;for(i=0;i<sizeof(missing_imports)/sizeof(missing_imports[0]);++i)if(missing_imports[i].address==address){missing_imports[i].native=1u;return;}}\n')
        w.write('static void forward_import(IrixGuestRuntime *rt,IrixGuestCpu *cpu){size_t i;for(i=0;i<sizeof(fwds)/sizeof(fwds[0]);++i)if(fwds[i].stub==cpu->pc){(void)irix_guest_call(rt,cpu,fwds[i].target);return;}fprintf(stderr,"NO FORWARD 0x%08x\\n",cpu->pc);irix_guest_trap(rt,cpu,0xeeee1001u);}\n')
        w.write('static void missing_import(IrixGuestRuntime *rt,IrixGuestCpu *cpu){IrixGuestFunctionEntry*e=irix_guest_find_function(rt,cpu->pc);fprintf(stderr,"MISSING IMPORT pc=0x%08x %s\\n",cpu->pc,e&&e->name?e->name:"?");irix_guest_trap(rt,cpu,0xeeee1002u);}\n')
        w.write('void register_irix_bridge_imports(IrixGuestRuntime *rt,IrixGuestImportResolver resolver){IrixGuestFunction fn;\n')
        w.write('/* Native bridges may override both import stubs and their resolved guest\n')
        w.write('   targets. This matters for prebound GOT calls which jump directly to\n')
        w.write('   the SGI implementation and never execute an import stub. DesktopRunner\n')
        w.write('   registers these entries before translated functions, so an override wins\n')
        w.write('   only when resolver(name) actually supplies a native bridge. */\n')
        for addr,target,name,owner in all_entries:
            q=json.dumps(name)
            mark=f'mark_native(0x{addr:08x}u);' if addr in missing_index else ''
            w.write(f'fn=resolver?resolver({q}):0;if(fn){{irix_guest_register_function(rt,0x{addr:08x}u,fn,"{owner}:{name}");{mark}}}\n')
        for target,name in target_entries:
            q=json.dumps(name)
            label=json.dumps('native-target:'+name)
            w.write(f'fn=resolver?resolver({q}):0;if(fn)irix_guest_register_function(rt,0x{target:08x}u,fn,{label});\n')
        w.write('}\n')
        w.write('void register_irix_prebound_imports(IrixGuestRuntime *rt){size_t i,j;size_t native_addresses=0,unresolved_addresses=0,native_names=0,unresolved_names=0;\n')
        for got_addr,target,name,owner,old in data_patches:
            w.write(f'irix_guest_write32be(rt,0x{got_addr:08x}u,0x{target:08x}u); /* {owner}:{name}, was 0x{old:08x} */\n')
        for addr,target,name,owner in resolved: w.write(f'irix_guest_register_function(rt,0x{addr:08x}u,forward_import,"{owner}:{name}");\n')
        w.write('for(i=0;i<sizeof(missing_imports)/sizeof(missing_imports[0]);++i){\n')
        w.write('  if(missing_imports[i].native){++native_addresses;}else{++unresolved_addresses;irix_guest_register_function(rt,missing_imports[i].address,missing_import,missing_imports[i].name);}\n')
        w.write('  for(j=0;j<i;++j)if(strcmp(missing_imports[j].name,missing_imports[i].name)==0)break;\n')
        w.write('  if(j==i){if(missing_imports[i].native)++native_names;else++unresolved_names;}\n')
        w.write('}\n')
        w.write('fprintf(stderr,"IRIX import closure: guest-resolved=%zu native-resolved=%zu unresolved=%zu unique-native=%zu unique-unresolved=%zu data-patches=%zu\\n",(size_t)%du,native_addresses,unresolved_addresses,native_names,unresolved_names,(size_t)%du);\n' % (len(resolved),len(data_patches)))
        w.write('if(unresolved_names){size_t shown=0;fprintf(stderr,"IRIX unresolved imports:");for(i=0;i<sizeof(missing_imports)/sizeof(missing_imports[0])&&shown<32u;++i){if(missing_imports[i].native)continue;for(j=0;j<i;++j)if(!missing_imports[j].native&&strcmp(missing_imports[j].name,missing_imports[i].name)==0)break;if(j==i){fprintf(stderr," %s",missing_imports[i].name);++shown;}}if(unresolved_names>shown)fprintf(stderr," ...(+%zu)",unresolved_names-shown);fputc(\'\\n\',stderr);}\n')
        w.write('}\n')
    data={'resolved':[dict(address=a,target=t,name=n,owner=o) for a,t,n,o in resolved],
          'missing':[dict(address=a,name=n,owner=o) for a,t,n,o in missing],
          'missing_unique':sorted({n for a,t,n,o in missing}),
          'data_patches':[dict(got=a,target=t,name=n,owner=o,old_value=old) for a,t,n,o,old in data_patches]}
    (out/'bindings.json').write_text(json.dumps(data,indent=2)+'\n')
    print(f'addresses={len(by)} guest_resolved={len(resolved)} guest_missing={len(missing)} unique_guest_missing={len({n for a,t,n,o in missing})} native_targets={len(target_entries)} data_patches={len(data_patches)}')
if __name__=='__main__': main()
