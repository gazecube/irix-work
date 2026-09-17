#!/usr/bin/env python3
import argparse,json,struct,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).parent))
from extract_ar import members

SHT_PROGBITS=1; SHT_SYMTAB=2; SHT_RELA=4; SHT_NOBITS=8; SHT_REL=9
SHF_ALLOC=2; SHN_UNDEF=0; SHN_ABS=0xfff1; SHN_COMMON=0xfff2
STB_LOCAL=0; STB_GLOBAL=1; STB_WEAK=2; STT_OBJECT=1; STT_FUNC=2; STT_SECTION=3
R_MIPS_32=2; R_MIPS_HI16=5; R_MIPS_LO16=6; R_MIPS_GPREL16=7; R_MIPS_CALL16=11
R_MIPS_GOT_DISP=19; R_MIPS_GOT_PAGE=20; R_MIPS_GOT_OFST=21; R_MIPS_SUB=24; R_MIPS_SCN_DISP=32; R_MIPS_JALR=37
SMALL={'.srdata','.sdata','.sbss','.lit4','.lit8'}

def align(v,a): return (v+a-1)&~(a-1) if a>1 else v
def sx16(x): return x-0x10000 if x&0x8000 else x
def p16(b,o,v): struct.pack_into('>H',b,o,v&0xffff)
def p32(b,o,v): struct.pack_into('>I',b,o,v&0xffffffff)
def u32(b,o): return struct.unpack_from('>I',b,o)[0]
def hi(v): return ((v+0x8000)>>16)&0xffff

def sgi_c_locale_ctype():
 # SGI IRIX usr/include/ctype.h + usr/include/locale_attr.h define
 # __ctype_t as 257 uint32 class entries, 257 int16 lower entries,
 # 257 int16 upper entries, then 8 bytes of width/padding data.  The
 # values below reproduce the C-locale __ctype_t in usr/lib/abi/libc.so
 # byte-for-byte.
 out=bytearray(); cls=[0]*257
 for c in range(256):
  v=0
  if c<32 or c==127: v|=0x00000020
  if 9<=c<=13: v|=0x00000008
  if c==9: v|=0x80000000
  if c==32: v=0x80000000|0x00008000|0x00000008
  if 33<=c<=47 or 58<=c<=64 or 91<=c<=96 or 123<=c<=126: v=0x00000010
  if 48<=c<=57: v=0x00000084
  if 65<=c<=70: v=0x00000081
  elif 71<=c<=90: v=0x00000001
  if 97<=c<=102: v=0x00000082
  elif 103<=c<=122: v=0x00000002
  if c>=128: v=0
  cls[c+1]=v
 for v in cls: out+=struct.pack('>I',v)
 lower=[-1]+list(range(256)); upper=[-1]+list(range(256))
 for c in range(65,91): lower[c+1]=c+32
 for c in range(97,123): upper[c+1]=c-32
 for v in lower: out+=struct.pack('>h',v)
 for v in upper: out+=struct.pack('>h',v)
 out+=b'\0'*8
 assert len(out)==2064
 return bytes(out)

