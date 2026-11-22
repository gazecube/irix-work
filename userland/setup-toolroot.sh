#!/bin/sh
# Make the tool root that IRIX Makefiles build with (git-ignored; rerun after changes).  Paths literal.
#   toolroot/usr/include/make   SGI's make rules, copied from the source tree (eoe makerules, product defs,
#                               pmake's system.mk)
#   toolroot/usr/bin, sbin, etc the tools the rules name: cc/CC/ld -> tools/irixcc.py, install ->
#                               tools/irixinstall.py, yacc/lex -> bison/flex, the rest host tools or no-ops
#   compat/lib/32, compat/lib/64 libirixcompat.a
set -e
S=/home/nekron/Projects/irix-657m-src
U=/home/nekron/Projects/irix-work/userland
T=/home/nekron/Projects/irix-work/userland/toolroot
/home/nekron/Projects/irix-work/userland/compat/fetch.sh
mkdir -p $T/usr/include/make $T/usr/bin $T/usr/sbin $T/etc $T/usr/lib $T/usr/lib32 $T/usr/lib64
cp $S/eoe/include/makerules/* $T/usr/include/make/
cp $S/irix/include/make/*defs $T/usr/include/make/
cp $S/eoe/cmd/pmake/lib/mk/*.mk $T/usr/include/make/
for t in cc CC ld; do ln -sf $U/tools/irixcc.py $T/usr/bin/$t; done
ln -sf $U/tools/irixinstall.py $T/etc/install
for t in nm size strip lorder bison flex; do ln -sf /usr/bin/$t $T/usr/bin/$t; done
ln -sf /usr/bin/m4 $T/usr/bin/gnum4
printf '#!/bin/sh\n# ar: SGI'"'"'s key letters (crl, ccrl) without l, which GNU ar reads as "record dependencies" and which then\n# swallows the next argument\nk=$(printf %%s "$1" | tr -d l)\nshift\nexec /usr/bin/ar "$k" "$@"\n' > $T/usr/bin/ar
printf '#!/bin/sh\n# hostcc: tools the build runs on this machine (HOST_CC), old C as for the targets\nexec gcc -std=gnu89 -w -fcommon -fno-strict-aliasing -include /home/nekron/Projects/irix-work/userland/compat/include/prelude.h -I/home/nekron/Projects/irix-work/userland/compat/include -I/home/nekron/Projects/irix-work/userland/compat/irix-include "$@" -L/home/nekron/Projects/irix-work/userland/compat/lib/64 -lirixc -lirixcompat\n' > $T/usr/bin/hostcc
# yacc: IRIX's own, once eoe/cmd/yacc and eoe/lib/liby (yaccpar) are built into the staging root; bison -y until then
printf '#!/bin/sh\n# yacc: IRIX'"'"'s yacc from the staging root when it is built, else bison -y (without IRIX'"'"'s -P skeleton option)\nR=/home/nekron/opt/claude-work/eoe/root\nif [ -x $R/usr/bin/yacc ] && [ -r $R/usr/lib/yaccpar ]; then exec $R/usr/bin/yacc "$@"; fi\nwhile [ $# -gt 0 ]; do case "$1" in -P) shift 2;; *) break;; esac; done\nexec bison -y "$@"\n' > $T/usr/bin/yacc
# lex: IRIX's own once eoe/cmd/lex and eoe/lib/libl (ncform) are built into the staging root; flex -l until then
printf '#!/bin/sh\n# lex: IRIX'"'"'s lex from the staging root when it is built, else flex -l (without IRIX'"'"'s -l skeleton argument)\nR=/home/nekron/opt/claude-work/eoe/root\nif [ -x $R/usr/bin/lex ] && [ -r $R/usr/lib/lex/ncform ]; then exec $R/usr/bin/lex "$@"; fi\nwhile [ $# -gt 0 ]; do case "$1" in -l) shift 2;; *) break;; esac; done\nexec flex -l "$@"\n' > $T/usr/bin/lex
printf '#!/bin/sh\n# toolrootsafe: runs its arguments\nexec "$@"\n' > $T/usr/sbin/toolrootsafe
printf '#!/bin/sh\n# cord: no procedure rearrangement on x86, the output is the input\n[ -n "$2" ] && cp "$1" "$2"\nexit 0\n' > $T/usr/bin/cord
ln -sf $U/tools/tlink.py $T/usr/sbin/tlink
for t in tag setmagic libspec; do printf '#!/bin/sh\n# %s: not needed for a native build\nexit 0\n' $t > $T/usr/sbin/$t; done
for t in lint mkshlib mkf2c; do printf '#!/bin/sh\n# %s: not available\nexit 0\n' $t > $T/usr/bin/$t; done
printf '#!/bin/sh\n# as: IRIX assembly is MIPS code\necho "as: MIPS assembly cannot be assembled for x86: $*" >&2\nexit 1\n' > $T/usr/bin/as
find $T/usr/bin $T/usr/sbin -maxdepth 1 -type f -exec chmod +x {} +   # (not the symlinks: they point at host tools)
ln -sf /home/nekron/opt/claude-work/eoe/bootstrap/pmake/pmake $T/usr/sbin/smake
for m in 32 64; do
	mkdir -p $U/compat/lib/$m
	gcc -m$m -O1 -w -fPIC -c $U/compat/lib/irix_k.c -o $U/compat/lib/$m/irix_k.o
	gcc -m$m -O1 -w -fPIC -c $U/compat/lib/irix_kcall.c -o $U/compat/lib/$m/irix_kcall.o
	rm -f $U/compat/lib/$m/libirixcompat.a
	ar rcs $U/compat/lib/$m/libirixcompat.a $U/compat/lib/$m/*.o
done
$U/build-irixc.sh
# build-host tools from the IRIX source that the build runs
$T/usr/bin/hostcc -O -idirafter /home/nekron/opt/claude-work/eoe/root/usr/include -o $T/usr/bin/mkmsgs $S/eoe/cmd/mkmsgs/mkmsgs.c
echo "toolroot ready"
