#!/usr/bin/env python3
"""Recover the SGI X11 FAST_QUARKS string table from libX11.so.

IRIX X11 encodes common resource strings as negative pointer values. The
encoding used by _XQstring/Xrm{,Perm}StringToQuark is index = ~encoded. This
tool extracts the backing _SGIXQuarkNameTable from the original IRIX libX11
and emits either JSON or a C include for the native Xrm bridge.
"""
import argparse, json, struct
from pathlib import Path

PT_LOAD=1; SHT_SYMTAB=2; SHT_DYNSYM=11; STT_OBJECT=1

class Elf32BE:
 def __init__(self,path):
  self.data=Path(path).read_bytes()
  if self.data[:6]!=b'\x7fELF\x01\x02': raise ValueError('expected ELF32 big-endian input')
  h=struct.unpack_from('>16sHHIIIIIHHHHHH',self.data,0)
  self.phoff,self.shoff=h[5],h[6]; self.phentsz,self.phnum=h[9],h[10]; self.shentsz,self.shnum,self.shstr=h[11],h[12],h[13]
  self.ph=[]
  for i in range(self.phnum):self.ph.append(struct.unpack_from('>IIIIIIII',self.data,self.phoff+i*self.phentsz))
  self.sh=[]
  for i in range(self.shnum):self.sh.append(struct.unpack_from('>IIIIIIIIII',self.data,self.shoff+i*self.shentsz))
 def readva(self,addr,size):
  for p in self.ph:
   if p[0]==PT_LOAD and p[2]<=addr and addr+size<=p[2]+p[4]:return self.data[p[1]+addr-p[2]:p[1]+addr-p[2]+size]
  raise ValueError('address not file-backed: %#x'%addr)
 def symbol(self,name):
  for s in self.sh:
   if s[1] not in (SHT_SYMTAB,SHT_DYNSYM) or not s[9]:continue
   st=self.sh[s[6]]; strings=self.data[st[4]:st[4]+st[5]]
   for i in range(s[5]//s[9]):
    n,v,z,info,other,sh=struct.unpack_from('>IIIBBH',self.data,s[4]+i*s[9])
    if (info&15)!=STT_OBJECT:continue
    e=strings.find(b'\0',n); nm=strings[n:e].decode('ascii','replace') if n else ''
    if nm==name:return v,z
  raise KeyError(name)
 def cstr(self,addr):
  out=bytearray()
  while True:
   c=self.readva(addr+len(out),1)[0]
   if not c:return out.decode('latin1')
   out.append(c)

def extract(path):
 e=Elf32BE(path); base,size=e.symbol('_SGIXQuarkNameTable')
 if size%4:raise ValueError('_SGIXQuarkNameTable size is not pointer-aligned')
 out=[]
 for i in range(size//4):
  p=struct.unpack('>I',e.readva(base+i*4,4))[0]
  out.append(e.cstr(p) if p else None)
 return out

def main():
 ap=argparse.ArgumentParser(); ap.add_argument('libX11'); ap.add_argument('-o','--output'); ap.add_argument('--format',choices=('json','c'),default='json'); a=ap.parse_args(); names=extract(a.libX11)
 if a.format=='json': text=json.dumps({'count':len(names),'names':names},indent=2)+'\n'
 else:
  rows=['/* Generated from IRIX _SGIXQuarkNameTable. */','static const char *const irix_fast_quark_names[] = {']
  for s in names:
   if s is None:rows.append('    0,')
   else:rows.append('    '+json.dumps(s)+',')
  rows+=['};','static const unsigned irix_fast_quark_count = %du;'%len(names),'']; text='\n'.join(rows)
 if a.output:Path(a.output).write_text(text)
 else:print(text,end='')
if __name__=='__main__':main()
