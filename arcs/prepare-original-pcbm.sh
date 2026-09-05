#!/bin/sh
set -eu

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
OUT="$HERE/sgisrc/include/pcbm"
BASE="https://raw.githubusercontent.com/calmsacibis995/irix-657m-src/main/stand/arcs/include/pcbm"

mkdir -p "$OUT"

for name in sysstart install diags recover console; do
    echo "staging original PROM icon: ${name}.h"
    curl -fL "$BASE/${name}.h" -o "$OUT/${name}.h"
done
