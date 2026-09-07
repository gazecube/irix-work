# ImdInvertedTeeRenderer reverse engineering notes

Primary evidence: `usr/lib32/internal/libimdIconPanel.so` from the preserved `shared_objects.tar` corpus.

Corpus SHA-256:

```
7e1ef829d11bb06b5ae504f80517721ef1885990a25800312a901b9b3141a98c
```

Library SHA-256:

```
e23ceeede4bb684f6a397a503c148038c8329507396f6defaadf41ffc89b6309  libimdIconPanel.so
```

The library is ELF32 MSB MIPS N32 MIPS-III, contains debug info, and is not stripped.

## Relevant symbols

```
09684198  ImdInvertedTeeRenderer::ImdInvertedTeeRenderer()
0968421c  ImdInvertedTeeRenderer::setScale(float)
096842f0  ImdInvertedTeeRenderer::setSize(ImdView &)
096844e0  ImdInvertedTeeRenderer::realize(Widget)
096b55f0  ImdInvertedTeeRenderer::getRect(ImdView &, long &, long &, long &, long &)
096b56e4  ImdInvertedTeeRenderer::getXRect(...)
096b5818  ImdInvertedTeeRenderer::getIconXRect(...)
096b58dc  ImdInvertedTeeRenderer::getLabelXRect(...)
096b5a6c  ImdInvertedTeeRenderer::beginRender()
096b5b0c  ImdInvertedTeeRenderer::endRender()
096b5b5c  ImdInvertedTeeRenderer::getScaleMultiplier()
096b5b70  ImdInvertedTeeRenderer::getIconX(...)
096b5bbc  ImdInvertedTeeRenderer::getTextX(...)
096b5c08  ImdInvertedTeeRenderer::getTextY(...)
```

## ImdView geometry fields

`ImdRenderer::getRect()` at `0x096b261c` proves the base `ImdView` geometry layout:

```
+0x04  x
+0x08  y
+0x0c  width
+0x10  height
```

It returns:

```
left   = view.x
 top   = view.y
right  = view.x + view.width
bottom = view.y + view.height
```

The member names above are semantic names recovered from use; exact private source member spellings are not yet recovered.

## Constructor and scale relationship

The inverted-tee constructor calls the `ImdIconRenderer` base constructor and then initializes its own fields.

At `0x096841cc` through `0x096841e0`:

```
this+0x80 = (this+0x20) * 0.5
```

`this+0x20` is the renderer scale field used throughout the base renderer. `this+0x80` is the inverted-tee icon scale field, based on its use in `setSize()` and the exported `setIconScale(float)` method.

`ImdInvertedTeeRenderer::setScale(float)` at `0x0968421c` calls the base scale setter and, unless the explicit icon-scale flag at `+0x84` is set, again computes:

```
iconScale = scale * 0.5
```

Therefore the normal icon-to-renderer scale ratio is exactly `0.5`.

## Resource recovery

`ImdInvertedTeeRenderer::realize(Widget)` at `0x096844e0` calls `VkGetResource()` for two literal resource names found in `.rodata`:

```
invertedTeeMargin
minimumIconWidth
```

The literal default strings passed by the function are:

```
invertedTeeMargin = "0"
minimumIconWidth  = "50"
```

The values are stored as:

```
this+0x86  invertedTeeMargin
this+0x88  minimumIconWidth plus a base-renderer width contribution
```

The exact private member spelling for the fields themselves is not yet recovered. The resource names are binary-proven.

Clogin's original app-defaults override:

```
*ImdViewPanel.invertedTeeMargin: 3
```

so Clogin uses a vertical icon-to-label gap of 3 pixels.

## setSize(ImdView &) flow

`ImdInvertedTeeRenderer::setSize(ImdView &)` at `0x096842f0` is 496 bytes.

The function first obtains the view label through the view virtual interface, then calls the renderer virtual `formatLabel(const ImdString &)`. Vtable decoding identifies that call exactly: the virtual entry at offset `0x144` is `ImdIconRenderer::formatLabel`.

It then chooses a positional font through `ImdIconRenderer::getPosFont(...)`, converts the formatted `ImdString` to bytes, obtains its length, and calls:

```
XmbTextExtents(fontSet, string, length, NULL, &logicalRect)
```

The imported symbols used by this sequence are:

```
ImdString::~ImdString()
ImdString::operator const char *()
ImdString::getLength(bool) const
XmbTextExtents
```

The icon extent is calculated as:

```
iconExtent = trunc((iconScale / scale) * 100.0f)
```

Because the normal inverted-tee relation is `iconScale = scale * 0.5`, the standard icon extent resolves to:

```
50 pixels
```

The function then writes the view height at `view+0x10`:

```
height = iconExtent + invertedTeeMargin + logicalRect.height
```

For Clogin this is normally:

```
height = 50 + 3 + formattedLabelHeight
```

The initial width candidate is:

```
width = max(iconExtent, logicalRect.width)
```

The renderer's truncate flag is then consulted through the binary-proven `ImdRenderer::getTruncateFlag()` call.

When truncation is disabled, width is forced to at least the renderer's recovered minimum-width field:

```
width = max(width, minimumWidth)
```

When truncation is enabled, the width is additionally capped by a base-renderer maximum-width field:

```
width = min(max(width, minimumWidth), truncateMaximumWidth)
```

The exact private source names of those base-renderer fields remain to be recovered. Do not encode guessed names for them until additional symbol/DWARF evidence establishes the spellings.

## Portable Clogin implementation

The current Linux probe still uses an `XmDrawnButton` as a temporary selection transport, not as the original rendering mechanism.

The previous guessed `128x96` dimensions have been removed. The portable widget now uses the binary-proven inverted-tee sizing formula:

```
iconExtent = 50
invertedTeeMargin = 3
width = max(50, measuredLabelWidth)
height = 50 + 3 + measuredLabelHeight
```

The original renderer measures the formatted label with `formatLabel()`, `getPosFont()`, and `XmbTextExtents()`. Until the reconstructed renderer/font path is connected, the probe uses `XmStringExtent()` as an explicitly documented portable substitute for label measurement only.

The geometry formula and constants are recovered from the IRIX binary; only the font-measurement transport remains portable.
