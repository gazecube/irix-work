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
mkdir -p $T/usr/include/make $T/usr/bin $T/usr/sbin $T/etc $T/usr/lib $T/usr/lib32 $T/usr/lib64
cp $S/eoe/include/makerules/* $T/usr/include/make/
cp $S/irix/include/make/*defs $T/usr/include/make/
cp $S/eoe/cmd/pmake/lib/mk/*.mk $T/usr/include/make/
for t in cc CC ld; do ln -sf $U/tools/irixcc.py $T/usr/bin/$t; done
ln -sf $U/tools/irixinstall.py $T/etc/install
for t in ar nm size strip lorder bison flex; do ln -sf /usr/bin/$t $T/usr/bin/$t; done
ln -sf /usr/bin/m4 $T/usr/bin/gnum4
printf '#!/bin/sh\n# yacc: bison -y; the IRIX -P parser skeleton option is dropped\nwhile [ $# -gt 0 ]; do case "$1" in -P) shift 2;; *) break;; esac; done\nexec bison -y "$@"\n' > $T/usr/bin/yacc
printf '#!/bin/sh\n# lex: flex -l; the IRIX -l skeleton argument is dropped\nwhile [ $# -gt 0 ]; do case "$1" in -l) shift 2;; *) break;; esac; done\nexec flex -l "$@"\n' > $T/usr/bin/lex
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
$U/compat/fetch.sh
echo "toolroot ready"
