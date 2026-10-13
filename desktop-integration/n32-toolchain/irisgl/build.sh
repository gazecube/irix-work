#!/bin/sh
# Build the IRIS GL-over-Xlib libgl.so (N32) into IRIX-TREE/opt/lib32.
here="$(cd "$(dirname "$0")" && pwd)"
R="$(cd "$here/../../.." && pwd)"
out="${1:-$R/IRIX-TREE/opt/lib32}"
mkdir -p "$out"
"$here/../n32cc.sh" -O2 -c "$here/iglxlib.c" -o "$out/iglxlib.o" -I"$R/holyshit/usr/include" || exit 1
ld.lld -m elf32btsmipn32 -shared -soname libgl.so --image-base=0x0e000000 \
    -z max-page-size=0x1000 -z separate-loadable-segments --no-rosegment \
    --allow-shlib-undefined -o "$out/libgl.so" "$out/iglxlib.o" \
    "$R/IRIX-TREE/usr/lib32/libX11.so.1" "$R/IRIX-TREE/lib32/libc.so.1" \
    "$R/IRIX-TREE/usr/lib32/mips4/libm.so" 2>&1 | head -5
rm -f "$out/iglxlib.o"
ls -la "$out/libgl.so"
