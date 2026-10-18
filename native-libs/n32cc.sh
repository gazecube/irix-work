#!/bin/sh
R="${IRIX_WORK:-$(cd "$(dirname "$0")/.." && pwd)}"
RES="$(clang -print-resource-dir)"
exec clang --target=mips64-unknown-linux-gnuabin32 -mabi=n32 -march=mips3 -fPIC -gdwarf-4 -O1 \
  -fno-strict-aliasing -fno-stack-protector -w -std=gnu89 \
  -nostdinc -D_VA_LIST_ -isystem "$RES/include" -Wno-return-mismatch \
  -isystem "$R/IRIX-TREE/usr/include" -isystem "$R/holyshit/usr/include" \
  -D__sgi=1 -Dsgi=1 -D_MIPSEB=1 -D__MIPSEB=1 -D_LANGUAGE_C=1 -D__LANGUAGE_C -D_MIPS_SIM=_ABIN32 \
  -D_MIPS_SZINT=32 -D_MIPS_SZLONG=32 -D_MIPS_SZPTR=32 -D_MIPS_FPSET=32 \
  -D"__long_long=long long" -D__EXTENSIONS__ -D_SVR4_SOURCE=1 -D_SGI_SOURCE=1 \
  -Wno-implicit-function-declaration -Wno-int-conversion -Wno-incompatible-pointer-types "$@"
