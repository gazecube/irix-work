#!/bin/sh
set -eu

PIN=822f4fcb99524a2fa03747acc4c5946eeb63ea3c
BASE="https://raw.githubusercontent.com/calmsacibis995/irix-657m-src/$PIN/stand/arcs/include/fonts"
OUT="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/sgisrc/include/fonts"

mkdir -p "$OUT"

fetch_one() {
    name=$1
    expect=$2
    tmp="$OUT/$name.tmp"
    dst="$OUT/$name"

    echo "fetching original IRIX font: $name"
    curl -fL "$BASE/$name" -o "$tmp"
    got=$(git hash-object "$tmp")
    if [ "$got" != "$expect" ]; then
        echo "error: $name blob mismatch: got $got expected $expect" >&2
        rm -f "$tmp"
        exit 1
    fi
    mv "$tmp" "$dst"
}

fetch_one scrB18.h 7f86baed71fd15a7c86f5df1e3188ec38d90b058
fetch_one special.h 8ed27775ee97ef974b5cff054368c9c366950e7f
fetch_one ncenB18.h 27adb54d1b8ff64e3ea153f6432a19593c63dec9
fetch_one helvR10.h b3bc7fd32082c62e7df5bcdaf38dc25a4cbbf136

echo "original IRIX font headers staged in $OUT"
