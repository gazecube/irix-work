#!/bin/sh
#
# Populate the runnable host desktop with the objects named by the original
# IRIX 6.5 /var/X11/xdm/DefaultIconList.
#
# Ground truth from desktop_eoe Xsession.dt:
#   FLtoPopDB < /var/X11/xdm/DefaultIconList |
#       bgicons -icondb $desktopdir/DefaultBackgroundIconDB
#
# The original path creates ImdRefModel references.  That reference-file
# encoding is still under reconstruction.  This helper deliberately uses
# POSIX symlinks only as a Linux host transport so the current ImdDirModel can
# enumerate the same target set.  It is not claimed to be the IRIX on-disk
# reference format and must disappear once ImdRefModel::createReference() is
# restored.
#
# -n mirrors FLtoPopDB's original "No file system checking" option: missing
# IRIX targets are represented by dangling host symlinks instead of skipped.

set -eu

no_check=0
if [ "${1-}" = "-n" ]; then
    no_check=1
    shift
fi

if [ "$#" -ne 0 ]; then
    echo "usage: $0 [-n]" >&2
    exit 2
fi

: "${HOME:?HOME is not set}"

background_dir=${SGI_DESKTOP_DIRECTORY:-"$HOME/Desktop"}
mkdir -p "$background_dir"

# Xsession.dt creates the dumpster on first login before bgicons runs.
mkdir -p "$HOME/dumpster"
: > "$HOME/dumpster/.dumpster"

# Keep this list byte-for-byte equivalent to the shipped DefaultIconList.
# Do not add Linux conveniences here.
cat <<'EOF' | while IFS= read -r source_entry
a:$HOME
b:$HOME/dumpster
c:/var/www/htdocs/WhatsNew/Welcome_to_SGI
d:/var/www/htdocs/Register/Register_To_Win
e:/usr/people/OutOfBox/StartSysTour
EOF
do
    source_entry=${source_entry#?:}

    case "$source_entry" in
        '\$HOME')
            target=$HOME
            ;;
        '\$HOME/'*)
            target=$HOME/${source_entry#\$HOME/}
            ;;
        *)
            target=$source_entry
            ;;
    esac

    if [ ! -e "$target" ] && [ ! -L "$target" ] && [ "$no_check" -eq 0 ]; then
        echo "iid-populate-default-desktop: skipping missing IRIX target: $target" >&2
        continue
    fi

    name=$(basename "$target")
    dest=$background_dir/$name

    if [ -e "$dest" ] || [ -L "$dest" ]; then
        echo "iid-populate-default-desktop: keeping existing $dest" >&2
        continue
    fi

    ln -s "$target" "$dest"
    echo "iid-populate-default-desktop: $dest -> $target" >&2
done
