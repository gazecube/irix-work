# FmWindow::_createPanels reverse-engineering notes

Target symbol:

```text
_createPanels__8FmWindowGv
0x100187e0
size 3596 bytes
```

Current specimen:

```text
irix-re/evidence/fm
ELF 32-bit MSB MIPS N32 MIPS-III
sha256 65430bf4937027314b1a940b1e7f109d01f3dc48275223216360e3b50a809818
```

The symbol size and the details below were rechecked against the recovered standalone `fm` specimen by the `FM binary archaeology` workflow on 2026-09-06.

This function is the main composition root for a normal directory window.

## Model acquisition

The function begins by obtaining the IID broker and resolving the directory model through:

```text
ImdBroker::contextBroker()
ImdBroker::getModel(ImdDNA const &)
```

The resolved model becomes the backing model for the main directory panel. In the recovered object, the returned model is stored at `FmWindow +0x78` before panel construction.

## Window/UI shell

The function creates the core Motif container hierarchy with calls including:

```text
XmCreateForm
XtVaCreateManagedWidget
```

It also creates an `ImdViewerFinder` associated with the current directory/string state and connects callbacks from that helper into the window.

## Main directory panel

The primary directory view is constructed as:

```text
ImdModelBasedIconPanel
```

The imported constructor is exactly:

```text
__ct__22ImdModelBasedIconPanelGPCcP10_WidgetRecP8ImdModelP3Argi
```

The new panel object is stored at `FmWindow +0xa4` (`164` decimal). Immediately afterward `_createPanels()` calls:

```text
FmMenuActions::setDirPanel(ImdModelBasedIconPanel *)
```

with the `FmMenuActions` object at `FmWindow +0x1e4`.

The panel later becomes the selection holder as well. This establishes the main directory panel as both the action target and selection source for menu/file operations.

## Visual/resource setup

Before creating the model-based icon panel, FM inspects the window/display visual and builds an argument list with calls including:

```text
XGetWindowAttributes
ImdViewPanelHasOverlayVisual
imdFtrChooseXdbeVisual
imdFtrChooseVisual
ImdArgList::setArg
```

The recovered executable imports:

```text
ImdViewPanelHasOverlayVisual(Display *)
```

at `0x100131a4`. This confirms that visual selection and double-buffer/overlay capability are resolved before the panel is instantiated.

## View pipeline

The main panel receives the following pluggable view components:

```text
order
filter
renderer
layout
```

through the IID panel adoption APIs.

The recovered executable imports:

```text
ImdViewPanelAdoptRenderer(Widget, ImdRenderer *)
ImdViewPanelAdoptLayout(Widget, ImdLayout *)
```

The concrete renderer constructor imported by `fm` is:

```text
__ct__22ImdInvertedTeeRendererGv
```

and the icon layout constructor is:

```text
__ct__13ImdIconLayoutGP10_WidgetRec
```

This is an important architectural seam: model ordering/filtering, rendering and layout are separate objects attached to the same model-based panel.

## Callback wiring

The directory panel receives a dense callback set through `VkCallbackObject::addCallback()`.

The standalone specimen imports the following `ImdModelBasedIconPanel` callback tokens, among others:

```text
kBeginExistsCallback
kEndExistsCallback
kAddOrDeleteCallback
kDeleteCallback
kModelInvalidCallback
kChangedDNACallback
kViewRenamedCallback
```

The exact callback-to-FmWindow method mapping is still being recovered and should not be guessed.

## Reference-container state

The function derives a reference-container identity through:

```text
ImdRefModel::refContainerDNA(ImdString const &)
```

and stores the resulting `ImdDNA` in the window state. This is the bridge used by shelf/reference behavior rather than treating reference storage as an unrelated side channel.

## Additional presentation controls

The function reads window resources with `VkGetResource()` and `XtGetApplicationResources()` and constructs:

```text
ImdIControlBoard
```

The control board is populated with buttons through:

```text
ImdIControlBoard::addButton()
```

Two `VkPopupMenu` instances are also created, followed by:

```text
FmMenuHandler(FmWindow *, VkMenu *, ...)
```

The resulting menu handler is therefore owned at the window/panel layer and is separate from the lower-level `FmMenuActions` action implementation.

## Final window wiring

Before returning, the function updates menu-toggle visual state, initializes quick help and registers the main panel as the view through:

```text
FmWindow::_initializeQuickHelp()
VkSimpleWindow::addView()
```

## Reimplementation architecture

The normal directory-window path can now be decomposed as:

```text
FmWindow
  |
  +-> ImdModel / directory identity
  |
  +-> ImdModelBasedIconPanel
  |     +-> ImdOrder
  |     +-> ImdFilter
  |     +-> ImdInvertedTeeRenderer
  |     `-> ImdIconLayout
  |
  +-> FmMenuActions
  |     +-> dir panel
  |     `-> selection holder
  |
  +-> ImdIControlBoard
  +-> popup menus
  +-> FmMenuHandler
  `-> quick-help / window presentation layer
```

This is the key object boundary for the Linux FM port: the model, filter/order pipeline, renderer, layout and action layer can be reconstructed independently and then composed in the same place.

## Current portable bring-up boundary

The runnable Linux `fm` now places a reconstructed `ImdModelBasedIconPanel` named `panel` in the recovered directory-panel slot and adopts an `ImdInvertedTeeRenderer` on its underlying `ImdViewPanel` widget.

The portable `ImdBroker`/directory model and `ImdIconLayout` implementations are not complete yet. Until they are binary-backed and present, the panel receives a null model and the already-working `XmList` browser remains as a temporary child of that IMD panel widget. This preserves a usable file manager without claiming that the model/view body has already been reconstructed.
