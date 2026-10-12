#!/bin/sh
# Build placeholder N32 libGL.so/libGLcore.so/libgl.so into IRIX-TREE/opt/lib32
# so GL-probing programs (clogin via libil) load and fall back to software.
here="$(cd "$(dirname "$0")" && pwd)"
R="$(cd "$here/../../.." && pwd)"
out="${1:-$R/IRIX-TREE/opt/lib32}"
mkdir -p "$out"
clang --target=mips64-unknown-linux-gnuabin32 -mabi=n32 -march=mips3 -fPIC \
    -fno-builtin -c "$here/glstub.c" -o "$out/glstub.o" || exit 1
base=0x0e000000
for n in libGL.so libGLcore.so libgl.so; do
    ld.lld -m elf32btsmipn32 -shared -soname $n --image-base=$base \
        -z max-page-size=0x1000 -z separate-loadable-segments --no-rosegment \
        --allow-shlib-undefined -o "$out/$n" "$out/glstub.o" \
        "$R/IRIX-TREE/lib32/libc.so.1" || exit 1
    base=$(printf "0x%x" $((base + 0x100000)))
done
ln -sf libGL.so "$out/libGL.so.1"
rm -f "$out/glstub.o"
