# FM scaleZoom thumbwheel reverse-engineering notes

Target fm specimen:

```text
irix-re/evidence/fm
SHA-256 65430bf4937027314b1a940b1e7f109d01f3dc48275223216360e3b50a809818
```

The recovered FileManager resources define `scaleZoom` as a vertical scaling
control with minimum 60, maximum 500, value 100, homePosition 100,
showValue false, and traversalOn false.

The preserved `libSgm.so.1` exports the original SGI thumbwheel implementation:

```text
SgCreateThumbWheel
SgThumbWheelGetValue
SgThumbWheelSetValue
sgThumbWheelClassRec
sgThumbWheelWidgetClass
```

It also contains the original source/header names `ThumbWheel.c`,
`Sgm/ThumbWheel.h`, and `Sgm/ThumbWheelP.h`.

The runnable fm reconstruction now resolves `sgThumbWheelWidgetClass` from the
runtime. Because fm currently puts the reconstructed libSgm ahead of MaXX's
library, it can load `/opt/MaXX/lib64/libSgm.so.1` (or the lib directory
variant) explicitly and instantiate the preserved SgThumbWheel class without
requiring MaXX headers at build time. Hosts without the historical widget keep
an XmScale fallback.

The callback reads the widget's `value` resource and continues to feed
`ImdViewPanelSetScale()`. This avoids assuming an unrecovered callback-struct
layout while preserving the binary/resource-backed scale range and identity.
