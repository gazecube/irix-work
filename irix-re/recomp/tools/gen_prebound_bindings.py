#!/usr/bin/env python3
"""Bind IRIX executable/DSO quickstart and prebound GOT imports by symbol name."""
import argparse, json, struct
from pathlib import Path

def dyn_prebound(path):
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
        if val: out[name]=val
    return out

def main():
    ap=argparse.ArgumentParser(); ap.add_argument('stack_link_json'); ap.add_argument('bundles',nargs='+'); ap.add_argument('-o','--output',required=True)
    a=ap.parse_args(); bundles=[json.loads(Path(p).read_text()) for p in a.bundles]; link=json.loads(Path(a.stack_link_json).read_text()); defs={}
    for b in bundles:
        for f in b['functions']: defs.setdefault(f['name'],f['address'])
    for name,addr in link['globals'].items(): defs.setdefault(name,addr)
    entries=[]; trace=[]
    for b in bundles:
        pre=dyn_prebound(b['path'])
        for imp in b['imports']:
            name=imp['name']; addrs=[]
            if imp['address']: addrs.append(imp['address'])
            if pre.get(name): addrs.append(pre[name])
            if name == 'XResourceManagerString':
                trace.append((b['prefix'], imp['address'], pre.get(name,0), defs.get(name,0)))
            for stub in addrs: entries.append((stub,defs.get(name,0),name,b['prefix']))
    if trace:
        print('TRACE XResourceManagerString defs=0x%08x' % defs.get('XResourceManagerString',0))
        for owner,imp_addr,pre_addr,target in trace:
            print('TRACE XResourceManagerString owner=%s import=0x%08x prebound=0x%08x target=0x%08x' %
                  (owner,imp_addr or 0,pre_addr or 0,target or 0))
    by={}; conflicts=[]
    for e in entries:
        addr,target,name,owner=e; old=by.get(addr)
        if old is None or (target and not old[1]): by[addr]=e
        elif target and old[1] and target!=old[1]: conflicts.append((addr,old,e))
    if conflicts:
        for x in conflicts[:20]: print('CONFLICT',x)
        raise SystemExit(f'{len(conflicts)} binding conflicts')
    resolved=sorted(e for e in by.values() if e[1]); missing=sorted(e for e in by.values() if not e[1]); all_entries=sorted(by.values())
    target_entries=sorted({(target,name) for addr,target,name,owner in resolved if target})
    out=Path(a.output); out.mkdir(parents=True,exist_ok=True)
    with (out/'bindings.c').open('w') as w:
        w.write('#include "IrixGuest.h"\n#include <stdio.h>\n#include <stddef.h>\n')
        w.write('typedef IrixGuestFunction (*IrixGuestImportResolver)(const char *);\n')
        w.write('typedef struct { IrixGuestAddr stub,target; const char *name; } Fwd;\nstatic const Fwd fwds[]={\n')
        for addr,target,name,owner in resolved: w.write(f'{{0x{addr:08x}u,0x{target:08x}u,"{owner}:{name}"}},\n')
        w.write('};\n')
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
            w.write(f'fn=resolver?resolver({q}):0;if(fn)irix_guest_register_function(rt,0x{addr:08x}u,fn,"{owner}:{name}");\n')
        for target,name in target_entries:
            q=json.dumps(name)
            label=json.dumps('native-target:'+name)
            w.write(f'fn=resolver?resolver({q}):0;if(fn)irix_guest_register_function(rt,0x{target:08x}u,fn,{label});\n')
        w.write('}\n')
        w.write('void register_irix_prebound_imports(IrixGuestRuntime *rt){size_t i;IrixGuestFunctionEntry *chosen;\n')
        for addr,target,name,owner in resolved: w.write(f'irix_guest_register_function(rt,0x{addr:08x}u,forward_import,"{owner}:{name}");\n')
        for addr,target,name,owner in missing: w.write(f'irix_guest_register_function(rt,0x{addr:08x}u,missing_import,"{owner}:{name}");\n')
        w.write('fprintf(stderr,"XRMSTR TABLE BEGIN\\n");\n')
        w.write('for(i=0;i<rt->function_count;++i)if(rt->functions[i].address==0x0f708050u)fprintf(stderr,"XRMSTR entry[%zu] name=%s fn=%p\\n",i,rt->functions[i].name?rt->functions[i].name:"?",(void*)rt->functions[i].function);\n')
        w.write('chosen=irix_guest_find_function(rt,0x0f708050u);fprintf(stderr,"XRMSTR chosen name=%s fn=%p\\n",chosen&&chosen->name?chosen->name:"?",chosen?(void*)chosen->function:0);\n')
        w.write('fprintf(stderr,"XRMSTR TABLE END\\n");\n')
        w.write('}\n')
    data={'resolved':[dict(address=a,target=t,name=n,owner=o) for a,t,n,o in resolved],'missing':[dict(address=a,name=n,owner=o) for a,t,n,o in missing]}
    (out/'bindings.json').write_text(json.dumps(data,indent=2)+'\n')
    print(f'addresses={len(by)} resolved={len(resolved)} missing={len(missing)} native_targets={len(target_entries)}')
if __name__=='__main__': main()
