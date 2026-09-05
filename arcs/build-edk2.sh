#!/bin/sh
set -eu

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
: "${WORKSPACE:?set WORKSPACE to your EDK II workspace}"
TOOLCHAIN=${TOOLCHAIN:-GCC}
TARGET=${TARGET:-DEBUG}
PYTHON_COMMAND=${PYTHON_COMMAND:-python3}
export PYTHON_COMMAND

if [ ! -f "$HERE/vendor/sgibootscreen-src/src/ip19.c" ]; then
    echo "error: initialize submodules first:" >&2
    echo "  git submodule update --init --recursive" >&2
    exit 1
fi

"$HERE/prepare-original-fonts.sh"

rm -rf "$WORKSPACE/ArcsPkg"
cp -a "$HERE" "$WORKSPACE/ArcsPkg"

cd "$WORKSPACE"
# edksetup.sh probes several optional environment variables and is not
# nounset-safe. Temporarily disable -u while sourcing the upstream script,
# then restore our stricter shell mode for the actual build.
set +u
. ./edksetup.sh
set -u

build \
    -a X64 \
    -t "$TOOLCHAIN" \
    -b "$TARGET" \
    -p ArcsPkg/ArcsPkg.dsc

echo
echo "EFI image:"
echo "$WORKSPACE/Build/ArcsPkg/${TARGET}_${TOOLCHAIN}/X64/ArcsGfxBringup.efi"
