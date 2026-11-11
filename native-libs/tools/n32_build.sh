#!/bin/sh
# Lift the IRIX N32 libXt, libXm, libC and libCsup to i386 and link them with the libc layer; then a program.
#   build32.sh            relink from the existing lift output
#   build32.sh lift       lift and compile first
# All paths literal.
set -e
cd /home/nekron/opt/claude-work/n32
CF="-m32 -msse2 -mfpmath=sse -malign-double -std=gnu11 -O1 -fPIC -fno-strict-aliasing -fwrapv -w -g1"
IRIXC="-m32 -std=gnu89 -fpermissive -funsigned-char -fcommon -fno-strict-aliasing -fwrapv -fno-builtin -fPIC -O1 -w"
LF="-Wl,-z,notext -Wl,-z,noexecstack -Wl,--no-undefined -Wl,-rpath,\$ORIGIN"
T=/home/nekron/Projects/irix-work/irix-re/recomp/tools
L=/home/nekron/Projects/irix-work/IRIX-TREE/usr/lib32
mkdir -p out
gcc $IRIXC -I/home/nekron/opt/claude-work/satconfig-native/libgen -c /home/nekron/opt/claude-work/satconfig-native/libgen/regcmp.c -o out/regcmp.o
gcc $IRIXC -I/home/nekron/opt/claude-work/satconfig-native/libgen -c /home/nekron/opt/claude-work/satconfig-native/libgen/regex.c -o out/regex.o
gcc -m32 -O1 -w -fPIC -shared -Wl,-soname,libc.so.1 -o out/libc.so.1 /home/nekron/Projects/irix-work/native-libs/nativemem/shim/irixlibc64.c out/regcmp.o out/regex.o -L/usr/lib -lX11 -ldl -lm
nm -D --defined-only out/libc.so.1 | awk '$3 ~ /^irix_/ {print substr($3,6)}' | sort > out/shimdefs.txt
if [ "$1" = lift ]; then
	HL="-h stdio.h -h stdlib.h -h string.h -h strings.h -h unistd.h -h fcntl.h -h errno.h -h signal.h -h setjmp.h -h time.h -h ctype.h -h locale.h -h math.h -h pwd.h -h dirent.h -h netdb.h -h dlfcn.h -h wchar.h -h sys/stat.h -h sys/time.h -h sys/select.h -h sys/socket.h -h sys/ioctl.h -h sys/mman.h -h X11/Xlib.h -h X11/Xutil.h -h X11/Xresource.h -h X11/Xatom.h -h X11/Xlibint.h -h X11/Xlocale.h -h X11/extensions/shape.h"
	XI=/home/nekron/Projects/irix-work/native-libs/build/xm-sgi-inc
	python3 $T/n64_types.py $L/libXt.so Xt.types.json --m32 -h X11/IntrinsicP.h -h X11/CoreP.h -h X11/CompositeP.h -h X11/ConstrainP.h -h X11/ObjectP.h -h X11/RectObjP.h -h X11/ShellP.h -h X11/VendorP.h -h X11/HookObjI.h | head -1
	python3 $T/n64_types.py $L/libXm.so.1 Xm.types.json --m32 -I $XI -h X11/IntrinsicP.h -h X11/ShellP.h -h X11/VendorP.h $(cd $XI && ls Xm/*P.h | sed 's/^/-h /') --like SgvendorShellClassRec=vendorShellClassRec | head -1
	echo '{}' > e.json
	python3 $T/n64_protos.py $L/libXt.so Xt.protos.json --m32 $HL | head -1
	python3 $T/n64_protos.py $L/libXm.so.1 Xm.protos.json --m32 $HL | head -1
	python3 $T/n64_protos.py $L/libC.so.2 C.protos.json --m32 $HL | head -1
	python3 $T/lift_n64.py $L/libXt.so Xt --name Xt --hook XtInitializeWidgetClass=lift_native_class --types Xt.types.json --protos Xt.protos.json --wrapped out/shimdefs.txt | sed -n 1,3p
	python3 $T/lift_n64.py $L/libXm.so.1 Xm --name Xm --lifted $L/libXt.so --types Xm.types.json --protos Xm.protos.json --wrapped out/shimdefs.txt | sed -n 1,3p
	python3 $T/lift_n64.py $L/libC.so.2 C --name C --types e.json --protos C.protos.json --wrapped out/shimdefs.txt | sed -n 2,3p
	python3 $T/lift_n64.py $L/libCsup.so Csup --name Csup --types e.json --protos e.json --wrapped out/shimdefs.txt | sed -n 2,3p
	for l in Xt Xm C Csup; do
		gcc $CF -c $l/code.c -o $l/code.o
		gcc -m32 -c $l/data.s -o $l/data.o
		ld -m elf_i386 -r $l/code.o $l/data.o -o $l/all.o
	done
fi
for l in Xt Xm C Csup; do
	nm -u $l/all.o | awk '{print $2}' | sort > $l/undef.txt
	comm -12 $l/undef.txt out/shimdefs.txt | awk '{print $1, "irix_" $1}' > $l/libc.map
	objcopy --redefine-syms=$l/libc.map $l/all.o $l/all_renamed.o
done
G="gcc -m32 -shared -Lout -L/usr/lib"
$G -Wl,-soname,libXt.so -o out/libXt.so Xt/all_renamed.o -l:libc.so.1 -lX11 -lm -lpthread $LF
$G -Wl,-soname,libXm.so.1 -o out/libXm.so.1 Xm/all_renamed.o -l:libXt.so -l:libc.so.1 -lX11 -lXext -lm $LF
$G -Wl,-soname,libC.so.2 -o out/libC.so.2 C/all_renamed.o -l:libc.so.1 -lm $LF
$G -Wl,-soname,libCsup.so -o out/libCsup.so Csup/all_renamed.o -l:libc.so.1 -lm $LF
ls -la out/*.so*
