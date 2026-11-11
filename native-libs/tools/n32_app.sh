#!/bin/sh
# app32.sh PATH NAME : lift the IRIX N32 program PATH and link it as the i386 executable NAME/NAME
set -e
cd /home/nekron/opt/claude-work/n32
T=/home/nekron/Projects/irix-work/irix-re/recomp/tools
L=/home/nekron/Projects/irix-work/IRIX-TREE/usr/lib32
CF="-m32 -msse2 -mfpmath=sse -malign-double -std=gnu11 -O1 -fPIC -fno-strict-aliasing -fwrapv -w -g1"
HL="-h stdio.h -h stdlib.h -h string.h -h strings.h -h unistd.h -h fcntl.h -h errno.h -h signal.h -h time.h -h ctype.h -h math.h -h pwd.h -h dirent.h -h sys/stat.h -h sys/time.h -h sys/wait.h -h X11/Xlib.h -h X11/Xutil.h -h X11/Xresource.h -h X11/Xatom.h -h X11/extensions/shape.h"
mkdir -p $2
echo '{}' > e.json
python3 $T/n64_protos.py $1 $2/protos.json --m32 $HL | head -1
python3 $T/lift_n64.py $1 $2 --name app --lifted $L/libXt.so --lifted $L/libXm.so.1 --lifted $L/libC.so.2 --lifted $L/libCsup.so --types e.json --protos $2/protos.json --wrapped out/shimdefs.txt | sed -n 2,6p
gcc $CF -c $2/code.c -o $2/code.o
gcc -m32 -c $2/data.s -o $2/data.o
ld -m elf_i386 -r $2/code.o $2/data.o -o $2/all.o
nm -u $2/all.o | awk '{print $2}' | sort > $2/undef.txt
comm -12 $2/undef.txt out/shimdefs.txt | awk '{print $1, "irix_" $1}' > $2/libc.map
objcopy --redefine-syms=$2/libc.map $2/all.o $2/all_renamed.o
{ echo 'extern int __Argc; extern char **__Argv;'; nm $2/all.o | grep -q ' T __istart$' && echo 'extern unsigned long long lw___istart(void);'
  echo 'static void __attribute__((constructor(65000))) irix_start(int argc, char **argv, char **envp) { __Argc = argc; __Argv = argv;'
  nm $2/all.o | grep -q ' T __istart$' && echo '    lw___istart();'; echo '}'; } > $2/start.c
gcc -m32 -fPIC -w -c $2/start.c -o $2/start.o
gcc -m32 -o $2/$2 $2/all_renamed.o $2/start.o -Lout -L/usr/lib -l:libXm.so.1 -l:libXt.so -l:libC.so.2 -l:libCsup.so -l:libc.so.1 -lX11 -lXext -lm -Wl,-z,notext -Wl,-z,noexecstack -Wl,-rpath,/home/nekron/opt/claude-work/n32/out 2>&1 | grep -o "undefined reference to .*" | sort -u
ls -la $2/$2
