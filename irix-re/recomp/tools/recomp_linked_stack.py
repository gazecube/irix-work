#!/usr/bin/env python3
import argparse,json,re,struct,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).parent))
from link_stack_n32 import StackLink
from translate_n32 import decode

STT_FUNC=2

def ident(s): return re.sub(r'[^A-Za-z0-9_]','_',s)

def emit_function(path, cname, source, name, start, words):
 with open(path,'w') as w:
  p=lambda x='': print(x,file=w)
  p(f'/* Relocated IRIX N32: {source}: {name} @ 0x{start:08x}, 0x{len(words)*4:x} bytes. */')
  p('#include "IrixGuest.h"'); p('#include <math.h>'); p()
  p('enum { IRIX_PENDING_NONE, IRIX_PENDING_JUMP, IRIX_PENDING_CALL, IRIX_PENDING_RETURN };'); p()
  p(f'void {cname}(IrixGuestRuntime *rt, IrixGuestCpu *cpu)'); p('{')
  p(f'    IrixGuestAddr pc = 0x{start:08x}u;')
  p('    IrixGuestAddr pending_target = 0;')
  p('    IrixGuestAddr pending_return = 0;')
  p('    int pending_kind = IRIX_PENDING_NONE;')
  p('    int old_pending;')
  p('    IrixGuestAddr old_target;')
  p('    IrixGuestAddr old_return;'); p()
  p('    for (;;) {')
  p('        old_pending = pending_kind;')
  p('        old_target = pending_target;')
  p('        old_return = pending_return;')
  p('        pending_kind = IRIX_PENDING_NONE;')
  p('        cpu->pc = pc;'); p()
  p('        switch (pc) {')
  for i,word in enumerate(words):
   addr=start+i*4; lines,_=decode(addr,word)
   p(f'        case 0x{addr:08x}u: /* 0x{word:08x} */')
   for line in lines:p('            '+line)
   p('            break;')
  p('        default:'); p('            return;'); p('        }'); p()
  p('        cpu->gpr[0] = 0;')
  p('        if (old_pending == IRIX_PENDING_RETURN) return;')
  p('        if (old_pending == IRIX_PENDING_CALL) {')
  p('            if (irix_guest_call(rt, cpu, old_target) != 0) return;')
  p('            pc = old_return; continue;')
  p('        }')
  p('        if (old_pending == IRIX_PENDING_JUMP) { pc = old_target; continue; }')
  p('        pc += 4;')
  p('    }')
  p('}')

def main():
 ap=argparse.ArgumentParser(); ap.add_argument('archives',nargs='+'); ap.add_argument('-o','--output',required=True); a=ap.parse_args()
 out=Path(a.output); out.mkdir(parents=True,exist_ok=True)
 L=StackLink()
 for p in a.archives:L.add_archive(p)
 L.layout(); L.relocate(); L.write(out/'image')
 funcs=[]; failed=[]
 for oi,o in enumerate(L.objs):
  arc=ident(Path(o.archive).stem); obj=ident(Path(o.name).stem); od=out/'c'/f'{oi:04d}_{arc}_{obj}'; od.mkdir(parents=True,exist_ok=True)
  for si,s in enumerate(o.syms):
   if s['type']!=STT_FUNC or not s['size'] or s['shndx'] not in o.image: continue
   if s['value']+s['size']>len(o.image[s['shndx']]) or s['size']%4: failed.append({'object':o.name,'name':s['name'],'error':'invalid function extent'}); continue
   start=o.base[s['shndx']]+s['value']; b=o.image[s['shndx']][s['value']:s['value']+s['size']]; words=struct.unpack('>%dI'%(len(b)//4),b)
   cname=f'irix_recomp_o{oi:04d}_s{si:04d}_{ident(s["name"])}'; dst=od/f'{si:04d}_{ident(s["name"])}.c'
   try: emit_function(dst,cname,f'{Path(o.archive).name}:{o.name}',s['name'],start,words)
   except Exception as exc: failed.append({'object':o.name,'name':s['name'],'address':start,'error':str(exc)}); continue
   funcs.append({'address':start,'size':s['size'],'name':s['name'],'c_name':cname,'archive':o.archive,'member':o.name,'source':str(dst)})
 funcs.sort(key=lambda x:x['address'])
 with open(out/'registry.c','w') as w:
  print('#include "IrixGuest.h"',file=w); print(file=w)
  for f in funcs: print(f'extern void {f["c_name"]}(IrixGuestRuntime *, IrixGuestCpu *);',file=w)
  print('\nvoid irix_register_recompiled_stack(IrixGuestRuntime *rt)\n{',file=w)
  for f in funcs: print(f'    irix_guest_register_function(rt, 0x{f["address"]:08x}u, {f["c_name"]}, "{f["name"]}");',file=w)
  print('}',file=w)
 (out/'functions.json').write_text(json.dumps({'functions':funcs,'failed':failed,'imports':L.imports,'gp':L.gp},indent=2)+'\n')
 print(f'functions={len(funcs)} failed={len(failed)} imports={len(L.imports)} relocations={L.relocations}')
 if failed:
  for x in failed[:30]: print('FAIL',x)
  raise SystemExit(1)
if __name__=='__main__': main()
