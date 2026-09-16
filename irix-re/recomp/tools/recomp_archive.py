#!/usr/bin/env python3
"""Drive static recompilation across every ELF object in an IRIX archive.

The driver extracts archive members, discovers sized STT_FUNC symbols, invokes
translate_n32.py for every function, and emits a machine-readable failure
manifest. It is intentionally breadth-first: one unsupported function does not
hide the rest of the archive.
"""
import argparse, json, struct, subprocess, sys, tempfile
from pathlib import Path
sys.path.insert(0,str(Path(__file__).parent))
from extract_ar import members

SHT_SYMTAB=2; STT_FUNC=2

def funcs(data):
 h=struct.unpack_from('>16sHHIIIIIHHHHHH',data,0); shoff,esz,num=h[6],h[11],h[12]
 sec=[]
 for i in range(num):sec.append(struct.unpack_from('>IIIIIIIIII',data,shoff+i*esz))
 out=[]
 for s in sec:
  if s[1]!=SHT_SYMTAB or not s[9]:continue
  ss=sec[s[6]]; strings=data[ss[4]:ss[4]+ss[5]]
  for i in range(s[5]//s[9]):
   n,v,z,info,other,sh=struct.unpack_from('>IIIBBH',data,s[4]+i*s[9])
   if info&15!=STT_FUNC or not z or sh==0:continue
   e=strings.find(b'\0',n); name=strings[n:e].decode('ascii','replace') if n else ''
   if name:out.append((name,v,z))
 return out

def main():
 ap=argparse.ArgumentParser(); ap.add_argument('archive'); ap.add_argument('-o','--output',required=True); ap.add_argument('--translator',default=str(Path(__file__).with_name('translate_n32.py'))); a=ap.parse_args()
 out=Path(a.output); out.mkdir(parents=True,exist_ok=True); report={'archive':a.archive,'objects':[],'translated':0,'failed':0}
 with tempfile.TemporaryDirectory() as td:
  for name,body in members(Path(a.archive).read_bytes()):
   if not body.startswith(b'\x7fELF\x01\x02'):continue
   obj=Path(td)/Path(name).name; obj.write_bytes(body); fs=funcs(body); ore={'member':name,'functions':[]}
   od=out/Path(name).stem; od.mkdir(parents=True,exist_ok=True)
   for fn,addr,size in fs:
    dst=od/(fn.replace('/','_')+'.c')
    p=subprocess.run([sys.executable,a.translator,str(obj),fn,'-o',str(dst)],text=True,capture_output=True)
    r={'name':fn,'address':addr,'size':size,'ok':p.returncode==0}
    if p.returncode: r['error']=(p.stderr or p.stdout).strip(); report['failed']+=1
    else: report['translated']+=1
    ore['functions'].append(r)
   report['objects'].append(ore)
 (out/'manifest.json').write_text(json.dumps(report,indent=2)+'\n')
 print(f"objects={len(report['objects'])} translated={report['translated']} failed={report['failed']}")
 if report['failed']:print('unsupported functions are recorded in manifest.json')
if __name__=='__main__':main()
