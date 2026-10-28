#!/bin/bash
# Motif 1.2 (UnixWare X11R5 lib/Xm) for x86_64, linked against the host's libXt.
# Experimental sibling of build-motif.sh (which is i386 only).  Records which source
# files compile cleanly so the LP64 porting list is explicit.
#   UW7_SRC=~/uw7-src native-libs/build-motif64.sh [ OUT_DIR ]
set -u
here="$(cd "$(dirname "$0")" && pwd)"
OUT="${1:-$here/build/xm64}"
SRC="${UW7_SRC:-$HOME/uw7-src}/usr/src/common/X11R5/lib/Xm"
[ -d "$SRC" ] || { echo "no $SRC"; exit 1; }
rm -rf "$OUT"; mkdir -p "$OUT/src" "$OUT/obj" "$OUT/inc/Xm" "$OUT/log"
cp "$SRC"/*.c "$SRC"/*.h "$OUT/src/"
cp "$here/patches/Xm/uw7_compat.h" "$here/patches/Xm/lp64_decls.h" "$OUT/src/"
(cd "$OUT/src" && patch -s -p1 < "$here/patches/Xm/List.c.patch")
# Xt R6+ headers already define XtTypedArg/XtTypedArgList (R5 has no such types)
sed -i 's/^typedef struct _XtTypedArg {/#if XtSpecificationRelease < 6\ntypedef struct _XtTypedArg {/; s/^typedef struct _XtTypedArg\* XtTypedArgList;/typedef struct _XtTypedArg* XtTypedArgList;\n#endif/' "$OUT/src/VaSimpleP.h"
# LP64 fixes: addresses passed through 32-bit integer types (found with
# -Wpointer-to-int-cast; the rest of those warnings are small integers/handles)
sed -i 's/(Cardinal)info/(unsigned long)info/' "$OUT/src/DragBS.c"
sed -i 's/(CARD32)iccInfo/(unsigned long)iccInfo/' "$OUT/src/DragICC.c"
sed -i '683,725s/(unsigned) /(unsigned long) /g' "$OUT/src/TraversalI.c"
for h in "$OUT"/src/*.h; do ln -sf "$h" "$OUT/inc/Xm/$(basename "$h")"; done
FLAGS="-m64 -c -w -std=gnu89 -fPIC -g -O1 -I$OUT/inc -I$OUT/src -include $OUT/src/lp64_decls.h -DXmPATH_MODE_FULL=0 -DXmPATH_MODE_RELATIVE=1 -DFS_PathMode(w)=0"
export FLAGS OUT
: > "$OUT/failed.txt"
compile_one() {
    f="$1"; b="$(basename "$f" .c)"; extra=
    [ "$b" = IconFile ] && extra="-include $OUT/src/uw7_compat.h"
    gcc $FLAGS $extra "$f" -o "$OUT/obj/$b.o" 2> "$OUT/log/$b.log" || echo "$b" >> "$OUT/failed.txt"
}
export -f compile_one
ls "$OUT"/src/*.c | xargs -P4 -I{} bash -c 'compile_one {}'
bad=$(wc -l < "$OUT/failed.txt"); ok=$(ls "$OUT"/obj/*.o 2>/dev/null | wc -l)
echo "compiled $ok files, $bad failed (see $OUT/failed.txt)"
[ "$bad" -eq 0 ] || exit 2
gcc -m64 -shared -Wl,-soname,libXm.so.1 -o "$OUT/libXm.so.1" "$OUT"/obj/*.o -lXt -lXpm -lX11 -lSM -lICE
echo "built $OUT/libXm.so.1"
