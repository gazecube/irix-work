#!/bin/sh
# Install the Motif VLC front end: /opt/vlcmotif/bin/{vlcmotif,vlcd} and the launcher /usr/local/bin/vlcmotif.
# Run as root: sudo /home/nekron/Projects/irix-work/vlc-motif/install.sh
set -e
SRC=$(cd "$(dirname "$0")" && pwd)
D=/opt/vlcmotif
[ -x "$SRC/vlcmotif" ] && [ -x "$SRC/vlcd" ] || { echo "build first: $SRC/build.sh" >&2; exit 1; }
mkdir -p "$D/bin"
install -m 755 "$SRC/vlcmotif" "$D/bin/vlcmotif"
install -m 755 "$SRC/vlcd" "$D/bin/vlcd"
# an existing launcher may carry local edits: keep it, and put the stock one beside it
if [ -e /usr/local/bin/vlcmotif ] && ! cmp -s "$SRC/vlcmotif.sh" /usr/local/bin/vlcmotif; then
    install -m 755 "$SRC/vlcmotif.sh" /usr/local/bin/vlcmotif.stock
    echo "kept your /usr/local/bin/vlcmotif (stock launcher saved as vlcmotif.stock)"
else
    install -m 755 "$SRC/vlcmotif.sh" /usr/local/bin/vlcmotif
    echo "installed: /usr/local/bin/vlcmotif"
fi
