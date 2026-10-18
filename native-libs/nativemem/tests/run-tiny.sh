#!/bin/bash
set -eu
here="$(cd "$(dirname "$0")/.." && pwd)"; N="$here/.."
W="${1:-/tmp/nm-tiny}"; rm -rf "$W"; mkdir -p "$W"; cd "$W"
"$N/n32cc.sh" -c "$here/tests/tiny.c" -o tiny.o
llvm-ar rcs libtiny.a tiny.o
ld.lld -m elf32btsmipn32 -shared --unresolved-symbols=ignore-all -o libtiny_n32.so tiny.o
python3 "$here/tools/nm_build.py" libtiny.a libtiny_n32.so "$W/out"
python3 "$here/tools/nm_pack.py" "$W/out/gen" "$W/libtiny_native.so"
gcc -m32 -w -o tiny-native-test "$here/tests/tiny-native-test.c" -L"$W" -l:libtiny_native.so -Wl,-rpath,"$W"
./tiny-native-test
