#!/usr/bin/env python3
import argparse,io,json,re,struct,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).parent))
from link_stack_n32 import StackLink
from translate_n32_core import decode, static_control_info

STT_FUNC=2
DEFAULT_MAX_FILE_BYTES=2*1024*1024
PARTITION_VERSION=1
BATCH_PREAMBLE='#include "IrixGuest.h"\n#include "IrixMipsInterp.h"\n#include <math.h>\n\nenum { IRIX_PENDING_NONE, IRIX_PENDING_JUMP, IRIX_PENDING_CALL, IRIX_PENDING_RETURN, IRIX_PENDING_LINK };\n\n'

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

def static_branch_condition(addr,insn):
 op=insn>>26; rs=(insn>>21)&31; rt=(insn>>16)&31
 r=lambda n: 'cpu->gpr[%d]'%n
 if op in (0x04,0x14): return '%s == %s'%(r(rs),r(rt))
 if op in (0x05,0x15): return '%s != %s'%(r(rs),r(rt))
 if op in (0x06,0x16): return '(int64_t)%s <= 0'%r(rs)
 if op in (0x07,0x17): return '(int64_t)%s > 0'%r(rs)
 if op==0x01:
  if rt in (0x00,0x02): return '(int64_t)%s < 0'%r(rs)
  if rt in (0x01,0x03,0x11,0x13): return '(int64_t)%s >= 0'%r(rs)
 if op==0x11 and rs==0x08:
  cond='(cpu->fcr31 & (1u << 23)) != 0'
  if not (rt&1): cond='!(%s)'%cond
  return cond
 return None

def static_cfg_entries(decoded, start, end_addr):
 entries={start}
 for addr,word,lines,control in decoded:
  if not control: continue
  ctl=static_control_info(addr,word)
  if ctl and 'target' in ctl and start<=ctl['target']<end_addr:
   entries.add(ctl['target'])
  if ctl and ctl.get('kind') in ('call','call_indirect','link'):
   ret=ctl.get('ret',(addr+8)&0xffffffff)
   if start<=ret<end_addr: entries.add(ret)
  elif ctl is None:
   ret=(addr+8)&0xffffffff
   if start<=ret<end_addr: entries.add(ret)
 return entries

def static_block_labels(decoded,start,end_addr):
 labels=set(static_cfg_entries(decoded,start,end_addr))
 for idx,(addr,word,lines,control) in enumerate(decoded):
  if not control: continue
  if idx+1<len(decoded): labels.add(decoded[idx+1][0])
  ctl=static_control_info(addr,word)
  # Native conditional-branch lowering emits an explicit goto to the
  # post-delay fallthrough block for both normal and likely branches.
  if ctl and static_branch_condition(addr,word) is not None and idx+2<len(decoded):
   labels.add(decoded[idx+2][0])
 return labels

