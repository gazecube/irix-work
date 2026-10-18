#!/bin/bash
set -eu
usage() { echo "Usage: UW7_SRC=~/uw7-src native-libs/build-xt.sh [ -m32 | -m64 ] [ builddir ]"; }
here="$(cd "$(dirname "$0")" && pwd)"
ARCH=-m32
[ "${1:-}" = "-m64" ] && { ARCH=-m64; shift; } || { [ "${1:-}" = "-m32" ] && shift; }
OUT="${1:-$here/build/xt${ARCH#-m}}"
SRC="${UW7_SRC:-$HOME/uw7-src}/usr/src/common/X11R5/lib/Xt"
[ -d "$SRC" ] || { usage; echo "no Xt source at $SRC" >&2; exit 1; }
rm -rf "$OUT"; mkdir -p "$OUT/src" "$OUT/obj" "$OUT/inc/X11"
cp "$SRC"/* "$OUT/src/"
cp "$here"/patches/Xt/* "$OUT/src/"
for h in "$OUT"/src/*.h; do ln -sf "$h" "$OUT/inc/X11/$(basename "$h")"; done
LIBDIR=; [ "$ARCH" = -m32 ] && LIBDIR=-L/usr/lib32
for f in "$OUT"/src/*.c; do
    gcc $ARCH -c -w -std=gnu89 -fPIC -g -O1 -I"$OUT/inc" -I"$OUT/src" -DXTHREADS=0 "$f" -o "$OUT/obj/$(basename "$f" .c).o"
done
gcc $ARCH -shared -o "$OUT/libXt.so.6" "$OUT"/obj/*.o $LIBDIR -lX11 -lSM -lICE
echo "built $OUT/libXt.so.6 (headers in $OUT/inc)"
