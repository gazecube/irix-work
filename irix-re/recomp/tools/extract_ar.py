#!/usr/bin/env python3
"""Extract Unix ar members without depending on host ar understanding MIPS."""
import argparse
from pathlib import Path

def members(data):
 if data[:8]!=b'!<arch>\n': raise ValueError('not an ar archive')
 p=8; strings=b''
 while p+60<=len(data):
  h=data[p:p+60]; p+=60
  if h[58:60]!=b'`\n': raise ValueError('bad ar member header')
  raw=h[:16].decode('ascii','replace').strip(); n=int(h[48:58].decode().strip() or '0')
  body=data[p:p+n]; p+=(n+1)&~1
  if raw=='//': strings=body; continue
  if raw=='/': continue
  if raw.startswith('/') and raw[1:].isdigit() and strings:
   q=int(raw[1:]); e=strings.find(b'/\n',q); name=strings[q:e].decode('utf-8','replace')
  else:name=raw.rstrip('/')
  yield name,body

def main():
 ap=argparse.ArgumentParser(); ap.add_argument('archive'); ap.add_argument('-C','--directory',default='.'); ap.add_argument('names',nargs='*'); a=ap.parse_args()
 out=Path(a.directory); out.mkdir(parents=True,exist_ok=True); wanted=set(a.names)
 count=0
 for name,body in members(Path(a.archive).read_bytes()):
  if wanted and name not in wanted:continue
  dst=out/Path(name).name; dst.write_bytes(body); print(dst); count+=1
 if wanted and count!=len(wanted):raise SystemExit('one or more requested members were not found')
if __name__=='__main__':main()
