#!/bin/sh
set -eu

# Dump the original IRIX clogin PandoraComponentUI constructor and nearby
# evidence in one reproducible pass.
#
# Usage:
#   ./dump_pandora_constructor.sh /path/to/usr/Cadmin/bin/clogin [output-dir]
#
# Target symbol:
#   PandoraComponentUI::PandoraComponentUI(const char *, Widget)
#   0x10012bbc, size 1700 bytes (0x6a4)

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
    echo "usage: $0 /path/to/clogin [output-dir]" >&2
    exit 2
fi

BIN=$1
OUT=${2:-pandora-constructor-dump}
START=0x10012bbc
STOP=0x10013260

if [ ! -f "$BIN" ]; then
    echo "error: not a file: $BIN" >&2
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
STRINGS=$(find_tool strings llvm-strings || true)

if [ -z "$OBJDUMP" ]; then
    echo "error: no usable objdump found" >&2
    exit 1
fi

{
    echo "binary: $BIN"
    echo "constructor start: $START"
    echo "constructor stop:  $STOP"
    echo "objdump: $OBJDUMP"
    echo "readelf: ${READELF:-not found}"
    echo "strings: ${STRINGS:-not found}"
} > "$OUT/README.txt"

# Primary artifact. -C demangles where supported; -r includes relocations;
# -z keeps zero-filled instruction regions visible if present.
"$OBJDUMP" -C -drz --start-address="$START" --stop-address="$STOP" "$BIN" \
    > "$OUT/PandoraComponentUI.constructor.disasm.txt" 2>&1 || \
"$OBJDUMP" -drz --start-address="$START" --stop-address="$STOP" "$BIN" \
    > "$OUT/PandoraComponentUI.constructor.disasm.txt" 2>&1

# A wider window helps resolve branch targets, local thunks, and nearby rodata
# references without requiring another run.
"$OBJDUMP" -C -drz --start-address=0x10012900 --stop-address=0x10013600 "$BIN" \
    > "$OUT/PandoraComponentUI.nearby.disasm.txt" 2>&1 || true

if [ -n "$READELF" ]; then
    "$READELF" -h "$BIN" > "$OUT/elf-header.txt" 2>&1 || true
    "$READELF" -SW "$BIN" > "$OUT/sections.txt" 2>&1 || true
    "$READELF" -sW "$BIN" > "$OUT/symbols.txt" 2>&1 || true
    "$READELF" -rW "$BIN" > "$OUT/relocations.txt" 2>&1 || true
    "$READELF" -dW "$BIN" > "$OUT/dynamic.txt" 2>&1 || true
fi

if [ -n "$STRINGS" ]; then
    "$STRINGS" -a -t x "$BIN" > "$OUT/strings-offsets.txt" 2>&1 || true
    grep -E '(^|[^A-Za-z])(form|panelWindow|scrolledWindow|warningLabel|userFullNameLabel|userLabel|userText|passwordLabel|passwordText|hostLabel|helpButton|loginButton|Xdraw|XmN|XtVa|XtAddCallback)([^A-Za-z]|$)' \
        "$OUT/strings-offsets.txt" > "$OUT/pandora-strings.txt" 2>/dev/null || true
fi

# Imports and symbols most useful while decoding jal/jalr targets.
if [ -f "$OUT/symbols.txt" ]; then
    grep -E 'Pandora(ComponentUI|Component|Window)|XtVaCreate|XmCreateScrolledWindow|XtAddCallback|XmN|xm[A-Za-z]+WidgetClass' \
        "$OUT/symbols.txt" > "$OUT/pandora-symbols.txt" 2>/dev/null || true
fi

cat > "$OUT/NEXT.txt" <<'EOF'
Decode PandoraComponentUI.constructor.disasm.txt call-by-call.
For every Xt/Motif creation call, record:
  1. call address and target
  2. widget instance name
  3. widget class / creation routine
  4. parent Widget
  5. every vararg resource/value pair
  6. object/member offset receiving the returned Widget
  7. XtAddCallback resource, callback target, and client data

Do not infer XmForm attachment relationships from app-defaults offsets alone.
EOF

echo "wrote $OUT"
