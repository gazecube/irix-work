#!/usr/bin/env python3
"""Generate C registration code from recomp_archive.py manifests."""
import argparse,json,re
from pathlib import Path

def ident(s):return re.sub(r'[^A-Za-z0-9_]','_',s)
def main():
 ap=argparse.ArgumentParser(); ap.add_argument('manifests',nargs='+'); ap.add_argument('-o','--output',required=True); a=ap.parse_args(); rows=[]
 for mp in a.manifests:
  m=json.loads(Path(mp).read_text())
  for o in m['objects']:
   obj=Path(o['member']).stem
   for f in o['functions']:
    if f['ok']:rows.append((f['address'],obj,f['name']))
 rows.sort()
 with open(a.output,'w') as w:
  w.write('#include "IrixGuest.h"\n\n')
  for _,obj,n in rows:w.write('extern void recomp_%s_%s(IrixRuntime *, IrixCpu *);\n'%(ident(obj),ident(n)))
  w.write('\nvoid irix_register_recompiled_stack(IrixRuntime *rt)\n{\n')
  for addr,obj,n in rows:w.write('    irix_guest_register_recompiled_function(rt, 0x%08xu, recomp_%s_%s);\n'%(addr,ident(obj),ident(n)))
  w.write('}\n')
 print('registered',len(rows),'functions')
if __name__=='__main__':main()
