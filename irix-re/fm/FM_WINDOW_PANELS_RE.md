# FmWindow::_createPanels reverse-engineering notes

Target symbol:

```text
_createPanels__8FmWindowGv
0x100187e0
```

This function is the main composition root for a normal directory window.

## Model acquisition

The function begins by obtaining the IID broker and resolving the directory model through:

```text
ImdBroker::contextBroker()
ImdBroker::getModel(ImdDNA const &)
```

The resolved model becomes the backing model for the main directory panel.

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

using the resolved `ImdModel` and an `ImdArgList` of visual/resources parameters.

Immediately after creation it is registered with `FmMenuActions` through:

```text
FmMenuActions::setDirPanel()
```

and later through:

```text
FmMenuActions::setSelectionHolder()
```

This establishes the main directory panel as both the action target and selection source for menu/file operations.

## Visual/resource setup

Before creating the model-based icon panel, FM inspects the window/display visual and builds an argument list with:

```text
XGetWindowAttributes
ImdViewPanelIsOverlayVisual
imdFtrChooseXdbeVisual
imdFtrChooseVisual
ImdArgList::setArg
```

This confirms that visual selection and double-buffer/overlay capability are resolved before the panel is instantiated.

## View pipeline

The main panel receives the following pluggable view components:

```text
order
filter
renderer
layout
```

through the IID panel adoption APIs:

```text
ImdViewPanelAdoptOrder()
ImdViewPanelAdoptFilter()
ImdViewPanelAdoptRenderer()
ImdViewPanelAdoptLayout()
```

The concrete renderer is:

```text
ImdInvertedTeeRenderer
```

and the icon layout is:

```text
ImdIconLayout
```

This is an important architectural seam: model ordering/filtering, rendering and layout are separate objects attached to the same model-based panel.

## Callback wiring

The directory panel receives a dense callback set through `VkCallbackObject::addCallback()`.

The original constructor wires callbacks for selection/model/view events before exposing the panel through menu actions. The exact callback names should be recovered in a later callback-specific pass, but their location and ownership are now established.

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
