#!/usr/bin/env python3
"""Link SGI N32 ET_REL objects into a synthetic 32-bit guest image.

This is a recompiler-side linker, not a host ABI bridge. Unsupported or
ambiguous linkage is fatal rather than guessed.
"""
import argparse, json, struct
from pathlib import Path

ET_REL=1; SHT_SYMTAB=2; SHT_NOBITS=8; SHT_REL=9; SHF_ALLOC=2
R_MIPS_32=2; R_MIPS_26=4; R_MIPS_HI16=5; R_MIPS_LO16=6; R_MIPS_GPREL16=7
R_MIPS_LITERAL=8; R_MIPS_GOT16=9; R_MIPS_PC16=10; R_MIPS_CALL16=11
R_MIPS_GPREL32=12; R_MIPS_GOT_DISP=19; R_MIPS_GOT_PAGE=20; R_MIPS_GOT_OFST=21
R_MIPS_GOT_HI16=22; R_MIPS_GOT_LO16=23; R_MIPS_CALL_HI16=30; R_MIPS_CALL_LO16=31
R_MIPS_JALR=37
RN={2:'32',4:'26',5:'HI16',6:'LO16',7:'GPREL16',8:'LITERAL',9:'GOT16',10:'PC16',11:'CALL16',12:'GPREL32',19:'GOT_DISP',20:'GOT_PAGE',21:'GOT_OFST',22:'GOT_HI16',23:'GOT_LO16',30:'CALL_HI16',31:'CALL_LO16',37:'JALR'}

def align(v,a): return (v+a-1)&~(a-1) if a>1 else v
def sx16(v): return v-0x10000 if v&0x8000 else v
def u32(b,o): return struct.unpack_from('>I',b,o)[0]
def p16(b,o,v): struct.pack_into('>H',b,o,v&0xffff)
def p32(b,o,v): struct.pack_into('>I',b,o,v&0xffffffff)
def hi(v): return ((v+0x8000)>>16)&0xffff
def lo(v): return v&0xffff