class Obj:
 def __init__(self,archive,name,data):
  self.archive=archive; self.name=name; self.data=data; self.sec=[]; self.syms=[]; self.base={}; self.image={}; self.gp0=0
  h=struct.unpack_from('>16sHHIIIIIHHHHHH',data,0); self.shoff,self.shentsz,self.shnum,self.shstr=h[6],h[11],h[12],h[13]
  raw=[struct.unpack_from('>IIIIIIIIII',data,self.shoff+i*self.shentsz) for i in range(self.shnum)]
  st=raw[self.shstr]; names=data[st[4]:st[4]+st[5]]
  for i,x in enumerate(raw):
   no=x[0]; e=names.find(b'\0',no); nm=names[no:e].decode('ascii','replace') if no else ''
   self.sec.append(dict(no=i,name=nm,type=x[1],flags=x[2],off=x[4],size=x[5],link=x[6],info=x[7],align=x[8] or 1,entsz=x[9]))
  for s in self.sec:
   if s['type']==SHT_SYMTAB:
    ss=self.sec[s['link']]; strings=data[ss['off']:ss['off']+ss['size']]
    for i in range(s['size']//s['entsz']):
     n,v,z,info,other,sh=struct.unpack_from('>IIIBBH',data,s['off']+i*s['entsz'])
     e=strings.find(b'\0',n); nm=strings[n:e].decode('ascii','replace') if n else ''
     self.syms.append(dict(no=i,name=nm,value=v,size=z,bind=info>>4,type=info&15,shndx=sh))
    break
  for s in self.sec:
   if s['name']=='.reginfo' and s['size']>=24:
    self.gp0=struct.unpack_from('>I',data,s['off']+20)[0]
  if not self.gp0: self.gp0=0x7ff0
 def alloc_section(self,s,addr):
  self.base[s['no']]=addr
  self.image[s['no']]=bytearray(s['size']) if s['type']==SHT_NOBITS else bytearray(self.data[s['off']:s['off']+s['size']])
 def local_addr(self,sym):
  if sym['shndx'] in self.base: return self.base[sym['shndx']]+sym['value']
  if sym['shndx']==SHN_ABS: return sym['value']
  return None

class StackLink:
 def __init__(self,base=0x50000000,small_base=0x60000000,import_base=0x70000000):
  self.base=base; self.small_base=small_base; self.import_base=import_base; self.objs=[]; self.globals={}; self.imports={}; self.data_imports={}; self.synthetic_data={}; self.got={}; self.duplicates=[]
 def add_archive(self,p):
  for name,data in members(Path(p).read_bytes()):
   if data.startswith(b'\x7fELF\x01\x02'): self.objs.append(Obj(str(p),name,data))
 def layout(self):
  main=self.base; small=self.small_base
  # ordinary runtime sections first
  for o in self.objs:
   for s in o.sec:
    if s['type'] not in (SHT_PROGBITS,SHT_NOBITS) or not (s['flags']&SHF_ALLOC) or not s['size'] or s['name'] in SMALL: continue
    main=align(main,max(s['align'],16)); o.alloc_section(s,main); main+=s['size']
  # small-data sections are kept in one GP window
  for o in self.objs:
   for s in o.sec:
    if s['type'] not in (SHT_PROGBITS,SHT_NOBITS) or not (s['flags']&SHF_ALLOC) or not s['size'] or s['name'] not in SMALL: continue
    small=align(small,max(s['align'],8)); o.alloc_section(s,small); small+=s['size']
  self.main_end=main; self.small_data_end=small
  # collect globals after sections have addresses
  for o in self.objs:
   for s in o.syms:
    a=o.local_addr(s)
    if a is None or not s['name'] or s['bind'] not in (STB_GLOBAL,STB_WEAK): continue
    old=self.globals.get(s['name'])
    if old is None or (old[1]==STB_WEAK and s['bind']==STB_GLOBAL): self.globals[s['name']]=(a,s['bind'],o.name)
    elif s['bind']==STB_GLOBAL and old[1]==STB_GLOBAL and old[0]!=a: self.duplicates.append((s['name'],old[2],o.name))
  # allocate COMMONs in main BSS-like tail
  for o in self.objs:
   for s in o.syms:
    if s['shndx']!=SHN_COMMON or not s['name']: continue
    if s['name'] in self.globals: continue
    main=align(main,max(s['value'],8)); self.globals[s['name']]=(main,s['bind'],o.name); main+=s['size']
  # Undefined object symbols are data imports, not callable stubs. Give them
  # real zero-initialized guest storage so translated code may dereference
  # them safely. Keep STT_FUNC and ambiguous STT_NOTYPE symbols on the existing
  # import-stub path.
  for o in self.objs:
   for s in o.syms:
    if s['shndx']!=SHN_UNDEF or s['type']!=STT_OBJECT or not s['name'] or s['bind'] not in (STB_GLOBAL,STB_WEAK): continue
    if s['name'] in self.globals or s['name'] in self.data_imports: continue
    size=max(s['size'],8)
    a=align(main,8)
    self.data_imports[s['name']]=(a,size,o.name)
    main=a+size
  # __libc_attr is initialized data in IRIX libc, not BSS.  In the default C
  # locale its first N32 word points at a __ctype_t table.  Materialize the
  # exact SGI C-locale table and initialize the scalar fields we can reproduce
  # without importing libc code pointers.
  if '__libc_attr' in self.data_imports:
   attr,size,owner=self.data_imports['__libc_attr']
   if size<92: raise ValueError(f'__libc_attr data import is only {size} bytes')
   ctype=sgi_c_locale_ctype(); ctype_addr=align(main,8); main=ctype_addr+len(ctype)
   self.synthetic_data[ctype_addr]=ctype
   blob=bytearray(size); p32(blob,0,ctype_addr)
   # __attr_t._csinfo for the C locale: eucwidth={1,0,0}, scrwidth={1,0,0}, mb_cur_max=1.
   blob[4:11]=bytes((1,0,0,1,0,0,1))
   # _euc_func._is_euc and its callback pointers remain zero for the C locale.
   blob[0x1c]=1  # _collate_res._coll_as_cmp
   self.synthetic_data[attr]=bytes(blob)
  self.main_end=main
  # GP sits at +0x8000 from small region base. GOT grows after small data.
  self.gp=self.small_base+0x8000; self.nextgot=align(self.small_data_end,4)
 def import_addr(self,name):
  if name not in self.imports: self.imports[name]=self.import_base+len(self.imports)*8
  return self.imports[name]
 def symaddr(self,o,s):
  a=o.local_addr(s)
  if a is not None: return a
  if s['name'] in self.globals: return self.globals[s['name']][0]
  if s['name'] in self.data_imports: return self.data_imports[s['name']][0]
  if s['shndx']==SHN_UNDEF and s['name']: return self.import_addr(s['name'])
  raise ValueError(f'unresolvable symbol {s["name"]!r} in {o.name}')
 def gotaddr(self,key,value):
  old=self.got.get(key)
  if old is not None:
   if old[1]!=value: raise ValueError(f'GOT key collision {key}')
   return old[0]
  a=self.nextgot; self.nextgot+=4; self.got[key]=(a,value); return a
 def check16(self,v,what):
  if not -0x8000<=v<=0x7fff: raise OverflowError(f'{what}: GP displacement {v} out of range')
  return v
 def patch_rel(self,o,rs):
  ts=o.sec[rs['info']]
  if ts['no'] not in o.image: return 0
  b=o.image[ts['no']]; count=0; ent=rs['entsz'] or 8
  for q in range(rs['off'],rs['off']+rs['size'],ent):
   ro,ri=struct.unpack_from('>II',o.data,q); sn=ri>>8; typ=ri&255; sym=o.syms[sn]; S=self.symaddr(o,sym); word=u32(b,ro)
   if typ==R_MIPS_32: p32(b,ro,S+word)
   elif typ==R_MIPS_CALL16:
    G=self.gotaddr(('sym',sym['name'],S),S); d=self.check16(G-self.gp,'CALL16 '+sym['name']); p16(b,ro+2,d)
   elif typ==R_MIPS_SCN_DISP:
    # Sections are not concatenated in this synthetic image, so S-scn_addr is zero.
    # The stored section-relative addend therefore remains unchanged.
    pass
   elif typ==R_MIPS_GPREL16:
    A=sx16(word&0xffff)
    if sym['bind']==STB_LOCAL and sym['type']==STT_SECTION: v=A+S+o.gp0-self.gp
    else: v=A+S-self.gp
    self.check16(v,'GPREL16 '+(sym['name'] or '<section>')); p16(b,ro+2,v)
   else: raise ValueError(f'unsupported runtime REL type {typ} {o.name}:{ts["name"]}+{ro:#x}')
   count+=1
  return count
 def patch_rela(self,o,rs):
  ts=o.sec[rs['info']]
  if ts['no'] not in o.image: return 0
  b=o.image[ts['no']]; ent=rs['entsz'] or 12; raw=[]
  for q in range(rs['off'],rs['off']+rs['size'],ent):
   ro,ri,A=struct.unpack_from('>IIi',o.data,q); raw.append((ro,ri>>8,ri&255,A))
  count=0; i=0
  while i<len(raw):
   ro=raw[i][0]; group=[]
   while i<len(raw) and raw[i][0]==ro: group.append(raw[i]); i+=1
   types=[x[2] for x in group]
   if types==[R_MIPS_JALR]: count+=1; continue
   if types in ([R_MIPS_GPREL16,R_MIPS_SUB,R_MIPS_HI16],[R_MIPS_GPREL16,R_MIPS_SUB,R_MIPS_LO16]):
    _,sn,_,A=group[0]; sym=o.syms[sn]; S=self.symaddr(o,sym); v=self.gp-(S+A)-(o.gp0 if sym['bind']==STB_LOCAL and sym['type']==STT_SECTION else 0)
    p16(b,ro+2,hi(v) if types[-1]==R_MIPS_HI16 else v); count+=3; continue
   if len(group)!=1: raise ValueError(f'unknown RELA composition {types} in {o.name}:{ts["name"]}+{ro:#x}')
   _,sn,typ,A=group[0]; sym=o.syms[sn]; S=self.symaddr(o,sym)
   if typ==R_MIPS_GOT_DISP:
    G=self.gotaddr(('sym',sym['name'],S),S); d=self.check16(G-self.gp,'GOT_DISP '+sym['name']); p16(b,ro+2,d)
   elif typ==R_MIPS_GOT_PAGE:
    page=(S+A+0x8000)&~0xffff; G=self.gotaddr(('page',page),page); d=self.check16(G-self.gp,'GOT_PAGE '+sym['name']); p16(b,ro+2,d)
   elif typ==R_MIPS_GOT_OFST:
    page=(S+A+0x8000)&~0xffff; p16(b,ro+2,S+A-page)
   elif typ==R_MIPS_JALR: pass
   else: raise ValueError(f'unsupported runtime RELA type {typ} in {o.name}:{ts["name"]}+{ro:#x}')
   count+=1
  return count
 def relocate(self):
  n=0
  for o in self.objs:
   for rs in o.sec:
    if rs['type']==SHT_REL: n+=self.patch_rel(o,rs)
    elif rs['type']==SHT_RELA: n+=self.patch_rela(o,rs)
  self.relocations=n
  if self.nextgot-self.small_base>0x10000: raise OverflowError(f'small-data + GOT window is {self.nextgot-self.small_base:#x}, exceeds 64KiB GP window')
 def write(self,outdir):
  out=Path(outdir); out.mkdir(parents=True,exist_ok=True)
  main=bytearray(self.main_end-self.base); small=bytearray(self.nextgot-self.small_base)
  for o in self.objs:
   for no,b in o.image.items():
    a=o.base[no]
    dst=small if self.small_base<=a<self.small_base+0x10000 else main; origin=self.small_base if dst is small else self.base
    dst[a-origin:a-origin+len(b)]=b
  for a,b in self.synthetic_data.items(): main[a-self.base:a-self.base+len(b)]=b
  for _,(a,v) in self.got.items(): p32(small,a-self.small_base,v)
  (out/'main.bin').write_bytes(main); (out/'small.bin').write_bytes(small)
  mani={'base':self.base,'main_end':self.main_end,'small_base':self.small_base,'small_end':self.nextgot,'gp':self.gp,'import_base':self.import_base,'relocations':self.relocations,'objects':[], 'globals':{k:v[0] for k,v in self.globals.items()},'imports':self.imports,'data_imports':{k:[a,z,owner] for k,(a,z,owner) in self.data_imports.items()},'synthetic_data':{hex(a):len(b) for a,b in self.synthetic_data.items()},'got':{str(k):[a,v] for k,(a,v) in self.got.items()},'duplicates':self.duplicates}
  for o in self.objs: mani['objects'].append({'archive':o.archive,'member':o.name,'gp0':o.gp0,'sections':{o.sec[n]['name']:a for n,a in o.base.items()}})
  (out/'link.json').write_text(json.dumps(mani,indent=2)+'\n')

def main():
 ap=argparse.ArgumentParser(); ap.add_argument('archives',nargs='+'); ap.add_argument('-o','--output',required=True); a=ap.parse_args(); L=StackLink()
 for p in a.archives:L.add_archive(p)
 L.layout(); L.relocate(); L.write(a.output)
 print(f'objects={len(L.objs)} globals={len(L.globals)} imports={len(L.imports)} data_imports={len(L.data_imports)} got={len(L.got)} relocs={L.relocations}')
 print(f'main={L.base:#x}-{L.main_end:#x} small={L.small_base:#x}-{L.nextgot:#x} gp={L.gp:#x} duplicates={len(L.duplicates)}')
if __name__=='__main__': main()