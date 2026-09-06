#!/usr/bin/env bash
set -euo pipefail

# Local archaeology helper for the original IRIX libimdIconPanel.so.
#
# Usage:
#   ./irix-re/libimdIconPanel/tools/dump_model_based_icon_view.sh /path/to/libimdIconPanel.so
#
# If no path is supplied, this script searches the common reconstructed/evidence
# locations in this checkout. It does not download anything and does not use
# GitHub Actions.

bin="${1:-}"

if [[ -z "$bin" ]]; then
    candidates=(
        "irix-re/evidence/shared_objects/libimdIconPanel.so"
        "irix-re/evidence/shared_objects/libimdIconPanel.so.1"
        "irix-re/evidence/libimdIconPanel.so"
        "irix-re/evidence/libimdIconPanel.so.1"
    )

    for candidate in "${candidates[@]}"; do
        if [[ -f "$candidate" ]]; then
            bin="$candidate"
            break
        fi
    done
fi

if [[ -z "$bin" || ! -f "$bin" ]]; then
    echo "error: original libimdIconPanel.so not found" >&2
    echo "usage: $0 /path/to/libimdIconPanel.so" >&2
    exit 1
fi

for tool in mips-linux-gnu-readelf mips-linux-gnu-objdump c++filt; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "error: required tool '$tool' not found" >&2
        exit 1
    fi
done

outdir="${TMPDIR:-/tmp}/imd-model-view-archaeology"
rm -rf "$outdir"
mkdir -p "$outdir"

raw="$outdir/symbols.raw"
demangled="$outdir/symbols.txt"
report="$outdir/report.txt"

mips-linux-gnu-readelf -Ws "$bin" > "$raw"
mips-linux-gnu-readelf -Ws "$bin" | c++filt > "$demangled"

{
    echo "===== specimen ====="
    file "$bin"
    sha256sum "$bin"
    echo

    echo "===== focused symbols ====="
    grep -E 'Imd(ModelBasedIconView|ModelBasedIconPanel|IconView|IconPanel|ViewPanel)::|ImdViewPanel(AdoptView|DeleteView|DeleteAllViews|BeginBlockChanges|EndBlockChanges)' "$demangled" || true
    echo

    echo "===== source-unit strings ====="
    strings -a "$bin" | grep -E '(^|/)(ModelBasedIconView|ModelBasedIconPanel|IconView|ViewPanel)\.c\+\+$' || true
    echo

    echo "===== DWARF names ====="
    mips-linux-gnu-readelf --debug-dump=info "$bin" 2>/dev/null \
        | c++filt \
        | grep -E -A24 -B8 'DW_AT_name.*Imd(ModelBasedIconView|ModelBasedIconPanel|IconView|ViewPanel)' \
        || true
    echo

    echo "===== disassembly ====="

    dump_matching() {
        local regex="$1"
        grep -E "$regex" "$raw" | while read -r num value size type bind vis ndx name rest; do
            [[ "$type" == "FUNC" ]] || continue
            [[ "$size" =~ ^[0-9]+$ ]] || continue
            [[ "$size" -gt 0 ]] || continue

            local start stop
            start=$((16#$value))
            stop=$((start + size))

            printf '\n===== %s 0x%x size %d =====\n' "$name" "$start" "$size"
            mips-linux-gnu-objdump -drC \
                --start-address="$start" \
                --stop-address="$stop" \
                "$bin"
        done
    }

    # Constructor/destructor and the exact model/view population seam.
    dump_matching 'ImdModelBasedIconView|ImdModelBasedIconPanel'

    # Lower-level adoption/deletion functions called by the panel path.
    dump_matching 'ImdViewPanel(AdoptView|DeleteView|DeleteAllViews|BeginBlockChanges|EndBlockChanges)'
} > "$report"

echo "$report"
