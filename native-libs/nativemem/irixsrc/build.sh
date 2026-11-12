#!/bin/sh
# build.sh 32|64 OUTDIR : compile the IRIX libc sources listed here for the libc layer (paths literal)
set -e
S=/home/nekron/Projects/irix-657m-src
C=/home/nekron/Projects/irix-work/native-libs/nativemem/irixsrc
mkdir -p $C/src $C/include/sys $2
cp $S/irix/lib/libc/inc/synonyms.h $C/include/synonyms.h
cp $S/irix/lib/libc/src/gen/getinvent.c $S/irix/lib/libc/src/gen/scaninvent.c $C/src/
cp $S/eoe/include/invent.h $C/include/invent.h
cp $S/irix/kern/sys/invent.h $C/include/sys/invent.h
for f in getinvent scaninvent; do
	gcc -m$1 -std=gnu89 -fPIC -O1 -w -fno-strict-aliasing -I$C/include -include $C/include/irix_types.h -Dmalloc=irix_malloc -Dcalloc=irix_calloc -Dfree=irix_free -c $C/src/$f.c -o $2/$f.o
done
