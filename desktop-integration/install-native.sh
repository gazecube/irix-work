#!/bin/bash
set -u
here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/.." && pwd)"
SRC="$repo/build-irix-re/recomp/root"
DEST=/
MANIFEST=usr/lib/irix-recomp.manifest
APPLY=0
while [ $# -gt 0 ]; do
    case "$1" in
        --apply) APPLY=1 ;;
        --src) SRC="$2"; shift ;;
        --dest) DEST="$2"; shift ;;
        --manifest) MANIFEST="$2"; shift ;;
        -h|--help) echo "usage: $0 [--apply] [--src DIR] [--dest DIR] [--manifest FILE]  (see README.install)"; exit 0 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
    shift
done
DEST="${DEST%/}"
case "$MANIFEST" in /*) ;; *) MANIFEST="$DEST/$MANIFEST" ;; esac
[ -d "$SRC" ] || { echo "no source tree: $SRC" >&2; exit 1; }

EXCLUDE_PREFIXES=(
    /sbin/sh /bin/sh /usr/bin/sh /usr/sbin/sh
    /usr/local/bin/ksh /usr/bin/ksh
    /usr/lib32/libXt.so /usr/lib32/libXext.so /usr/lib32/libXi.so
    /usr/lib32/libXmu.so /usr/lib32/libGLU.so
    /usr/include/X11/bitmaps/xm_
)
excluded() {
    local p="$1" x
    for x in "${EXCLUDE_PREFIXES[@]}"; do
        case "$p" in "$x"*) return 0 ;; esac
    done
    return 1
}

SUDO=""
if [ "$(id -u)" -ne 0 ] && [ "$APPLY" = 1 ]; then
    SUDO="sudo -n"
    $SUDO true || { echo "need passwordless sudo (sudo -n)" >&2; exit 1; }
fi

declare -A ours
if [ -r "$MANIFEST" ]; then
    while IFS= read -r l; do [ -n "$l" ] && ours["$l"]=1; done < "$MANIFEST"
fi

new=0; same=0; upd=0; conflict=0; skip=0; mapped=0
declare -a installed
while IFS= read -r -d '' f; do
    rel="/${f#"$SRC"/}"
    if excluded "$rel"; then skip=$((skip+1)); continue; fi
    pdir="$(dirname "$rel")"
    rdir="$(realpath -m -- "$DEST$pdir")"
    target="$rdir/$(basename "$rel")"
    [ "$rdir" != "$DEST$pdir" ] && mapped=$((mapped+1))
    if [ -L "$f" ]; then
        if [ -e "$target" ] || [ -L "$target" ]; then same=$((same+1)); continue; fi
        echo "NEW(link) $target -> $(readlink "$f")"
        new=$((new+1))
        if [ "$APPLY" = 1 ]; then
            $SUDO install -d -o root -g root -m 755 "$rdir"
            $SUDO ln -s "$(readlink "$f")" "$target"
        fi
        installed+=("$rel")
        continue
    fi
    if [ -L "$target" ]; then
        echo "CONFLICT (destination is a symlink, left alone): $target"
        conflict=$((conflict+1)); continue
    fi
    if [ -e "$target" ]; then
        if cmp -s "$f" "$target"; then
            same=$((same+1)); installed+=("$rel"); continue
        fi
        if [ -n "${ours[$rel]:-}" ] || [ -n "${ours[${target#"$DEST"}]:-}" ]; then
            echo "UPDATE $target"
            upd=$((upd+1))
        else
            echo "CONFLICT (differs, not ours, skipped): $target"
            conflict=$((conflict+1)); continue
        fi
    else
        [ "${VERBOSE:-0}" = 1 ] && echo "NEW $target"
        new=$((new+1))
    fi
    if [ "$APPLY" = 1 ]; then
        mode=$(stat -c '%a' "$f")
        $SUDO install -d -o root -g root -m 755 "$rdir"
        $SUDO install -o root -g root -m "$mode" -- "$f" "$target"
    fi
    installed+=("$rel")
done < <(find "$SRC" \( -type f -o -type l \) -print0 | sort -z)

echo
echo "new=$new updated=$upd identical=$same conflicts=$conflict excluded=$skip via-symlinked-parent=$mapped"
if [ "$APPLY" = 1 ]; then
    tmp="$(mktemp)"
    printf '%s\n' "${installed[@]}" | sort -u > "$tmp"
    $SUDO install -d -o root -g root -m 755 "$(dirname "$MANIFEST")"
    $SUDO install -o root -g root -m 644 "$tmp" "$MANIFEST"
    rm -f "$tmp"
    echo "manifest written: $MANIFEST ($(wc -l < "$MANIFEST") paths)"
else
    echo "dry run only; re-run with --apply to install (VERBOSE=1 lists new files)."
fi
