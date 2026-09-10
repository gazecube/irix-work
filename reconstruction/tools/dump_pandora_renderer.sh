#!/bin/sh
set -eu

# Dump the original IRIX clogin PandoraRenderer constructor/call sites plus the
# corresponding libimdIconPanel renderer symbols in one reproducible pass.
#
# Usage:
#   ./dump_pandora_renderer.sh /path/to/clogin /path/to/libimdIconPanel.so [output-dir]
#
# Known target from clogin symbols:
#   PandoraRenderer::PandoraRenderer() = 0x100184b4
#
# This tool intentionally gathers evidence only. It does not assume that
# PandoraRenderer derives from ImdIconRenderer; the constructor/vtable bytes
# are what we need to prove or reject that relationship.

if [ "$#" -lt 2 ] || [ "$#" -gt 3 ]; then
    echo "usage: $0 /path/to/clogin /path/to/libimdIconPanel.so [output-dir]" >&2
    exit 2
fi

CLOGIN=$1
IMD=$2
OUT=${3:-pandora-renderer-dump}

if [ ! -f "$CLOGIN" ]; then
    echo "error: not a file: $CLOGIN" >&2
    exit 1
fi
if [ ! -f "$IMD" ]; then
    echo "error: not a file: $IMD" >&2
    exit 1
fi

mkdir -p "$OUT"

find_tool()
{
    for t in "$@"; do
        if command -v "$t" >/dev/null 2>&1; then
            command -v "$t"
            return 0
        fi
    done
    return 1
}

OBJDUMP=$(find_tool mips-sgi-irix6-objdump mips-linux-gnu-objdump llvm-objdump objdump || true)
READELF=$(find_tool mips-sgi-irix6-readelf mips-linux-gnu-readelf llvm-readelf readelf || true)
NM=$(find_tool mips-sgi-irix6-nm mips-linux-gnu-nm llvm-nm nm || true)
STRINGS=$(find_tool strings llvm-strings || true)

if [ -z "$OBJDUMP" ]; then
    echo "error: no usable objdump found" >&2
    exit 1
fi

{
    echo "clogin: $CLOGIN"
    echo "libimdIconPanel: $IMD"
    echo "PandoraRenderer ctor: 0x100184b4"
    echo "objdump: $OBJDUMP"
    echo "readelf: ${READELF:-not found}"
    echo "nm: ${NM:-not found}"
} > "$OUT/README.txt"

# Constructor window. Keep a generous tail so a base constructor call and the
# vptr/data member initialization remain visible even if the symbol size is not
# available to the host toolchain.
"$OBJDUMP" -C -drz --start-address=0x100184b4 --stop-address=0x10018640 "$CLOGIN" \
    > "$OUT/PandoraRenderer.constructor.disasm.txt" 2>&1 || \
"$OBJDUMP" -drz --start-address=0x100184b4 --stop-address=0x10018640 "$CLOGIN" \
    > "$OUT/PandoraRenderer.constructor.disasm.txt" 2>&1

# readyToDisplay contains the renderer allocation/adoption and the virtual
# calls that receive true and 2.0f.
"$OBJDUMP" -C -drz --start-address=0x100115e8 --stop-address=0x10011b8c "$CLOGIN" \
    > "$OUT/PandoraComponent.readyToDisplay.disasm.txt" 2>&1 || \
"$OBJDUMP" -drz --start-address=0x100115e8 --stop-address=0x10011b8c "$CLOGIN" \
    > "$OUT/PandoraComponent.readyToDisplay.disasm.txt" 2>&1

# displayIcon is included because its UserView construction/adoption calls may
# reveal the matching view hierarchy and vtable usage.
"$OBJDUMP" -C -drz --start-address=0x10011b8c --stop-address=0x10011d10 "$CLOGIN" \
    > "$OUT/PandoraComponent.displayIcon.disasm.txt" 2>&1 || \
"$OBJDUMP" -drz --start-address=0x10011b8c --stop-address=0x10011d10 "$CLOGIN" \
    > "$OUT/PandoraComponent.displayIcon.disasm.txt" 2>&1

