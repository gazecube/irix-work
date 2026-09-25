#!/usr/bin/env python3
import argparse,io,json,re,struct,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).parent))
from link_stack_n32 import StackLink
from translate_n32_core import decode

STT_FUNC=2
DEFAULT_MAX_FILE_BYTES=2*1024*1024
PARTITION_VERSION=1
BATCH_PREAMBLE='#include "IrixGuest.h"\n#include <math.h>\n\nenum { IRIX_PENDING_NONE, IRIX_PENDING_JUMP, IRIX_PENDING_CALL, IRIX_PENDING_RETURN };\n\n'

def ident(s): return re.sub(r'[^A-Za-z0-9_]','_',s)

def write_if_changed(path, text):
 path=Path(path)
 if path.is_file():
  try:
   if path.read_text()==text: return False
  except UnicodeDecodeError:
   pass
 path.parent.mkdir(parents=True,exist_ok=True)
 path.write_text(text)
 return True

def load_partition(path,max_file_bytes):
 path=Path(path)
 if not path.is_file():
  return {'version':PARTITION_VERSION,'max_file_bytes':max_file_bytes,'file_count':0,'assignments':{}}
 try:
  doc=json.loads(path.read_text())
 except Exception:
  return {'version':PARTITION_VERSION,'max_file_bytes':max_file_bytes,'file_count':0,'assignments':{}}
 if doc.get('version')!=PARTITION_VERSION or doc.get('max_file_bytes')!=max_file_bytes:
  return {'version':PARTITION_VERSION,'max_file_bytes':max_file_bytes,'file_count':0,'assignments':{}}
 if not isinstance(doc.get('assignments'),dict): doc['assignments']={}
 return doc

