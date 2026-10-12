#!/bin/sh
# n32link.sh OUT objects... [-llib ...]   link an IRIX N32 program with lld.
here="$(cd "$(dirname "$0")" && pwd)"
R="${IRIX_WORK:-$(cd "$here/../.." && pwd)}"
out=$1; shift
crt="${N32_CRT1:-${TMPDIR:-/tmp}/n32-crt1.$$.o}"
clang --target=mips64-unknown-linux-gnuabin32 -mabi=n32 -march=mips3 -fPIC \
    -c "$here/crt1.S" -o "$crt" 2>/dev/null || exit 1
libs=
for a in "$@"; do
    case "$a" in
        -l*) n=${a#-l}
             for d in "$R/IRIX-TREE/usr/lib32" "$R/IRIX-TREE/lib32"; do
                 [ -f "$d/lib$n.so" ] && { libs="$libs $d/lib$n.so"; break; }
                 [ -f "$d/lib$n.so.1" ] && { libs="$libs $d/lib$n.so.1"; break; }
             done ;;
        *) objs="$objs $a" ;;
    esac
done
ld.lld -m elf32btsmipn32 --allow-shlib-undefined \
    --dynamic-linker=/usr/lib32/libc.so.1 -e __start --image-base=0x10000000 \
    -z max-page-size=0x1000 -z separate-loadable-segments --no-rosegment \
    -o "$out" "$crt" $objs $libs "$R/IRIX-TREE/lib32/libc.so.1"
rc=$?
rm -f "$crt"
exit $rc
