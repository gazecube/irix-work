# Reconstruction Graveyard

Temporary implementations that did their job and were deliberately retired.

## Temporary SgThumbWheel line renderer

**Born:** 2026-09-06

**Retired:** 2026-09-06

**Cause of death:** Successfully proved the reconstructed widget class, expose path, pointer interaction, callback ABI, drag direction, and live `fm` icon scaling, then became visually indefensible.

It drew the wheel as one flat rectangle plus a stack of `XDrawLine()` ribs. That was enough to prove the plumbing, but it was never intended to survive renderer archaeology.

The MaXX x86-64 `libSgm.so.1.1` specimen proved that the native widget keeps eight cached wheel-state pixmaps in two four-frame banks, two 16x16 home-button pixmaps, copies the active frame with `XCopyArea()`, and applies Motif top/bottom shadow treatment. Once those facts were recovered, the temporary ASCII-looking renderer was taken out back.

It did its job. RIP.