if [ -n "$READELF" ]; then
    "$READELF" -sW "$CLOGIN" > "$OUT/clogin.symbols.txt" 2>&1 || true
    "$READELF" -rW "$CLOGIN" > "$OUT/clogin.relocations.txt" 2>&1 || true
    "$READELF" -sW "$IMD" > "$OUT/libimdIconPanel.symbols.txt" 2>&1 || true
    "$READELF" -rW "$IMD" > "$OUT/libimdIconPanel.relocations.txt" 2>&1 || true
    "$READELF" -wi "$IMD" > "$OUT/libimdIconPanel.debug-info.txt" 2>&1 || true
fi

if [ -n "$NM" ]; then
    "$NM" -C -n "$CLOGIN" > "$OUT/clogin.nm.txt" 2>&1 || "$NM" -n "$CLOGIN" > "$OUT/clogin.nm.txt" 2>&1 || true
    "$NM" -C -n "$IMD" > "$OUT/libimdIconPanel.nm.txt" 2>&1 || "$NM" -n "$IMD" > "$OUT/libimdIconPanel.nm.txt" 2>&1 || true
fi

if [ -n "$STRINGS" ]; then
    "$STRINGS" -a "$IMD" | grep -E 'Imd(Renderer|IconRenderer|IconView|View|Layout)|vtable|__vtbl' \
        > "$OUT/libimdIconPanel.renderer-strings.txt" 2>/dev/null || true
fi

# Compact views for quick manual inspection.
if [ -f "$OUT/clogin.nm.txt" ]; then
    grep -E 'PandoraRenderer|PandoraLayout|UserView|readyToDisplay|displayIcon' \
        "$OUT/clogin.nm.txt" > "$OUT/clogin.renderer-symbols.txt" 2>/dev/null || true
elif [ -f "$OUT/clogin.symbols.txt" ]; then
    grep -E 'PandoraRenderer|PandoraLayout|UserView|readyToDisplay|displayIcon' \
        "$OUT/clogin.symbols.txt" > "$OUT/clogin.renderer-symbols.txt" 2>/dev/null || true
fi

if [ -f "$OUT/libimdIconPanel.nm.txt" ]; then
    grep -E 'Imd(Renderer|IconRenderer|IconView|View|Layout)' \
        "$OUT/libimdIconPanel.nm.txt" > "$OUT/libimdIconPanel.renderer-symbols.txt" 2>/dev/null || true
elif [ -f "$OUT/libimdIconPanel.symbols.txt" ]; then
    grep -E 'Imd(Renderer|IconRenderer|IconView|View|Layout)' \
        "$OUT/libimdIconPanel.symbols.txt" > "$OUT/libimdIconPanel.renderer-symbols.txt" 2>/dev/null || true
fi

# Pull only renderer/view/layout-related DWARF context into a smaller file when
# readelf was able to decode the debug info.
if [ -f "$OUT/libimdIconPanel.debug-info.txt" ]; then
    grep -n -A20 -B5 -E 'ImdIconRenderer|ImdRenderer|ImdIconView|ImdView|ImdIconLayout|ImdLayout' \
        "$OUT/libimdIconPanel.debug-info.txt" > "$OUT/libimdIconPanel.renderer-dwarf.txt" 2>/dev/null || true
fi

cat > "$OUT/NEXT.txt" <<'EOF'
Primary questions to answer from this dump:

1. Does PandoraRenderer::PandoraRenderer() directly call
   ImdIconRenderer::ImdIconRenderer() or another recovered base constructor?
2. What vtable address is installed by PandoraRenderer, and what functions sit
   in the slots called by readyToDisplay() with true and 2.0f?
3. Does UserView construction call ImdIconView::ImdIconView()?
4. Does PandoraLayout construction call ImdIconLayout::ImdIconLayout()?
5. What are the exact signatures/parameter registers at those call sites?

Do not assign inheritance edges until constructor or DWARF inheritance evidence
proves them.
EOF

echo "wrote $OUT"
