#!/usr/bin/env python3
import argparse,contextlib,io,json,re,struct,sys,tempfile
from pathlib import Path
sys.path.insert(0,str(Path(__file__).parent))
from extract_ar import members
from translate_n32 import emit
SHT_SYMTAB=2; SHF_ALLOC=2; STT_FUNC=2

def align(v,a): return (v+a-1)&~(a-1) if a>1 else v

def parse(data):
 h=struct.unpack_from('>16sHHIIIIIHHHHHH',data,0); shoff,esz,num=h[6],h[11],h[12]
 sec=[]
 for i in range(num):
  x=struct.unpack_from('>IIIIIIIIII',data,shoff+i*esz)
  sec.append({'no':i,'type':x[1],'flags':x[2],'off':x[4],'size':x[5],'link':x[6],'align':x[8] or 1,'entsz':x[9]})
 fs=[]
 for s in sec:
  if s['type']!=SHT_SYMTAB or not s['entsz']: continue
  ss=sec[s['link']]; strings=data[ss['off']:ss['off']+ss['size']]
  for i in range(s['size']//s['entsz']):
   n,v,z,info,other,sh=struct.unpack_from('>IIIBBH',data,s['off']+i*s['entsz'])
   if info&15!=STT_FUNC or not z or sh==0: continue
   e=strings.find(b'\0',n); name=strings[n:e].decode('ascii','replace') if n else ''
   if name: fs.append({'name':name,'value':v,'size':z,'shndx':sh})
 return sec,fs

def ident(s): return re.sub(r'[^A-Za-z0-9_]','_',s)

def main():
 ap=argparse.ArgumentParser(); ap.add_argument('archive'); ap.add_argument('-o','--output',required=True); ap.add_argument('--translator',default=str(Path(__file__).with_name('translate_n32.py'))); ap.add_argument('--base',type=lambda x:int(x,0),default=0x50000000); a=ap.parse_args()
 out=Path(a.output); out.mkdir(parents=True,exist_ok=True); report={'archive':a.archive,'base':a.base,'objects':[],'translated':0,'failed':0}; cursor=a.base
 with tempfile.TemporaryDirectory() as td:
  parsed=[]
  for name,body in members(Path(a.archive).read_bytes()):
   if not body.startswith(b'\x7fELF\x01\x02'): continue
   sec,fs=parse(body); bases={}
   for s in sec:
    if s['flags']&SHF_ALLOC and s['size']:
     cursor=align(cursor,max(s['align'],16)); bases[s['no']]=cursor; cursor+=s['size']
   parsed.append((name,body,fs,bases))
  for oi,(name,body,fs,bases) in enumerate(parsed):
   obj=Path(td)/('%04d_'%oi+Path(name).name); obj.write_bytes(body); ore={'member':name,'section_bases':bases,'functions':[]}; od=out/('%04d_'%oi+Path(name).stem); od.mkdir(parents=True,exist_ok=True); prefix=ident(Path(name).stem)+'_'
   for fi,f in enumerate(fs):
    sb=bases.get(f['shndx'])
    if sb is None: continue
    addr=sb+f['value']; dst=od/('%04d_'%fi+ident(f['name'])+'.c')
    buf=io.StringIO(); err=None
    try:
     with contextlib.redirect_stdout(buf): emit(str(obj),f['name'],sb,prefix)
    except Exception as exc: err=str(exc)
    r={'name':f['name'],'address':addr,'size':f['size'],'section':f['shndx'],'ok':err is None}
    if err is not None: r['error']=err; report['failed']+=1
    else: dst.write_text(buf.getvalue()); report['translated']+=1
    ore['functions'].append(r)
   report['objects'].append(ore)
 report['end']=cursor; (out/'manifest.json').write_text(json.dumps(report,indent=2)+'\n')
 print(f"objects={len(report['objects'])} translated={report['translated']} failed={report['failed']} range={a.base:#x}-{cursor:#x}")
if __name__=='__main__': main()
