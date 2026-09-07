# Desktop render path reverse-engineering

Target binary: original IRIX `/usr/sbin/fm` from `desktop_eoe`.

This note records only relationships that are already proven by the recovered fm bodies and imported IMD symbols. Prototype desktop code is not used as evidence.

## Background startup

`FmApp::processArgs()` recognizes the original getopt grammar:

```text
bADo:pv:h:
```

The `-b` branch selects background mode. The later startup path checks the global `theBgWin` and, when absent, constructs:

```text
ImdBackWindow("backWindow", app->baseWidget())
```

This is a distinct startup path from normal `FmWindow("FileManager", ...)` construction.

## BackLayout is an ImdIconLayout

The recovered constructor:

```text
__ct__10BackLayoutGP10_WidgetRec
0x1001f5cc
```

calls:

```text
ImdIconLayout::ImdIconLayout(Widget panel)
```

before installing the BackLayout vtable and background-specific state.

Therefore the desktop/background layout is not an unrelated renderer. It is a specialized `ImdIconLayout` operating on an IMD view panel.

The recovered base calls make the inheritance/use relationship explicit:

```text
BackLayout::addView(ImdView &)
    -> ImdIconLayout::addView(ImdView &)

BackLayout::moveView(ImdView &, long, long)
    -> ImdIconLayout::moveView(ImdView &, long, long)
```

`BackLayout::doLayout()` also iterates the live panel through:

```text
ImdViewPanelCreateIterator
ImdViewPanelResetIterator
ImdViewPanelGetNext
ImdViewPanelDestroyIterator
```

and triggers the panel layout update path.

## Background drawing uses the panel renderer

`BackLayout::shapeView(ImdView &)` at `0x100161c0` proves the background panel obtains its renderer with:

```text
ImdViewPanelGetRenderer(panel)
```

It converts the view's logical position through:

```text
ImdViewPanelPointToX(panel, logicalX, logicalY, x, y)
```

and then dispatches renderer virtual methods for either:

```text
renderer bounds
```

or the renderer's detailed/pretty shape drawing path.

The non-pretty path obtains renderer bounds, builds an `XRectangle`, and unions that rectangle into the background window's X Shape region. The pretty path lets the renderer draw the detailed shape into the Shape target with an Xt GC.

This proves the background/desktop surface and normal IMD icon machinery share the `ImdViewPanel` renderer abstraction. The desktop is not painting independent hand-coded icon glyphs.

## Desktop placement is BackLayout policy over ImdView

Recovered background-placement functions operate directly on `ImdView` geometry.

`findHoles__10BackLayoutGiR11ImdViewListT2` at `0x1002ee50`:

- iterates the background `ImdViewPanel`
- reads `ImdView` x/y/width/height fields
- tests candidate rectangles for overlap
- allocates temporary `ImdView` objects to represent free positions
- scans from the right side of the background and wraps vertically/horizontally according to BackLayout fields

`snapViewToGrid__10BackLayoutGR7ImdViewRlT2` at `0x10024ee8` proves that desktop icon coordinates are snapped by `BackLayout` using the live `ImdView` extents and background scale, rather than by a separate fixed object grid.

## Model-side desktop population

The recovered `bgicons` binary proves that desktop objects are represented through the IMD model/reference system:

```text
ImdRefModel::desktopContainerDNA()
ImdRefModel::overlayContainerDNA()
ImdRefModel::createReference(...)
ImdBroker::contextBroker()
ImdBroker::normalizeDNA(...)
ImdBroker::getModel(...)
```

Its persistent icon database stores canonical serialized `ImdDNA` references containing:

```text
type + selector + optional "fn" attribute
```

This is the model-side counterpart to the `ImdView`/`BackLayout` render path recovered from fm.

## Binary-proven architecture

The currently proven desktop path is therefore:

```text
fm -b
  |
  v
FmApp::processArgs()
  |
  v
ImdBackWindow("backWindow", ...)
  |
  +-> IMD desktop/reference model objects
  |
  +-> ImdViewPanel
  |      |
  |      +-> ImdView objects
  |      `-> ImdViewPanelGetRenderer()
  |
  `-> BackLayout : ImdIconLayout
         |
         +-> normal ImdIconLayout add/move behavior
         +-> desktop placement/grid policy
         `-> X Shape integration around renderer extents
```

This is sufficient to reject the old prototype architecture of an independent desktop icon painter. The reconstruction should share the IMD view/renderer implementation with fm and add the binary-proven desktop-specific pieces around it: `ImdBackWindow`, `BackLayout`, desktop model/reference population, and X Shape behavior.

## Still unresolved

Do not assign these until the binary consumers are traced:

- exact `ImdBackWindow` constructor body and panel class construction sequence
- exact renderer class installed by the background window
- exact model-to-view adapter used by `ImdBackWindow`
- exact meanings of remaining unnamed BackLayout resource fields
- exact semantics of fm options `-A`, `-D`, and `-h`

Those remain binary-archeology targets, not reconstruction guesses.
