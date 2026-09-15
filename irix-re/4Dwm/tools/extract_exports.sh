#!/bin/sh
set -eu

if [ "$#" -ne 2 ]; then
    echo "usage: $0 /path/to/4Dwm output.tsv" >&2
    exit 2
fi

bin=$1
out=$2
nm_bin=${MIPS_NM:-mips-linux-gnu-nm}

printf 'address\tsize\tname\n' > "$out"
"$nm_bin" -D -S --defined-only "$bin" | awk '$3 ~ /^[Tt]$/ { print $1 "\t" $2 "\t" $4 }' >> "$out"
