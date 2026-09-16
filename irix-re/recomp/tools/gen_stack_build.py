#!/usr/bin/env python3
import argparse,json
from pathlib import Path

def main():
 ap=argparse.ArgumentParser(); ap.add_argument('root'); ap.add_argument('-o','--output',required=True); a=ap.parse_args(); root=Path(a.root); files=sorted(str(p) for p in root.rglob('*.c')); Path(a.output).write_text(json.dumps({'sources':files},indent=2)+'\n'); print('sources',len(files))
if __name__=='__main__':main()
