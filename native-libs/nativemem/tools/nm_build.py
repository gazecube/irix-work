#!/usr/bin/env python3
"""Translate an N32 static archive to native-memory C plus a swapped image.

    nm_build.py LIB.a CORPUS_LIB.so OUTDIR
"""
import os
import sys
from pathlib import Path

here = Path(__file__).resolve().parent
tools = here.parents[2] / 'irix-re' / 'recomp' / 'tools'
sys.path.insert(0, str(tools))
sys.path.insert(0, str(here))
os.environ['NATIVE_MEM'] = '1'

import link_stack_n32 as L          # noqa: E402
import nm_swap                      # noqa: E402

archive, so, out = sys.argv[1:4]
orig_write = L.StackLink.write


def write(self, outdir):
    nm_swap.apply(self, so)
    orig_write(self, outdir)
    nm_swap.swap_got(self, str(Path(outdir) / 'small.bin'))


L.StackLink.write = write
import recomp_linked_stack as R      # noqa: E402

sys.argv = ['recomp_linked_stack.py', archive, '-o', str(Path(out) / 'gen')]
R.main()
