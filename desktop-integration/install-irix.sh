#!/bin/sh
# Stage the IRIX desktop into one self-contained prefix.
#
#   <prefix>/bin/irix-session        session launcher (finds the rest by itself)
#   <prefix>/bin/irix-guest-exec     generic runner for original IRIX images
#   <prefix>/lib/libirixre.so*       shared runtime
#   <prefix>/root/                   native x86-64 runners, guest path layout
#   <prefix>/tree/                   original IRIX runtime tree (guest files)
#   <prefix>/libexec/irix-desktop-session   display-manager entry point
#
# Nothing outside <prefix> is touched, except with --xsession, which installs
# the display-manager entry (needs root).  The build-time dev tree
# (IRIX_DEV_TREE, headers and link libraries) is not needed at run time.
#
# Usage: install-irix.sh [--prefix DIR] [--build DIR] [--tree DIR]
#                        [--link] [--xsession]
#   --prefix DIR   install location (default /opt/irix)
#   --build DIR    CMake build directory (default <repo>/build-irix-re)
#   --tree DIR     IRIX runtime tree to copy (default <repo>/IRIX-TREE)
#   --link         hard-link instead of copying (same filesystem only)
#   --xsession     also install /usr/share/xsessions/irix-desktop.desktop
set -eu

here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/.." && pwd)"
prefix=/opt/irix
build="$repo/build-irix-re"
tree="$repo/IRIX-TREE"
link=0
xsession=0

while [ $# -gt 0 ]; do
    case "$1" in
        --prefix) prefix=$2; shift 2 ;;
        --build) build=$2; shift 2 ;;
        --tree) tree=$2; shift 2 ;;
        --link) link=1; shift ;;
        --xsession) xsession=1; shift ;;
        -h|--help) sed -n '2,24p' "$0"; exit 0 ;;
        *) echo "install-irix: unknown option $1" >&2; exit 2 ;;
    esac
done

recomp="$build/recomp"
for need in "$recomp/root" "$recomp/libirixre.so.1" "$recomp/irix-guest-exec" \
            "$recomp/quick/irix-session" "$tree/usr/lib32"; do
    if [ ! -e "$need" ]; then
        echo "install-irix: missing $need (build first)" >&2
        exit 1
    fi
done
if ! mkdir -p "$prefix" 2>/dev/null || [ ! -w "$prefix" ]; then
    echo "install-irix: cannot write $prefix; try:" >&2
    echo "    sudo install -d -o $(id -un) -g $(id -gn) $prefix" >&2
    exit 1
fi

copy_tree()
{
    # copy_tree SRC DST: preserve modes and symlinks, merge into DST.
    if [ "$link" = 1 ]; then
        mkdir -p "$2"
        cp -al "$1/." "$2/"
    elif command -v rsync >/dev/null 2>&1; then
        rsync -a "$1/" "$2/"
    else
        mkdir -p "$2"
        cp -a "$1/." "$2/"
    fi
}

echo "install-irix: runners   -> $prefix/root"
copy_tree "$recomp/root" "$prefix/root"
echo "install-irix: IRIX tree -> $prefix/tree"
copy_tree "$tree" "$prefix/tree"

mkdir -p "$prefix/bin" "$prefix/lib" "$prefix/libexec"
for f in "$recomp"/libirixre.so*; do
    cp -a "$f" "$prefix/lib/"
done
install -m755 "$recomp/irix-guest-exec" "$prefix/bin/irix-guest-exec"
install -m755 "$recomp/quick/irix-session" "$prefix/bin/irix-session"
install -m755 "$here/xsession/irix-claude" "$prefix/bin/irix-claude"
install -m755 "$here/irix-run" "$prefix/bin/irix-run"

cat >"$prefix/libexec/irix-desktop-session" <<EOF
#!/bin/sh
# Display-manager entry point for the installed IRIX desktop.
log="\${XDG_CACHE_HOME:-\$HOME/.cache}/irix-session.log"
mkdir -p "\$(dirname "\$log")"
exec "$prefix/bin/irix-session" >>"\$log" 2>&1
EOF
chmod 755 "$prefix/libexec/irix-desktop-session"

sed "s|/usr/local/libexec/irix-desktop-session|$prefix/libexec/irix-desktop-session|g" \
    "$here/xsession/irix-desktop.desktop" >"$prefix/irix-desktop.desktop"

if [ "$xsession" = 1 ]; then
    echo "install-irix: installing /usr/share/xsessions/irix-desktop.desktop"
    sudo install -m644 "$prefix/irix-desktop.desktop" \
        /usr/share/xsessions/irix-desktop.desktop
fi

cat <<EOF

installed to $prefix
  run inside X:     $prefix/bin/irix-session        (DISPLAY must be set)
  login manager:    sudo install -m644 $prefix/irix-desktop.desktop /usr/share/xsessions/
  Toolchest entry:  f.exec.sh "$prefix/bin/irix-claude"

Everything is located relative to the launcher; IRIX_RECOMP_ROOT and
IRIX_RUNTIME_TREE in the environment still override.
EOF
