#!/bin/sh
S="${IRIX_SCHEMES:-$(cd "$(dirname "$0")/../../.." && pwd)/IRIX-TREE/usr/lib/X11/schemes}"
B="$S/Base"
DEFS="-DGAMMA_1_0 -DUSE_COLORS -DUSE_FONTS -DUSE_SPACING -DCOLOR"
for f in BaseColorPalette FontPalette Base; do
    [ -r "$B/$f" ] && cat "$B/$f"
done | sed "s/ViewKit applications' cursors/ViewKit applications cursors/" | xrdb -n $DEFS 2>/dev/null
