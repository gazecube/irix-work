#!/bin/sh
# Build the X11R5 xclock for the host from the uw7-src Xaw Clock widget.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
src=${UW7_X11R5:-$HOME/uw7-src/usr/src/common/X11R5}/lib/Xaw
out=${1:-$here/bin}
mkdir -p "$out" "$out/inc/X11/Xaw"
cp "$src/Clock.h" "$src/ClockP.h" "$out/inc/X11/Xaw/"
cc -std=gnu89 -O2 -w -fcommon -I"$out/inc" \
    -o "$out/xclock" "$here/xclock.c" "$src/Clock.c" \
    -lXaw7 -lXmu -lXt -lXext -lX11 -lm
echo "built $out/xclock"
