#!/bin/sh
set -eu

usage() {
    cat <<'EOF'
usage: tools/disassemble-clogin.sh <clogin-binary> <mips-binutils-root> [symbol]

Examples:
  tools/disassemble-clogin.sh evidence/original/clogin /opt/mips-binutils
  tools/disassemble-clogin.sh evidence/original/clogin /opt/mips-binutils createIconList__7pandoraGv

The binutils root is expected to contain:
  usr/bin/mips-linux-gnu-objdump
  usr/bin/mips-linux-gnu-readelf

If the package also contains private libbfd/libopcodes builds under
usr/lib/x86_64-linux-gnu, this script adds that directory to LD_LIBRARY_PATH.
EOF
}

if [ "$#" -lt 2 ] || [ "$#" -gt 3 ]; then
    usage >&2
    exit 2
fi

binary=$1
root=$2
symbol=${3-}
objdump="$root/usr/bin/mips-linux-gnu-objdump"
readelf="$root/usr/bin/mips-linux-gnu-readelf"
libdir="$root/usr/lib/x86_64-linux-gnu"

[ -f "$binary" ] || { echo "missing binary: $binary" >&2; exit 1; }
[ -x "$objdump" ] || { echo "missing objdump: $objdump" >&2; exit 1; }
[ -x "$readelf" ] || { echo "missing readelf: $readelf" >&2; exit 1; }

if [ -d "$libdir" ]; then
    LD_LIBRARY_PATH="$libdir${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
    export LD_LIBRARY_PATH
fi

if [ -z "$symbol" ]; then
    echo "== file / ELF header =="
    "$readelf" -h "$binary"
    echo
    echo "== Clogin reconstruction symbols =="
    "$objdump" -T "$binary" | grep -E 'createIconList|addUser|alreadyKnown|findUserPicture|checkDirectory|getWhereHome|getWhereIcons|getHowDisplay|PwdNameList|IsInIgnoreList|checkAutoLogin' || true
    exit 0
fi

line=$($objdump -T "$binary" | awk -v want="$symbol" '$NF == want { print $1, $(NF-1); exit }')
[ -n "$line" ] || { echo "symbol not found in dynamic table: $symbol" >&2; exit 1; }

set -- $line
start_hex=$1
size_hex=$2

start=$((0x$start_hex))
size=$((0x$size_hex))
stop=$((start + size))

printf '== %s: 0x%x..0x%x ==\n' "$symbol" "$start" "$stop"
"$objdump" -dr --start-address="$start" --stop-address="$stop" "$binary"
