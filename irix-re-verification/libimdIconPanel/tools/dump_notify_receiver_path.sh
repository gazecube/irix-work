#!/usr/bin/env bash
set -euo pipefail

# Binary-first archaeology helper for the exact model callback -> NotifyReceiver
# -> ImdModelBasedIconView path used by the original IRIX libimdIconPanel.
#
# Usage:
#   ./irix-re/libimdIconPanel/tools/dump_notify_receiver_path.sh /path/to/libimdIconPanel.so /path/to/libimdModel.so
#   ./irix-re/libimdIconPanel/tools/dump_notify_receiver_path.sh /path/to/shared_objects.tar
#
# With a tar archive, both shared objects are located after one extraction.

input1="${1:-}"
input2="${2:-}"
outdir="${TMPDIR:-/tmp}/imd-notify-receiver-archaeology"
evidence_root="$outdir/shared-objects-root"
panel_bin=""
model_bin=""

rm -rf "$outdir"
mkdir -p "$outdir"

find_libs_in_root() {
    local root="$1"
    panel_bin="$(find "$root" -type f \( -name 'libimdIconPanel.so' -o -name 'libimdIconPanel.so.1' \) -print -quit)"
    model_bin="$(find "$root" -type f \( -name 'libimdModel.so' -o -name 'libimdModel.so.1' \) -print -quit)"
}

if [[ -n "$input1" ]] && [[ -f "$input1" ]] && file "$input1" | grep -q 'tar archive'; then
    mkdir -p "$evidence_root"
    tar -xf "$input1" -C "$evidence_root"
    find_libs_in_root "$evidence_root"
else
    if [[ -n "$input1" && -f "$input1" ]]; then
        panel_bin="$input1"
    fi
    if [[ -n "$input2" && -f "$input2" ]]; then
        model_bin="$input2"
    fi
fi

if [[ -z "$panel_bin" || -z "$model_bin" ]]; then
    for archive in \
        irix-re/evidence/shared_objects.tar \
        shared_objects.tar; do
        if [[ -f "$archive" ]] && file "$archive" | grep -q 'tar archive'; then
            mkdir -p "$evidence_root"
            tar -xf "$archive" -C "$evidence_root"
            find_libs_in_root "$evidence_root"
            break
        fi
    done
fi

if [[ -z "$panel_bin" || ! -f "$panel_bin" ]]; then
    echo "error: original libimdIconPanel.so not found" >&2
    exit 1
fi
if [[ -z "$model_bin" || ! -f "$model_bin" ]]; then
    echo "error: original libimdModel.so not found" >&2
    exit 1
fi

for tool in mips-linux-gnu-readelf mips-linux-gnu-objdump c++filt strings sha256sum; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "error: required tool '$tool' not found" >&2
        exit 1
    }
done

panel_raw="$outdir/panel.symbols.raw"
model_raw="$outdir/model.symbols.raw"
panel_dem="$outdir/panel.symbols.txt"
model_dem="$outdir/model.symbols.txt"
report="$outdir/report.txt"

mips-linux-gnu-readelf -Ws "$panel_bin" > "$panel_raw"
mips-linux-gnu-readelf -Ws "$model_bin" > "$model_raw"
mips-linux-gnu-readelf -Ws "$panel_bin" | c++filt > "$panel_dem"
mips-linux-gnu-readelf -Ws "$model_bin" | c++filt > "$model_dem"

dump_matching() {
    local bin="$1"
    local raw="$2"
    local regex="$3"

    grep -E "$regex" "$raw" | while read -r num value size type bind vis ndx name rest; do
        [[ "$type" == "FUNC" ]] || continue
        [[ "$size" =~ ^[0-9]+$ ]] || continue
        [[ "$size" -gt 0 ]] || continue

        local start stop
        start=$((16#$value))
        stop=$((start + size))
        printf '\n===== %s 0x%x size %d =====\n' "$name" "$start" "$size"
        mips-linux-gnu-objdump -drC --start-address="$start" --stop-address="$stop" "$bin"
    done
}

{
    echo "===== specimens ====="
    file "$panel_bin"
    sha256sum "$panel_bin"
    file "$model_bin"
    sha256sum "$model_bin"
    echo

    echo "===== exact panel-side symbols ====="
    grep -E 'NotifyReceiver|ImdModelBasedIconPanel::(changeModel|showChildren|show|setModel)|ImdModelBasedIconView::ImdModelBasedIconView|ImdIconPanel::adoptView|ImdViewPanelAdoptView' "$panel_dem" || true
    echo

    echo "===== exact model-side callback symbols ====="
    grep -E 'ImdModel::(addClientCallback|removeClientCallback|notifyClients|activate|deactivate|getSelf|getDNA|getNextChild|createChildIterator|destroyChildIterator)' "$model_dem" || true
    echo

    echo "===== NotifyReceiver-related DWARF ====="
    mips-linux-gnu-readelf --debug-dump=info "$panel_bin" 2>/dev/null \
        | c++filt \
        | grep -E -A36 -B12 'NotifyReceiver|receiveNotification|ImdModelBasedIconPanel::showChildren|ImdModelBasedIconPanel::changeModel' \
        || true
    echo

    echo "===== CallbackInfo / ImdModel callback DWARF ====="
    mips-linux-gnu-readelf --debug-dump=info "$model_bin" 2>/dev/null \
        | c++filt \
        | grep -E -A40 -B12 'CallbackInfo|ImdModel::addClientCallback|ImdModel::notifyClients|ImdModel::removeClientCallback' \
        || true
    echo

    echo "===== panel-side disassembly ====="
    dump_matching "$panel_bin" "$panel_raw" 'NotifyReceiver|ImdModelBasedIconPanel|ImdModelBasedIconView'
    dump_matching "$panel_bin" "$panel_raw" 'ImdIconPanel.*adoptView|ImdViewPanelAdoptView'
    echo

    echo "===== model-side callback disassembly ====="
    dump_matching "$model_bin" "$model_raw" 'ImdModel.*(addClientCallback|removeClientCallback|notifyClients|activate|deactivate|getSelf|getDNA)'
} > "$report"

echo "$report"
