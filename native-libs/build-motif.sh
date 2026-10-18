#!/bin/bash
set -eu
usage() { echo "Usage: UW7_SRC=~/uw7-src native-libs/build-motif.sh [ -m32 ] [ XT_BUILD_DIR [ OUT_DIR ] ]  (run build-xt.sh first)"; }
here="$(cd "$(dirname "$0")" && pwd)"
ARCH=-m32
[ "${1:-}" = "-m32" ] && shift
XT="${1:-$here/build/xt32}"
OUT="${2:-$here/build/xm32}"
SRC="${UW7_SRC:-$HOME/uw7-src}/usr/src/common/X11R5/lib/Xm"
[ -d "$SRC" ] && [ -f "$XT/libXt.so.6" ] || { usage; exit 1; }
rm -rf "$OUT"; mkdir -p "$OUT/src" "$OUT/obj" "$OUT/inc/Xm"
cp "$SRC"/*.c "$SRC"/*.h "$OUT/src/"
cp "$here/patches/Xm/uw7_compat.h" "$OUT/src/"
(cd "$OUT/src" && patch -s -p1 < "$here/patches/Xm/List.c.patch")
for h in "$OUT"/src/*.h; do ln -sf "$h" "$OUT/inc/Xm/$(basename "$h")"; done
FLAGS="$ARCH -c -w -std=gnu89 -fPIC -g -O1 -I$OUT/inc -I$XT/inc -I$OUT/src -DXmPATH_MODE_FULL=0 -DXmPATH_MODE_RELATIVE=1 -DFS_PathMode(w)=0"
for f in "$OUT"/src/*.c; do
    b="$(basename "$f" .c)"; extra=
    [ "$b" = IconFile ] && extra="-include $OUT/src/uw7_compat.h"
    gcc $FLAGS $extra "$f" -o "$OUT/obj/$b.o"
done
gcc $ARCH -shared -o "$OUT/libXm.so.1" "$OUT"/obj/*.o -L/usr/lib32 -L"$XT" -l:libXt.so.6 -lXpm -lX11 -lSM -lICE
echo "built $OUT/libXm.so.1"
