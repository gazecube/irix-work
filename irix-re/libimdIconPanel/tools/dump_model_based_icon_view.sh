#!/usr/bin/env bash
set -euo pipefail

# Local archaeology helper for the original IRIX shared-object archive.
#
# Usage:
#   ./irix-re/libimdIconPanel/tools/dump_model_based_icon_view.sh /path/to/libimdIconPanel.so
#   ./irix-re/libimdIconPanel/tools/dump_model_based_icon_view.sh /path/to/all-irix-shared-objects.tar
#
# If a tar archive is supplied, the entire archive is extracted with paths intact
# under /tmp and treated as a miniature IRIX shared-object evidence root. This is
# intentional: follow-up cross-library archaeology can then use the same tree
# without repeatedly extracting individual libraries.

input="${1:-}"
bin=""
outdir="${TMPDIR:-/tmp}/imd-model-view-archaeology"
evidence_root="$outdir/shared-objects-root"
rm -rf "$outdir"
mkdir -p "$outdir"

resolve_input() {
    local candidate="$1"

    if [[ -f "$candidate" ]] && file "$candidate" | grep -q 'tar archive'; then
        rm -rf "$evidence_root"
        mkdir -p "$evidence_root"
        tar -xf "$candidate" -C "$evidence_root"

        bin="$(find "$evidence_root" -type f \
            \( -name 'libimdIconPanel.so' -o -name 'libimdIconPanel.so.1' \) \
            -print -quit)"

        if [[ -z "$bin" ]]; then
            echo "error: libimdIconPanel.so not found after extracting $candidate" >&2
            exit 1
        fi
        return
    fi

    if [[ -f "$candidate" ]]; then
        bin="$candidate"
    fi
}

if [[ -n "$input" ]]; then
    resolve_input "$input"
else
    candidates=(
        "irix-re/evidence/shared_objects.tar"
        "irix-re/evidence/shared_objects/libimdIconPanel.so"
        "irix-re/evidence/shared_objects/libimdIconPanel.so.1"
        "irix-re/evidence/libimdIconPanel.so"
        "irix-re/evidence/libimdIconPanel.so.1"
    )

    for candidate in "${candidates[@]}"; do
        [[ -e "$candidate" ]] || continue
        resolve_input "$candidate"
        [[ -n "$bin" ]] && break
    done
fi

if [[ -z "$bin" || ! -f "$bin" ]]; then
    echo "error: original libimdIconPanel.so not found" >&2
    echo "usage: $0 /path/to/libimdIconPanel.so-or-shared-objects.tar" >&2
    exit 1
fi

for tool in mips-linux-gnu-readelf mips-linux-gnu-objdump c++filt; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "error: required tool '$tool' not found" >&2
        exit 1
    fi
done

raw="$outdir/symbols.raw"
demangled="$outdir/symbols.txt"
report="$outdir/report.txt"

mips-linux-gnu-readelf -Ws "$bin" > "$raw"
mips-linux-gnu-readelf -Ws "$bin" | c++filt > "$demangled"

{
    echo "===== evidence root ====="
    if [[ -d "$evidence_root" ]]; then
        echo "$evidence_root"
    else
        echo "direct binary input"
    fi
    echo

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

    dump_matching 'ImdModelBasedIconView|ImdModelBasedIconPanel'
    dump_matching 'ImdViewPanel(AdoptView|DeleteView|DeleteAllViews|BeginBlockChanges|EndBlockChanges)'
} > "$report"

echo "$report"