def assign_partitions(entries,path,max_file_bytes):
 part=load_partition(path,max_file_bytes)
 total=sum(e['bytes'] for e in entries)
 needed=max(1,(total+max_file_bytes-1)//max_file_bytes) if entries else 0
 old_count=int(part.get('file_count') or 0)
 file_count=max(old_count,needed) if old_count else needed
 buckets=[[] for _ in range(file_count)]
 bucket_bytes=[0 for _ in range(file_count)]
 placed=set()
 old=part.get('assignments',{})
 for i,e in enumerate(entries):
  b=old.get(f'{e["address"]:08X}')
  if isinstance(b,int) and 0<=b<file_count:
   buckets[b].append(i); bucket_bytes[b]+=e['bytes']; placed.add(i)
 for i,e in enumerate(entries):
  if i in placed: continue
  b=min(range(file_count),key=lambda n:bucket_bytes[n])
  buckets[b].append(i); bucket_bytes[b]+=e['bytes']
 for bucket in buckets:
  bucket.sort(key=lambda i:entries[i]['address'])
 while buckets and not buckets[-1]:
  buckets.pop(); bucket_bytes.pop()
 assignments={}
 for b,items in enumerate(buckets):
  for i in items: assignments[f'{entries[i]["address"]:08X}']=b
 write_if_changed(path,json.dumps({
  'version':PARTITION_VERSION,
  'max_file_bytes':max_file_bytes,
  'file_count':len(buckets),
  'assignments':dict(sorted(assignments.items())),
 },indent=2)+'\n')
 return buckets

def render_function(cname, source, name, start, words):
 w=io.StringIO(); p=lambda x='': print(x,file=w)
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
 p('        cpu->pc = pc;'); p('        rt->current_pc = pc;'); p()
 p('        switch (pc) {')
 for i,word in enumerate(words):
  addr=start+i*4; lines,_=decode(addr,word)
  p(f'        case 0x{addr:08x}u: /* 0x{word:08x} */')
  for line in lines:p('            '+line)
  p('            break;')
 p('        default:'); p('            return;'); p('        }'); p()
 p('        cpu->gpr[0] = 0;'); p('        if (rt->trapped) return;')
 p('        if (old_pending == IRIX_PENDING_RETURN) return;')
 p('        if (old_pending == IRIX_PENDING_CALL) {')
 p('            if (irix_guest_call(rt, cpu, old_target) != 0) return;')
 p('            pc = old_return; continue;')
 p('        }')
 p('        if (old_pending == IRIX_PENDING_JUMP) {')
 p(f'            if (old_target < 0x{start:08x}u || old_target >= 0x{start+len(words)*4:08x}u) {{')
 p('                if (irix_guest_call(rt, cpu, old_target) != 0) return;')
 p('                return;')
 p('            }')
 p('            pc = old_target; continue;')
 p('        }')
 p('        pc += 4;')
 p('    }')
 p('}')
 return w.getvalue()

def main():
 ap=argparse.ArgumentParser(); ap.add_argument('archives',nargs='+'); ap.add_argument('--support-archive',action='append',default=[]); ap.add_argument('-o','--output',required=True); a=ap.parse_args()
 out=Path(a.output); out.mkdir(parents=True,exist_ok=True)
 L=StackLink()
 for p in a.archives:L.add_archive(p)
 for p in a.support_archive:L.add_support_archive(p)
 support_added=L.resolve_support_archives()
 L.layout(); L.relocate(); L.write(out/'image')
 funcs=[]; failed=[]; changed=0; unchanged=0; batch_count=0
 for oi,o in enumerate(L.objs):
  arc=ident(Path(o.archive).stem); obj=ident(Path(o.name).stem); od=out/'c'/f'{oi:04d}_{arc}_{obj}'; od.mkdir(parents=True,exist_ok=True)
  emitted=[]
  for si,s in enumerate(o.syms):
   if s['type']!=STT_FUNC or not s['size'] or s['shndx'] not in o.image: continue
   if s['value']+s['size']>len(o.image[s['shndx']]) or s['size']%4: failed.append({'object':o.name,'name':s['name'],'error':'invalid function extent'}); continue
   start=o.base[s['shndx']]+s['value']; b=o.image[s['shndx']][s['value']:s['value']+s['size']]; words=struct.unpack('>%dI'%(len(b)//4),b)
   cname=f'irix_recomp_o{oi:04d}_s{si:04d}_{ident(s["name"])}'
   try:
    body=render_function(cname,f'{Path(o.archive).name}:{o.name}',s['name'],start,words)
   except Exception as exc: failed.append({'object':o.name,'name':s['name'],'address':start,'error':str(exc)}); continue
   marker=body.find('void '+cname)
   if marker<0: failed.append({'object':o.name,'name':s['name'],'address':start,'error':'generated body marker missing'}); continue
   row={'address':start,'size':s['size'],'name':s['name'],'c_name':cname,'archive':o.archive,'member':o.name}
   fbody=body[marker:].rstrip()+'\n\n'
   funcs.append(row)
   emitted.append({'address':start,'bytes':len(fbody.encode('utf-8')),'row':row,'body':fbody})
  buckets=assign_partitions(emitted,od/'partition.json',DEFAULT_MAX_FILE_BYTES)
  keep=set()
  for object_batch,items in enumerate(buckets):
   dst=od/f'recomp_{object_batch:04d}.c'
   text=BATCH_PREAMBLE+''.join(emitted[i]['body'] for i in items)
   if write_if_changed(dst,text): changed+=1
   else: unchanged+=1
   for i in items: emitted[i]['row']['source']=str(dst)
   keep.add(dst.name); batch_count+=1
  for stale in list(od.glob('batch_*.c'))+list(od.glob('recomp_*.c')):
   if stale.name not in keep:
    try: stale.unlink()
    except OSError: pass
 funcs.sort(key=lambda x:x['address'])
 w=io.StringIO(); print('#include "IrixGuest.h"',file=w); print('#include <stdio.h>',file=w); print(file=w)
 print('typedef IrixGuestFunction (*IrixGuestImportResolver)(const char *);',file=w); print(file=w)
 print('static void irix_missing_core_import(IrixGuestRuntime *rt, IrixGuestCpu *cpu)',file=w)
 print('{',file=w)
 print('    IrixGuestFunctionEntry *e = irix_guest_find_function(rt, cpu->pc);',file=w)
 print('    fprintf(stderr, "MISSING CORE IMPORT pc=0x%08x %s\\n", cpu->pc,',file=w)
 print('            e && e->name ? e->name : "?");',file=w)
 print('    irix_guest_trap(rt, cpu, 0xeeee1003u);',file=w)
 print('}',file=w); print(file=w)
 for f in funcs: print(f'extern void {f["c_name"]}(IrixGuestRuntime *, IrixGuestCpu *);',file=w)
 print('\nvoid irix_register_recompiled_stack(IrixGuestRuntime *rt)\n{',file=w)
 for f in funcs: print(f'    irix_guest_register_function(rt, 0x{f["address"]:08x}u, {f["c_name"]}, "{f["name"]}");',file=w)
 print('}',file=w)
 print('\nvoid irix_register_recompiled_stack_imports(IrixGuestRuntime *rt, IrixGuestImportResolver resolver)\n{',file=w)
 print('    IrixGuestFunction fn;',file=w)
 for name,addr in sorted(L.imports.items(), key=lambda kv: kv[1]):
  q=json.dumps(name)
  print(f'    fn = resolver ? resolver({q}) : 0;',file=w)
  print(f'    irix_guest_register_function(rt, 0x{addr:08x}u, fn ? fn : irix_missing_core_import, {q});',file=w)
 print('}',file=w)
 if write_if_changed(out/'registry.c',w.getvalue()): changed+=1
 else: unchanged+=1
 write_if_changed(out/'functions.json',json.dumps({'functions':funcs,'failed':failed,'imports':L.imports,'gp':L.gp},indent=2)+'\n')
 print(f'functions={len(funcs)} batches={batch_count} failed={len(failed)} imports={len(L.imports)} relocations={L.relocations} support_members={support_added} changed={changed} unchanged={unchanged}')
 if failed:
  for x in failed[:30]: print('FAIL',x)
  raise SystemExit(1)
if __name__=='__main__': main()
