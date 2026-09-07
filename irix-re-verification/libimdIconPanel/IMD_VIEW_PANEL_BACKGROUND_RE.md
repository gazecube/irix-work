# ImdViewPanel background and superclass reconstruction

Target: IRIX 6.5.0 `usr/lib32/internal/libimdIconPanel.so`.

## Result

The original `ImdViewPanel` is not based directly on `XmDrawingArea`.

The class record `imdViewPanelClassRec` is at `0x096caaa0`. Its superclass relocation resolves to:

```text
sgVisualDrawingAreaWidgetClass
```

The exported widget-class pointer is `imdViewPanelWidgetClass` at `0x096cab78`.

Therefore the original inheritance is:

```text
Core / XmManager / XmDrawingArea
  -> SgVisualDrawingArea
     -> ImdViewPanel
```

This matters because `SgVisualDrawingArea` supplies visual, colormap, dither-background, and normal inherited Core background behavior.

## ImdViewPanel resource table

The `ImdViewPanel` class record points to its own resource table at `0x096ca690` and reports `0x22` entries, or 34 resources.

Recovered named resources include:

```text
horzScrollPolicy
vertScrollPolicy
dragBorderWidth
useDoubleBuffer
dropEnabled
dragOverEnabled
boxSelectEnabled
boxSelectUseParent
renameEnabled
dragEnabled
commandEnabled
selectCallback
extendSelectCallback
deselectCallback
boxSelectCallback
pasteCallback
commandCallback
enterCallback
leaveCallback
motionCallback
viewMovedCallback
enterPanelCallback
leavePanelCallback
dragStartCallback
purgeDeletedCallback
expandCallback
typeToSelectCallback
renameCallback
layoutDoneCallback
newFilterCallback
changeBlockDoneCallback
```

There is no ordinary `background` resource declared by `ImdViewPanel` itself. The panel's background is inherited through the `SgVisualDrawingArea` / Xt superclass chain.

Two resource-table entries use encoded/predefined names and have string defaults `red` and `white`. Their semantic names are not yet proven and are intentionally not guessed here.

## SgVisualDrawingArea resources relevant to Clogin

The SGI widget defines resources including:

```text
SgNditherBackground
SgNinstallColormap
XmNvisual
```

Clogin constructs the panel with:

```text
installColormap = False
ditherBackground = False
```

Those are superclass resources, not private `ImdViewPanel` resources.

## Renderer background path

`ImdIconRenderer::realize(Widget)` begins at `0x096aed80`.

The function builds an `ImdArgList`, fetches widget resources, and then dispatches the first fetched Pixel through renderer vtable slot `+0x180`.

The call at approximately `0x096aee30` has this shape:

```text
load renderer vtable
load method at vtable + 0x188
load this-adjustment at vtable + 0x180
load fetched Pixel from stack
jalr method
```

The renderer vtable slot at `+0x180` is binary-proven as:

```text
ImdIconRenderer::setBackgroundPixel(unsigned long)
```

at `0x096b0124`.

Thus the value fetched from the realized panel is the panel background Pixel and is passed directly to `setBackgroundPixel()`.

`setBackgroundPixel()` rebuilds the renderer GCs with X GC mask `0x0c`, or `GCForeground | GCBackground`, using the supplied panel Pixel as the GC background.

The resulting path is:

```text
SgVisualDrawingArea resolves inherited panel background
  -> ImdViewPanel instance
     -> ImdIconRenderer::realize(panel)
        -> fetch panel background Pixel
           -> ImdIconRenderer::setBackgroundPixel(pixel)
              -> renderer GCs use the same background
```

## Consequence for the portable reconstruction

The previous portable alias:

```text
imdViewPanelWidgetClass = xmDrawingAreaWidgetClass
```

was structurally wrong and can produce a different resolved background even when the rest of the Clogin resources are correct.

The correct compatibility preference is:

```text
SgVisualDrawingArea when libSgm provides it
fallback XmDrawingArea only when Sgm is unavailable
```

No screenshot-derived gray should be hard-coded. The original renderer consumes the background resolved by the SGI widget/resource stack.
