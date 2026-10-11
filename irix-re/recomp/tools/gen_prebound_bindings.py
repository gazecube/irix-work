#!/usr/bin/env python3
"""Bind IRIX executable/DSO quickstart and prebound GOT imports by symbol name."""
import argparse, json, struct, sys
from pathlib import Path

STT_NOTYPE=0
STT_OBJECT=1
STT_FUNC=2

def dyn_symbols(path):
    """Read all dynamic symbols, including exports without global GOT slots."""
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
    for i in range(nsyms):
        off=voff(symtab+i*syment); n,v,z,info,other,sh=struct.unpack_from('>IIIBBH',p,off)
        if not n: continue
        so=voff(strtab+n); e=p.find(b'\0',so); name=p[so:e].decode('ascii','replace')
        ge=None; val=v
        if i>=gotsym:
            ge=got+(local_gotno+i-gotsym)*4
            try: val=struct.unpack_from('>I',p,voff(ge))[0]
            except KeyError: ge=None
        out[name]=dict(value=val,got=ge,type=info&15,bind=info>>4,shndx=sh,size=z,sym_value=v)
    return out

def dyn_got(path):
    return {name: row for name, row in dyn_symbols(path).items()
            if row['got'] is not None}

def main():
    ap=argparse.ArgumentParser(); ap.add_argument('stack_link_json'); ap.add_argument('bundles',nargs='+'); ap.add_argument('-o','--output',required=True)
    a=ap.parse_args(); bundles=[json.loads(Path(p).read_text()) for p in a.bundles]; link=json.loads(Path(a.stack_link_json).read_text()); defs={}
    # Later components are the more specific runtime providers.  In
    # particular, usr/lib32/rld is seeded after the normal DSO closure and
    # must be allowed to satisfy libc's private _rld_* imports even when an
    # earlier image contributed an import-stub-shaped symbol with the same
    # name.
    def_rank={}; rld_fallback={}
    for bi,b in enumerate(bundles):
        for f in b['functions']:
            name=f['name']; addr=f['address']
            if not addr:
                continue
            # rld also exports libc-shaped placeholders such as atexit and
            # _exithandle. Keep those as fallbacks, but let a real libc
            # definition win when one exists.
            if b.get('prefix') == 'irix_rld':
                rld_fallback[name]=addr
                continue
            old_rank=def_rank.get(name)
            if old_rank is None or bi>=old_rank:
                defs[name]=addr
                def_rank[name]=bi
    for name,addr in rld_fallback.items():
        defs.setdefault(name,addr)
    # Explicit runtime providers outrank auto-expanded DSOs regardless of
    # closure order.  Auto dependencies are appended after seed components,
    # so "last definition wins" alone can let a later recovered import stub
    # steal a name back from rld.
    for b in bundles:
        if b.get('prefix') == 'irix_rld':
            for f in b['functions']:
                if (f['address'] and
                        (f['name'].startswith('_rld_') or
                         f['name'].startswith('__rld_'))):
                    defs[f['name']] = f['address']
    for name,addr in link['globals'].items(): defs.setdefault(name,addr)
    defined_addrs={f['address'] for b in bundles for f in b['functions'] if f['address']}
    defined_addrs.update(addr for addr in link['globals'].values() if addr)

    # Direct-main maps and executes the original DSO memory images, so data
    # imports must be able to bind to the defining DSO object rather than the
    # synthetic static-core copy. Keep both universes available: legacy/static
    # execution still prefers link['globals'], while direct-main selects these
    # original DSO addresses.
    bundle_gots=[]
    dso_data_defs={}
    dso_data_rank={}
    for bi,b in enumerate(bundles):
        symbols=dyn_symbols(b['path'])
        got={name: row for name, row in symbols.items()
             if row['got'] is not None}
        bundle_gots.append(got)
        # DT_MIPS_GOTSYM partitions GOT users, not exported definitions.
        # libXt's objectClass is before that boundary. Missing it binds
        # libXm to the static Object copy, so _XmInitializeExtensions hooks
        # the wrong class and DSO widgets skip _SG_XtInitialize entirely.
        for name,row in symbols.items():
            if (row['shndx']==0 or row['type']!=STT_OBJECT or
                    row['bind'] not in (1, 2)):
                continue
            candidate=row['sym_value'] or row['value']
            if not candidate:
                continue
            old_rank=dso_data_rank.get(name)
            if old_rank is None or bi>=old_rank:
                dso_data_defs[name]=candidate
                dso_data_rank[name]=bi

    entries=[]; data_patches=[]; native_function_got_patches=[]
    for bi,b in enumerate(bundles):
        got=bundle_gots[bi]
        owner_functions={f['address'] for f in b['functions'] if f['address']}
        for imp in b['imports']:
            name=imp['name']; addrs=[]; row=got.get(name)
            # A prebound global GOT value can name a different import veneer
            # in another DSO. imdFtrDB's open slot, for example, points at
            # libC's _nsproc veneer. When this image has its own recovered
            # veneer and a native bridge supplies the name, bind the GOT to
            # that veneer so the call cannot be captured by the other import.
            # Keep provider bindings for names without a native bridge.
            if (row and row['got'] is not None and row['shndx']==0 and
                    row['type']==STT_FUNC and imp['address'] in owner_functions and
                    row['value'] != imp['address']):
                native_function_got_patches.append(
                    (row['got'], imp['address'], name, b['prefix'], row['value']))
            # st_value on an undefined ELF symbol is not necessarily a PLT
            # entry.  SGI prelinked N32 DSOs commonly leave the provider's
            # prebound address there.  Treating it as an owner-local stub
            # aliases unrelated imports from different images (for example
            # imdUtil:open and libC:_nsproc) and can overwrite real code.
            #
            # A nonzero GOT entry, on the other hand, is the actual address
            # used by this prebound image and is therefore the only safe
            # forwarding candidate here.
            if row and row['value']:
                addrs.append(row['value'])
            if imp['address'] and imp['address'] in defined_addrs:
                # An undefined STT_FUNC with a nonzero st_value can be a real
                # executable import veneer even when the prebound GOT already
                # contains a different provider address. Preserve both pieces
                # of evidence: the GOT target is where prebound callers may
                # jump, while st_value is the owner-local veneer that callers
                # can also reach directly. xwsh uses exactly this form for
                # strdup, malloc/calloc, getpwnam/getpwuid and XOpenDisplay.
                # defined_addrs keeps arbitrary prebound provider addresses
                # from being mistaken for local veneers.
                if imp['address'] not in addrs:
                    addrs.append(imp['address'])
            target=defs.get(name,0)
            for stub in addrs:
                # An undefined symbol may carry the address of an executable
                # import/PLT stub.  recomp_elf_bundle also recovers that stub
                # as callable code, so its address can appear in
                # defined_addrs even though it is NOT the provider of this
                # symbol.  Only suppress a candidate when it is the actual
                # selected definition.  Otherwise install the name-based
                # forwarder over the import stub.
                if target and stub == target:
                    continue
                entries.append((stub,target,name,b['prefix']))

        # Undefined data imports have two possible providers:
        #
        #   core_target - synthetic static-core global used by the historical
        #                 translated-core execution model.
        #   dso_target  - object definition in an original mapped IRIX DSO.
        #
        # AppRunner chooses at runtime. DIRECT_MAIN must use dso_target because
        # translated DSO code now runs with the owning DSO's $gp and therefore
        # must see one coherent Xt/Motif/libc data universe.
        for name,row in got.items():
            if row['shndx']!=0 or row['type']!=STT_OBJECT:
                continue
            core_target=link['globals'].get(name,0)
            dso_target=dso_data_defs.get(name,0)
            data_patches.append((row['got'],core_target,dso_target,
                                 name,b['prefix'],row['value']))

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
        got_addr,core_target,dso_target,name,owner,old=e
        prev=data_by.get(got_addr)
        if prev:
            if core_target and prev[1] and prev[1]!=core_target:
                raise SystemExit(f'core data binding conflict at 0x{got_addr:08x}: {prev} vs {e}')
            if dso_target and prev[2] and prev[2]!=dso_target:
                raise SystemExit(f'dso data binding conflict at 0x{got_addr:08x}: {prev} vs {e}')
            if ((core_target and not prev[1]) or
                (dso_target and not prev[2])):
                data_by[got_addr]=(
                    got_addr,
                    core_target or prev[1],
                    dso_target or prev[2],
                    name,owner,old)
        else:
            data_by[got_addr]=e
    data_patches=sorted(data_by.values())

    resolved=sorted(e for e in by.values() if e[1]); missing=sorted(e for e in by.values() if not e[1]); all_entries=sorted(by.values())

    # Fail the build rather than reaching runtime with a private rld import
    # that should have been satisfied by the explicitly seeded SGI rld.
    rld_defs = {
        f['name']: f['address']
        for b in bundles if b.get('prefix') == 'irix_rld'
        for f in b['functions'] if f['address']
    }
    if '_rld_new_interface' in rld_defs:
        bad = [e for e in missing if e[2] == '_rld_new_interface']
        if bad:
            raise SystemExit('_rld_new_interface remains unresolved despite explicit irix_rld provider')
    target_entries=sorted({(target,name) for addr,target,name,owner in resolved if target})
    missing_index={addr:i for i,(addr,target,name,owner) in enumerate(missing)}
    out=Path(a.output); out.mkdir(parents=True,exist_ok=True)
    with (out/'bindings.c').open('w') as w:
        w.write('#include "IrixGuest.h"\n#include <stdio.h>\n#include <stddef.h>\n#include <string.h>\n')
        w.write('typedef IrixGuestFunction (*IrixGuestImportResolver)(const char *);\n')
        w.write('typedef IrixGuestAddr (*IrixGuestDataResolver)(const char *);\n')
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
        for got_addr,veneer,name,owner,old in native_function_got_patches:
            q=json.dumps(name)
            w.write(f'fn=resolver?resolver({q}):0;if(fn&&irix_guest_ptr(rt,0x{got_addr:08x}u,4u))irix_guest_write32be(rt,0x{got_addr:08x}u,0x{veneer:08x}u); /* {owner}:{name}, was 0x{old:08x} */\n')
        w.write('}\n')
        w.write('void register_irix_prebound_imports(IrixGuestRuntime *rt,IrixGuestDataResolver data_resolver,int prefer_dso_data){size_t i,j;size_t native_addresses=0,unresolved_addresses=0,native_names=0,unresolved_names=0,dso_data_selected=0;IrixGuestAddr data_target;\n')
        for got_addr,core_target,dso_target,name,owner,old in data_patches:
            q=json.dumps(name)
            # Runtime-owned compatibility globals (errno, getopt state, etc.)
            # must point at the storage used by their native bridge.  Choosing
            # a guest libc copy first makes bridge_getopt() update one optarg
            # while translated code reads another.
            w.write(f'data_target=data_resolver?data_resolver({q}):0;')
            w.write(f'if(!data_target)data_target=prefer_dso_data?0x{dso_target:08x}u:0x{core_target:08x}u;')
            w.write(f'if(!data_target)data_target=prefer_dso_data?0x{core_target:08x}u:0x{dso_target:08x}u;')
            if dso_target:
                w.write(f'if(prefer_dso_data&&data_target==0x{dso_target:08x}u)++dso_data_selected;')
            w.write(f'if(data_target&&irix_guest_ptr(rt,0x{got_addr:08x}u,4u))irix_guest_write32be(rt,0x{got_addr:08x}u,data_target); /* {owner}:{name}, was 0x{old:08x}, core 0x{core_target:08x}, dso 0x{dso_target:08x} */\n')
        for addr,target,name,owner in resolved: w.write(f'irix_guest_register_function(rt,0x{addr:08x}u,forward_import,"{owner}:{name}");\n')
        w.write('for(i=0;i<sizeof(missing_imports)/sizeof(missing_imports[0]);++i){\n')
        w.write('  if(missing_imports[i].native){++native_addresses;}else{++unresolved_addresses;irix_guest_register_function(rt,missing_imports[i].address,missing_import,missing_imports[i].name);}\n')
        w.write('  for(j=0;j<i;++j)if(strcmp(missing_imports[j].name,missing_imports[i].name)==0)break;\n')
        w.write('  if(j==i){if(missing_imports[i].native)++native_names;else++unresolved_names;}\n')
        w.write('}\n')
        w.write('}\n')
    data={'resolved':[dict(address=a,target=t,name=n,owner=o) for a,t,n,o in resolved],
          'missing':[dict(address=a,name=n,owner=o) for a,t,n,o in missing],
          'missing_unique':sorted({n for a,t,n,o in missing}),
          'data_patches':[dict(got=a,core_target=ct,dso_target=dt,name=n,owner=o,old_value=old) for a,ct,dt,n,o,old in data_patches],
          'native_function_got_patches':[dict(got=a,veneer=v,name=n,owner=o,old_value=old) for a,v,n,o,old in native_function_got_patches]}
    (out/'bindings.json').write_text(json.dumps(data,indent=2)+'\n')
    print(f'addresses={len(by)} guest_resolved={len(resolved)} guest_missing={len(missing)} unique_guest_missing={len({n for a,t,n,o in missing})} native_targets={len(target_entries)} data_patches={len(data_patches)}')
if __name__=='__main__': main()
