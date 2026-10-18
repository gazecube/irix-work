#!/bin/bash
set -eu
usage() { echo "Usage: UW7_SRC=~/uw7-src native-libs/build-motif-n32.sh [ XT_BUILD_DIR [ OUT_DIR ] ]  (needs build-xt.sh output for the Xt headers)"; }
here="$(cd "$(dirname "$0")" && pwd)"
XT="${1:-$here/build/xt32}"
OUT="${2:-$here/build/xm-n32}"
SRC="${UW7_SRC:-$HOME/uw7-src}/usr/src/common/X11R5/lib/Xm"
[ -d "$SRC" ] && [ -d "$XT/inc" ] || { usage; exit 1; }
rm -rf "$OUT"; mkdir -p "$OUT/src" "$OUT/obj" "$OUT/inc/Xm"
cp "$SRC"/*.c "$SRC"/*.h "$OUT/src/"
cp "$here/patches/Xm/uw7_compat.h" "$OUT/src/"
(cd "$OUT/src" && patch -s -p1 < "$here/patches/Xm/List.c.patch")
for h in "$OUT"/src/*.h; do ln -sf "$h" "$OUT/inc/Xm/$(basename "$h")"; done
FLAGS="-c -I$OUT/inc -I$XT/inc -I$OUT/src -DXmPATH_MODE_FULL=0 -DXmPATH_MODE_RELATIVE=1 -DFS_PathMode(w)=0"
for f in "$OUT"/src/*.c; do
    b="$(basename "$f" .c)"; in="$f"
    if [ "$b" = IconFile ]; then
        in="$OUT/src/IconFile_n32.c"; { echo '#include "uw7_compat.h"'; cat "$f"; } > "$in"
    fi
    "$here/n32cc.sh" $FLAGS "$in" -o "$OUT/obj/$b.o"
done
ld.lld -m elf32btsmipn32 -shared --unresolved-symbols=ignore-all -o "$OUT/libXm_n32.so" "$OUT"/obj/*.o
echo "built $OUT/libXm_n32.so (N32 big-endian, DWARF 4, with types)"
