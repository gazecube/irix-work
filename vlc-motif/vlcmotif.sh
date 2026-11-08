#!/bin/sh
# Launcher for the Motif VLC front end: SGI scheme palette (like the Netscape launcher), lifted libXm, 64-bit helper vlcd.
D=${VLCMOTIF_HOME:-/opt/vlcmotif}
NS=${NETSCAPE_HOME:-/opt/netscape}
G=$(xprop -root _SGI_GAMMA_FLAG 2>/dev/null | sed -n 's/.*= "\(.*\)".*/\1/p')
[ -z "$G" ] && G=$(cat /etc/config/system.glGammaVal 2>/dev/null)
GV=$(echo "${G:-1.0}" | awk '{ if ($1 < 1.35) print "GAMMA_1_0"; else if ($1 <= 2.0) print "GAMMA_1_7"; else print "GAMMA_2_4" }')
REALHOME=$(getent passwd "$(id -un)" | cut -d: -f1,6 | cut -d: -f2)
SCHEMEDIR="${IRIX_SCHEMES:-${REALHOME:-$HOME}/Projects/irix-work/IRIX-TREE/usr/lib/X11/schemes}"
SCHEME=$(xrdb -query 2>/dev/null | awk -F'\t' '$1=="*scheme:" {print $2; exit}')
[ -n "$SCHEME" ] && [ -r "$SCHEMEDIR/$SCHEME/BaseColorPalette" ] || SCHEME=Base
SCHEMECOLORS=$( { cat "$SCHEMEDIR/$SCHEME/BaseColorPalette"; cat "$SCHEMEDIR/Base/Base"; } 2>/dev/null | sed "s/ViewKit applications' cursors/ViewKit applications cursors/" | xrdb -n -D$GV -DUSE_COLORS -DCOLOR 2>/dev/null )
BG=$(echo "$SCHEMECOLORS" | awk -F'\t' '$1=="*background:" {print $2; exit}')
case "$BG" in '#'*) ;; *) BG='#d9d9d9' ;; esac
mkdir -p "$HOME/.vlcmotif"
# the same look as the Netscape bundle: its SGI resource file plus the scheme's colours
if [ -r "$NS/share/sgi.ad" ]; then sed "s/@SCHEME_BG@/$BG/g; s/Netscape\*/Vlcmotif*/g" "$NS/share/sgi.ad" > "$HOME/.vlcmotif/sgi.ad"; else : > "$HOME/.vlcmotif/sgi.ad"; fi
echo "$SCHEMECOLORS" | grep -i -E '(background|foreground|color|shadow)[A-Za-z]*:' | sed 's/^\*/Vlcmotif*/' >> "$HOME/.vlcmotif/sgi.ad"
export XENVIRONMENT="$HOME/.vlcmotif/sgi.ad"
export LD_PRELOAD="$NS/lib32/libirixshim32.so"
export LD_LIBRARY_PATH="$NS/lib32:/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
[ -n "$VLCMOTIF_HELPER" ] || VLCMOTIF_HELPER="$D/bin/vlcd"
export VLCMOTIF_HELPER
exec "$D/bin/vlcmotif" "$@"
