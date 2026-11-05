#!/bin/sh
S="${IRIX_SCHEMES:-$(cd "$(dirname "$0")/../../.." && pwd)/IRIX-TREE/usr/lib/X11/schemes}"
B="$S/Base"
# the palette variant follows the server gamma, like the IRIX programs do with _SGI_GAMMA_FLAG:
# below 1.35 GAMMA_1_0, up to 2.0 GAMMA_1_7, above that GAMMA_2_4 (no value means 1.0)
G="$(cat "${IRIX_GAMMA_FILE:-/etc/config/system.glGammaVal}" 2>/dev/null)"
GAMMA=$(echo "${G:-1.0}" | awk '{ if ($1 < 1.35) print "GAMMA_1_0"; else if ($1 <= 2.0) print "GAMMA_1_7"; else print "GAMMA_2_4" }')
DEFS="-D$GAMMA -DUSE_COLORS -DUSE_FONTS -DUSE_SPACING -DCOLOR"
for f in BaseColorPalette FontPalette Base; do
    [ -r "$B/$f" ] && cat "$B/$f"
done | sed "s/ViewKit applications' cursors/ViewKit applications cursors/" | xrdb -n $DEFS 2>/dev/null
