#!/usr/bin/env python3
"""Breadth-first static recompilation driver for the IRIX desktop stack."""
import argparse, json, subprocess, sys
from pathlib import Path

DEFAULT_LIBS=['libXt.a','libXm.a','libSgm.a','libvk.a','libMrm.a','libUil.a']

def find(root,name):
 hits=list(root.rglob(name))
 # Prefer N32 locations when multiple ABIs are installed.
 hits.sort(key=lambda p:(0 if 'lib32' in p.parts else 1,len(str(p))))
 return hits[0] if hits else None

def main():
 ap=argparse.ArgumentParser(); ap.add_argument('development_tree'); ap.add_argument('-o','--output',required=True); ap.add_argument('--libraries',nargs='*',default=DEFAULT_LIBS); a=ap.parse_args()
 root=Path(a.development_tree); out=Path(a.output); out.mkdir(parents=True,exist_ok=True); driver=Path(__file__).with_name('recomp_archive.py'); summary=[]
 for lib in a.libraries:
  p=find(root,lib)
  if not p: summary.append({'library':lib,'found':False}); print('MISS',lib); continue
  dst=out/lib; print('RECOMP',p)
  r=subprocess.run([sys.executable,str(driver),str(p),'-o',str(dst)])
  summary.append({'library':lib,'found':True,'path':str(p),'returncode':r.returncode})
 (out/'stack.json').write_text(json.dumps(summary,indent=2)+'\n')
if __name__=='__main__':main()