class Obj:
 def __init__(self,path):
  self.path=Path(path); self.data=self.path.read_bytes()
  if self.data[:6]!=b'\x7fELF\x01\x02': raise ValueError(f'{path}: expected ELF32BE')
  h=struct.unpack_from('>16sHHIIIIIHHHHHH',self.data,0)
  if h[1]!=ET_REL: raise ValueError(f'{path}: expected ET_REL')
  self.shoff,self.shentsz,self.shnum,self.shstr=h[6],h[11],h[12],h[13]
  self.sec=[]
  for i in range(self.shnum):
   x=struct.unpack_from('>IIIIIIIIII',self.data,self.shoff+i*self.shentsz)
   self.sec.append(dict(no=i,nameoff=x[0],type=x[1],flags=x[2],off=x[4],size=x[5],link=x[6],info=x[7],align=x[8] or 1,entsz=x[9]))
  st=self.sec[self.shstr]; names=self.data[st['off']:st['off']+st['size']]
  for s in self.sec:
   e=names.find(b'\0',s['nameoff']); s['name']=names[s['nameoff']:e].decode('ascii','replace')
  self.sym=[]
  for s in self.sec:
   if s['type']==SHT_SYMTAB:
    ss=self.sec[s['link']]; strings=self.data[ss['off']:ss['off']+ss['size']]
    for i in range(s['size']//s['entsz']):
     n,v,z,info,other,sh=struct.unpack_from('>IIIBBH',self.data,s['off']+i*s['entsz'])
     e=strings.find(b'\0',n); name=strings[n:e].decode('ascii','replace') if n else ''
     self.sym.append(dict(no=i,name=name,value=v,size=z,bind=info>>4,type=info&15,shndx=sh))
    break
  self.base={}; self.image={}
 def alloc(self,c):
  for s in self.sec:
   if s['flags']&SHF_ALLOC and s['size']:
    c=align(c,max(s['align'],16)); self.base[s['no']]=c
    self.image[s['no']]=bytearray(s['size']) if s['type']==SHT_NOBITS else bytearray(self.data[s['off']:s['off']+s['size']])
    c+=s['size']
  return c
 def addr(self,s): return self.base[s['shndx']]+s['value'] if s['shndx'] in self.base else None

class Link:
 def __init__(self,base,gp=None): self.base=base; self.gp=gp; self.objs=[]; self.globals={}; self.got={}
 def add(self,p): self.objs.append(Obj(p))
 def layout(self):
  c=self.base
  for o in self.objs:c=o.alloc(c)
  self.got_base=align(c,16); self.nextgot=self.got_base
  for o in self.objs:
   for s in o.sym:
    a=o.addr(s)
    if a is not None and s['name'] and s['bind'] in (1,2):
     if s['name'] in self.globals and s['bind']==1: raise ValueError('duplicate global '+s['name'])
     self.globals.setdefault(s['name'],a)
  if self.gp is None:self.gp=self.got_base+0x7ff0
 def symaddr(self,o,s):
  a=o.addr(s)
  if a is not None:return a
  if s['name'] in self.globals:return self.globals[s['name']]
  raise KeyError(s['name'] or f'<sym#{s["no"]}>')
 def gotaddr(self,name,target):
  if name not in self.got:self.got[name]=(self.nextgot,target); self.nextgot+=4
  return self.got[name][0]
 def relocs(self,apply=False):
  out=[]
  for o in self.objs:
   for rs in o.sec:
    if rs['type']!=SHT_REL or rs['info'] not in o.image:continue
    ts=o.sec[rs['info']]; b=o.image[rs['info']]; pending_hi={}
    for q in range(0,rs['size'],rs['entsz'] or 8):
     ro,ri=struct.unpack_from('>II',o.data,rs['off']+q); sn=ri>>8; typ=ri&255; s=o.sym[sn]
     P=o.base[ts['no']]+ro; word=u32(b,ro); A=sx16(word&0xffff)
     try:S=self.symaddr(o,s)
     except KeyError:S=None
     rec=dict(object=o.path.name,section=ts['name'],offset=ro,address=P,type=RN.get(typ,str(typ)),symbol=s['name'],symbol_address=S); out.append(rec)
     if not apply or typ==R_MIPS_JALR:continue
     if S is None:raise ValueError(f'unresolved {s["name"]} for {RN.get(typ,typ)} at {P:#x}')
     if typ==R_MIPS_32:p32(b,ro,word+S)
     elif typ==R_MIPS_26:p32(b,ro,(word&0xfc000000)|(((S>>2)+(word&0x03ffffff))&0x03ffffff))
     elif typ==R_MIPS_GPREL16:p16(b,ro,S+A-self.gp)
     elif typ==R_MIPS_PC16:p16(b,ro,A+((S-(P+4))>>2))
     elif typ in (R_MIPS_GOT16,R_MIPS_CALL16,R_MIPS_GOT_DISP):p16(b,ro,self.gotaddr(s['name'],S)+A-self.gp)
     elif typ==R_MIPS_GPREL32:p32(b,ro,word+S-self.gp)
     elif typ in (R_MIPS_HI16,R_MIPS_GOT_HI16,R_MIPS_CALL_HI16):pending_hi.setdefault((sn,typ),[]).append((ro,A))
     elif typ in (R_MIPS_LO16,R_MIPS_GOT_LO16,R_MIPS_CALL_LO16):
      hk={R_MIPS_LO16:R_MIPS_HI16,R_MIPS_GOT_LO16:R_MIPS_GOT_HI16,R_MIPS_CALL_LO16:R_MIPS_CALL_HI16}[typ]
      value=S+A
      for ho,ha in pending_hi.pop((sn,hk),[]):p16(b,ho,hi(S+(ha<<16)+A))
      p16(b,ro,lo(value))
     elif typ==R_MIPS_GOT_PAGE:
      ga=self.gotaddr(s['name']+'@page',S&~0xffff); p16(b,ro,ga+A-self.gp)
     elif typ==R_MIPS_GOT_OFST:p16(b,ro,(S+A)&0xffff)
     elif typ==R_MIPS_LITERAL:p16(b,ro,S+A-self.gp)
     else:raise ValueError(f'unsupported relocation {typ} at {P:#x}')
    if apply and pending_hi:raise ValueError(f'unpaired HI relocation(s) in {o.path}:{ts["name"]}')
  return out
 def write_image(self,path):
  end=self.nextgot
  blob=bytearray(end-self.base)
  for o in self.objs:
   for no,b in o.image.items():
    a=o.base[no]; blob[a-self.base:a-self.base+len(b)]=b
  for _,(a,target) in self.got.items():p32(blob,a-self.base,target)
  Path(path).write_bytes(blob)

def main():
 ap=argparse.ArgumentParser(); ap.add_argument('objects',nargs='+'); ap.add_argument('--base',type=lambda x:int(x,0),default=0x50000000); ap.add_argument('--gp',type=lambda x:int(x,0)); ap.add_argument('--apply',action='store_true'); ap.add_argument('--image'); ap.add_argument('--json',action='store_true'); a=ap.parse_args()
 L=Link(a.base,a.gp)
 for p in a.objects:L.add(p)
 L.layout(); rr=L.relocs(a.apply)
 if a.image:
  if not a.apply:raise SystemExit('--image requires --apply')
  L.write_image(a.image)
 result={'base':L.base,'gp':L.gp,'got_base':L.got_base,'image_end':L.nextgot,'objects':[{'path':str(o.path),'sections':{o.sec[k]['name']:v for k,v in o.base.items()}} for o in L.objs],'globals':L.globals,'got':L.got,'relocations':rr}
 if a.json:print(json.dumps(result,indent=2))
 else:
  print(f'objects={len(L.objs)} globals={len(L.globals)} relocations={len(rr)} gp={L.gp:#x} got={L.got_base:#x}')
  for r in rr:print(f'{r["address"]:#010x} {r["type"]:10} {r["symbol"]}')
if __name__=='__main__':main()
