#!/bin/sh
# builds ewmhd and its test client with the host compiler
cd "$(dirname "$0")" || exit 1
cc -O2 -Wall -o ewmhd ewmhd.c -lX11 && cc -O2 -Wall -o test-ewmhd test-ewmhd.c -lX11 -lXtst && echo built