def render_function(cname, source, name, start, words):
 w=io.StringIO(); p=lambda x='': print(x,file=w)
 decoded=[]
 for i,word in enumerate(words):
  addr=start+i*4
  lines,control=decode(addr,word)
  decoded.append((addr,word,lines,control))

 p(f'/* Relocated IRIX N32: {source}: {name} @ 0x{start:08x}, 0x{len(words)*4:x} bytes. */')
 p('#include "IrixGuest.h"'); p('#include <math.h>'); p()
 p('enum { IRIX_PENDING_NONE, IRIX_PENDING_JUMP, IRIX_PENDING_CALL, IRIX_PENDING_RETURN, IRIX_PENDING_LINK };'); p()
 p(f'void {cname}(IrixGuestRuntime *rt, IrixGuestCpu *cpu)'); p('{')
 has_delay_slot=any(
  idx and decoded[idx-1][0]+4==addr and decoded[idx-1][3]=='control'
  for idx,(addr,_word,_lines,_control) in enumerate(decoded)
 )
 emitted_lines='\n'.join(line for _addr,_word,lines,_control in decoded for line in lines)
 end_addr=start+len(words)*4
 control_infos=[static_control_info(addr,word)
                for addr,word,_lines,control in decoded if control]
 needs_local_dispatch=any(
  ctl is None or ctl.get('kind')=='call_indirect' or
  (ctl.get('kind')=='link' and start<=ctl.get('target',0)<end_addr)
  for ctl in control_infos)
 p('    IrixGuestAddr pc = cpu->pc;')
 if has_delay_slot or 'pending_target' in emitted_lines: p('    IrixGuestAddr pending_target = 0;')
 if has_delay_slot or 'pending_return' in emitted_lines: p('    IrixGuestAddr pending_return = 0;')
 if has_delay_slot or 'pending_kind' in emitted_lines: p('    int pending_kind = IRIX_PENDING_NONE;')
 if has_delay_slot:
  p('    int old_pending;')
  p('    IrixGuestAddr old_target;')
  p('    IrixGuestAddr old_return;')
  p('    IrixGuestAddr local_link_returns[64];')
  p('    unsigned local_link_depth = 0;')
 p()
 cfg_entries=static_cfg_entries(decoded,start,end_addr)
 block_labels=static_block_labels(decoded,start,end_addr)
 p(f'    goto L_{start:08x};')
 if has_delay_slot and needs_local_dispatch:
  p('dispatch_local:')
  p('    switch (pc) {')
  for addr in sorted(cfg_entries):
   p(f'    case 0x{addr:08x}u: goto L_{addr:08x};')
  p('    default:')
  p('        (void)irix_mips_interp_call(rt, cpu, pc);')
  p('        return;')
  p('    }')
  p()

 for idx,(addr,word,lines,control) in enumerate(decoded):
  is_delay_slot=bool(idx and decoded[idx-1][0]+4==addr and decoded[idx-1][3]=='control')
  if addr in block_labels: p(f'L_{addr:08x}:')
  p(f'    /* 0x{addr:08x}: 0x{word:08x} */')
  if is_delay_slot:
   p('    old_pending = pending_kind;')
   p('    old_target = pending_target;')
   p('    old_return = pending_return;')
   p('    pending_kind = IRIX_PENDING_NONE;')
  p(f'    pc = 0x{addr:08x}u;')
  p('    cpu->pc = pc;')
  p('    rt->current_pc = pc;')
  ctl_here=static_control_info(addr,word) if control else None
  branch_cond=static_branch_condition(addr,word) if control else None
  if branch_cond is not None and ctl_here and ctl_here.get('kind')=='jump':
   p(f'    int branch_taken_{addr:08x} = ({branch_cond});')
   for line in lines:
    if ('pending_kind' not in line and 'pending_target' not in line and
        'pc += 4' not in line and
        line.strip() not in (f'if ({branch_cond}) {{','} else {','}')):
     p('    '+line)
  else:
   for line in lines: p('    '+line)
  p('    if (rt->trapped) return;')
  p('    cpu->gpr[0] = 0;')

  prev_ctl=None
  if is_delay_slot:
   prev_ctl=static_control_info(decoded[idx-1][0],decoded[idx-1][1])
  # Pending transfers are meaningful only in an architectural delay slot.
  # Keep ordinary straight-line instructions free of the generic resolver.
  if is_delay_slot:
   if prev_ctl and 'target' in prev_ctl:
    target=prev_ctl['target']; kind=prev_ctl['kind']
    if kind=='jump':
     owner_addr=decoded[idx-1][0]
     branch_cond=static_branch_condition(owner_addr,decoded[idx-1][1])
     if branch_cond is not None:
      after=addr+4
      if start<=target<end_addr:
       p(f'    if (branch_taken_{owner_addr:08x}) goto L_{target:08x};')
      else:
       p(f'    if (branch_taken_{owner_addr:08x}) {{')
       p(f'        if (irix_guest_call(rt, cpu, 0x{target:08x}u) != 0) return;')
       p('        return;')
       p('    }')
      if start<=after<end_addr: p(f'    goto L_{after:08x};')
      else: p('    return;')
     elif start<=target<end_addr:
      p(f'    if (old_pending == IRIX_PENDING_JUMP && old_target == 0x{target:08x}u) goto L_{target:08x};')
     else:
      p(f'    if (old_pending == IRIX_PENDING_JUMP && old_target == 0x{target:08x}u) {{')
      p('        if (irix_guest_call(rt, cpu, old_target) != 0) return;')
      p('        return;')
      p('    }')
    elif kind=='call':
     ret=prev_ctl['ret']
     p(f'    if (old_pending == IRIX_PENDING_CALL && old_target == 0x{target:08x}u) {{')
     p(f'        if (irix_guest_call(rt, cpu, 0x{target:08x}u) != 0) return;')
     if start<=ret<end_addr: p(f'        goto L_{ret:08x};')
     else: p('        return;')
     p('    }')
    elif kind=='link':
     ret=prev_ctl['ret']
     p(f'    if (old_pending == IRIX_PENDING_LINK && old_target == 0x{target:08x}u) {{')
     if start<=target<end_addr:
      p('        if (local_link_depth >= sizeof(local_link_returns) / sizeof(local_link_returns[0])) {')
      p('            irix_guest_trap(rt, cpu, 0xeeee0003u); return;')
      p('        }')
      p(f'        local_link_returns[local_link_depth++] = 0x{ret:08x}u;')
      p(f'        goto L_{target:08x};')
     else:
      p(f'        {{ static IrixGuestFunctionEntry *direct_entry_{addr:08x};')
      p(f'          IrixGuestFunctionEntry *direct_entry = direct_entry_{addr:08x};')
      p('          if (!direct_entry) {')
      p(f'              direct_entry = irix_guest_find_function(rt, 0x{target:08x}u);')
      p(f'              if (direct_entry && direct_entry->address == 0x{target:08x}u)')
      p(f'                  direct_entry_{addr:08x} = direct_entry;')
      p('          }')
      p(f'          if (direct_entry && direct_entry->address == 0x{target:08x}u) {{')
      p(f'              if (irix_guest_call_entry(rt, cpu, 0x{target:08x}u, direct_entry) != 0) return;')
      p(f'          }} else if (irix_guest_call(rt, cpu, 0x{target:08x}u) != 0) return;')
      p('        }')
      if start<=ret<end_addr: p(f'        goto L_{ret:08x};')
      else: p('        return;')
     p('    }')

   p('    if (old_pending == IRIX_PENDING_RETURN) {')
   if needs_local_dispatch:
    p('        if (local_link_depth && old_target == local_link_returns[local_link_depth - 1]) {')
    p('            local_link_depth--; pc = old_target; goto dispatch_local;')
    p('        }')
   p('        return;')
   p('    }')
   p('    if (old_pending == IRIX_PENDING_CALL) {')
   p('        if (irix_guest_call(rt, cpu, old_target) != 0) return;')
   p('        pc = old_return; goto dispatch_local;')
   p('    }')
   p('    if (old_pending == IRIX_PENDING_LINK) {')
   p(f'        if (old_target >= 0x{start:08x}u && old_target < 0x{end_addr:08x}u) {{')
   p('            if (local_link_depth >= sizeof(local_link_returns) / sizeof(local_link_returns[0])) {')
   p('                irix_guest_trap(rt, cpu, 0xeeee0003u); return;')
   p('            }')
   p('            local_link_returns[local_link_depth++] = old_return;')
   p('            pc = old_target; goto dispatch_local;')
   p('        }')
   p('        if (irix_guest_call(rt, cpu, old_target) != 0) return;')
   p('        pc = old_return; goto dispatch_local;')
   p('    }')
   p('    if (old_pending == IRIX_PENDING_JUMP) {')
   p(f'        if (old_target < 0x{start:08x}u || old_target >= 0x{end_addr:08x}u) {{')
   p('            if (irix_guest_call(rt, cpu, old_target) != 0) return;')
   p('            return;')
   p('        }')
   p('        pc = old_target; goto dispatch_local;')
   p('    }')

  if control:
   ctl=static_control_info(addr,word)
   if idx+1<len(decoded):
    delay=decoded[idx+1][0]
    after=decoded[idx+2][0] if idx+2<len(decoded) else None
    if ctl and ctl.get('likely') and after is not None:
     p(f'    if (pc == 0x{addr:08x}u) goto L_{delay:08x};')
     p(f'    goto L_{after:08x};')
    else:
     p(f'    goto L_{delay:08x};')
   else:
    p('    return;')
  elif idx+1<len(decoded):
   pass
  else:
   p('    return;')
  p()
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
