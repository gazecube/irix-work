#!/bin/bash
# Install mediad-linux into the native IRIX paths.  Dry run unless --apply.
#   install-mediad.sh [--apply | --uninstall] [--dest DIR]
# Files (as on IRIX): /usr/etc/mediad, /usr/lib/devicelib/*.so, the ORIGINAL
# /etc/init.d/mediad, /etc/config/mediad{,.options}; plus /etc/chkconfig (host
# shim), the irix-devices namespace tool and two systemd units.
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/.." && pwd)
tree=${IRIX_TREE:-$root/IRIX-TREE}
dest=/; mode=dry
while [ $# -gt 0 ]; do
    case $1 in
    --apply) mode=apply ;;
    --uninstall) mode=uninstall ;;
    --dest) shift; dest=$1 ;;
    *) sed -n '2,8p' "$0"; exit 2 ;;
    esac; shift
done
dest=${dest%/}
manifest=$dest/usr/lib/mediad-linux.manifest
b=$here/mediad-linux/build
# dest-relative path | source | mode
items=(
 "usr/etc/mediad|$b/mediad-linux|755"
 "usr/lib/devicelib/dev_linux.so|$b/dev_linux.so|755"
 "usr/lib/devicelib/fmt_iso.so|$b/fmt_iso.so|755"
 "usr/lib/devicelib/fmt_udf.so|$b/fmt_udf.so|755"
 "etc/init.d/mediad|$tree/etc/init.d/mediad|755"
 "etc/config/mediad|$tree/etc/config/mediad|644"
 "etc/config/mediad.options|$tree/etc/config/mediad.options|644"
 "etc/chkconfig|$here/irix-tree/chkconfig|755"
 "usr/lib/irix-devices/irix-devices|$here/irix-device-ecosystem/bin/irix-devices|755"
 "etc/systemd/system/irix-devices.service|$here/systemd/irix-devices.service|644"
 "etc/systemd/system/mediad.service|$here/systemd/mediad.service|644"
)
SUDO=""; [ "$(id -u)" != 0 ] && SUDO="sudo -n"
run() { if [ $mode = apply ] || [ $mode = uninstall ]; then $SUDO "$@"; else echo "  would: $*"; fi; }
owned() { [ -f "$manifest" ] && grep -qxF "$1" "$manifest"; }
if [ $mode = uninstall ]; then
    [ -f "$manifest" ] || { echo "nothing installed"; exit 0; }
    $SUDO systemctl disable --now mediad.service irix-devices.service 2>/dev/null
    while read -r f; do [ -n "$f" ] && $SUDO rm -f "$dest/$f" && echo "removed /$f"; done < "$manifest"
    $SUDO rm -f "$manifest"; $SUDO systemctl daemon-reload 2>/dev/null; exit 0
fi
new=0; same=0; upd=0; conflict=0; newmanifest=()
for it in "${items[@]}"; do
    IFS='|' read -r rel src m <<<"$it"
    [ -f "$src" ] || { echo "MISSING SOURCE: $src"; conflict=$((conflict+1)); continue; }
    d="$dest/$rel"
    if [ -e "$d" ] || [ -L "$d" ]; then
        if cmp -s "$src" "$d"; then same=$((same+1)); newmanifest+=("$rel"); continue; fi
        if owned "$rel"; then upd=$((upd+1)); echo "update  /$rel"
        else conflict=$((conflict+1)); echo "CONFLICT /$rel exists and is not ours; left alone"; continue; fi
    else new=$((new+1)); echo "new     /$rel"; fi
    run /usr/bin/install -D -m "$m" "$src" "$d" && newmanifest+=("$rel")
done
echo "new=$new updated=$upd identical=$same conflicts=$conflict"
if [ $mode = apply ]; then
    printf '%s\n' "${newmanifest[@]}" | $SUDO tee "$manifest" >/dev/null
    $SUDO mkdir -p /var/lib/irix-devices
    $SUDO systemctl daemon-reload
    echo "installed.  enable with: sudo systemctl enable --now irix-devices mediad"
else
    echo "(dry run; use --apply)"
fi
