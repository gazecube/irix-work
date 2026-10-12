#!/bin/sh
# Build the original IRIX 6.5.7m osview source as an N32 program and install
# it into the IRIX tree as /usr/sbin/osview (the shipped binary is o32, which
# this runtime cannot load).  The shipped file is kept as osview.o32.
set -eu
here="$(cd "$(dirname "$0")" && pwd)"
R="${IRIX_WORK:-$(cd "$here/../.." && pwd)}"
S="${OSVIEW_SRC:-$HOME/Projects/irix-657m-src/eoe/cmd/osview}"
B="${N32_BUILD:-$R/build-n32/osview}"
mkdir -p "$B"
for f in getinfo main panel run readstruct; do
    "$here/n32cc.sh" -std=gnu89 -DTILES_TO_LPAGES -O1 -I"$S" -w \
        -include sys/types.h -include sys/pda.h -c "$S/$f.c" -o "$B/$f.o"
done
"$here/n32link.sh" "$B/osview" "$B"/getinfo.o "$B"/main.o "$B"/panel.o \
    "$B"/run.o "$B"/readstruct.o -lcurses
dest="$R/IRIX-TREE/usr/sbin/osview"
if [ -f "$dest" ] && [ ! -f "$dest.o32" ]; then
    cp -p "$dest" "$dest.o32"
fi
install -m755 "$B/osview" "$dest"
echo "installed N32 osview -> $dest"
