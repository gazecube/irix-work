#!/usr/bin/env python3
import argparse,collections,json,re
from pathlib import Path

def main():
 ap=argparse.ArgumentParser(); ap.add_argument('manifests',nargs='+'); a=ap.parse_args(); errors=collections.Counter(); funcs=[]
 for p in a.manifests:
  m=json.loads(Path(p).read_text())
  for o in m['objects']:
   for f in o['functions']:
    if f['ok']:continue
    e=f.get('error','unknown'); q=re.search(r'unsupported ([^\n]+?)(?: at 0x[0-9a-fA-F]+|$)',e); key=q.group(1) if q else e.splitlines()[-1]; errors[key]+=1; funcs.append((o['member'],f['name'],key))
 print('failed functions:',len(funcs)); print('\nfailure classes:')
 for k,n in errors.most_common():print('%6d  %s'%(n,k))
 print('\nfunctions:')
 for o,f,k in funcs:print('%s:%s: %s'%(o,f,k))
if __name__=='__main__':main()
