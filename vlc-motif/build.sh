#!/bin/sh
# build vlcmotif (32-bit, SGI Motif headers, lifted libXm) and, when libvlc is installed, vlcd (64-bit)
set -e
cd /home/nekron/Projects/irix-work/vlc-motif
gcc -m32 -msse2 -mfpmath=sse -ffp-contract=off -std=gnu11 -O2 -Wall -Wno-unused-variable -Wno-unused-but-set-variable \
    -I/home/nekron/Projects/irix-work/native-libs/build/xm-sgi-inc \
    -I/home/nekron/Projects/irix-work/irix-re/libSgm/include \
    -o vlcmotif vlcmotif.c /home/nekron/Projects/irix-work/irix-re/libSgm/Dial.c \
    -L/opt/netscape/lib32 -Wl,--allow-shlib-undefined -Wl,-rpath-link,/opt/netscape/lib32 -l:libXm_lifted.so \
    -L/usr/lib -lXt -lXext -lX11 -lSM -lICE -lm
ls -la vlcmotif
if [ -f /usr/include/vlc/vlc.h ] && [ -f vlcd.c ]; then
    gcc -O2 -Wall -o vlcd vlcd.c $(pkg-config --cflags --libs libvlc)
    ls -la vlcd
else
    echo "(vlcd not built: libvlc headers or vlcd.c missing)"
fi
