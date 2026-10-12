#!/bin/sh
# clang as an IRIX N32 (MIPSpro -n32 -mips3) compiler.
#   IRIX_WORK        repo root (default: two levels above this script)
#   IRIX_KERNEL_SYS  irix/kern/sys of the 6.5.7m source (kernel-visible headers)
#   N32_OVERLAY      where the overlay include dir is created
here="$(cd "$(dirname "$0")" && pwd)"
R="${IRIX_WORK:-$(cd "$here/../.." && pwd)}"
K="${IRIX_KERNEL_SYS:-$HOME/Projects/irix-657m-src/irix/kern/sys}"
OVL="${N32_OVERLAY:-$R/build-n32/ovl}"
if [ ! -d "$OVL/sys" ]; then
    mkdir -p "$OVL/sys"
    for h in ksa.h sysinfo.h pda.h sysmp.h sysget.h tile.h; do
        [ -f "$K/$h" ] && ln -sf "$K/$h" "$OVL/sys/$h"
    done
fi
exec clang --target=mips64-unknown-linux-gnuabin32 -mabi=n32 -march=mips3 -fPIC \
  -fno-strict-aliasing -fno-stack-protector -nostdinc \
  -isystem "$OVL" -D_KMEMUSER=1 \
  -isystem "$R/IRIX-TREE/usr/include" -isystem "$R/holyshit/usr/include" \
  -D__sgi=1 -Dsgi=1 -D_MIPSEB=1 -D__MIPSEB=1 -D_LANGUAGE_C=1 -D__LANGUAGE_C \
  -D_MIPS_SIM=_ABIN32 -D_MIPS_SZINT=32 -D_MIPS_SZLONG=32 -D_MIPS_SZPTR=32 \
  -D_MIPS_FPSET=32 -D"__long_long=long long" -D__EXTENSIONS__ -D_SVR4_SOURCE=1 \
  -D_SGI_SOURCE=1 -Wno-implicit-function-declaration -Wno-int-conversion \
  -Wno-incompatible-pointer-types -Wno-error "$@"
