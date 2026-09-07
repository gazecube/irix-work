# ImdDragManager / desktop drag archaeology

This note records only behavior recovered from the preserved original IRIX
`/usr/lib32/internal/libimdIconPanel.so` specimen.

## Exported drag surface

The original N32 library exports:

```
0x0969eca0  dragStart__12ImdIconPanelGilT2P7_XEvent
0x0969eb18  dragMotion__12ImdIconPanelGP7ImdView
0x0969eaa0  dragEnd__12ImdIconPanelGv
0x09682c88  getDragManager__12ImdIconPanelGv

0x096a0b70  dragStart__22ImdModelBasedIconPanelGilT2P7_XEvent
0x096a0a30  dragEnd__22ImdModelBasedIconPanelGv

0x096809b8  ImdDragManager::ImdDragManager(ImdIconPanel *)
0x0968a920  ImdDragManager::~ImdDragManager()
0x09694818  ImdDragManager::dragStart(int,long,long,XEvent *)
0x09696938  ImdDragManager::dragMotion(XPoint const &)
0x09696668  ImdDragManager::dragFinished()
0x09695984  ImdDragManager::setupDrag(int,long,long,XEvent *)
0x09695b08  ImdDragManager::computeBounds(long &,long &,long &,long &)
0x0969577c  ImdDragManager::drawDraggedItems(Widget,XRectangle &)
0x096b44b4  ImdDragManager::shapeDragWindow()
0x09681034  ImdDragManager::init(Widget)
0x096814e8  ImdDragManager::oneTimeInit(Widget)
```

The original library also imports and uses Motif/SGI DnD primitives including
`XmDragStart`, `dndAddButton1Translations`, `dndSetUpDragShell`,
`SgDragShellColormapWidget`, and `sgDragShellWidgetClass`.

## ImdIconPanel ownership

Original `ImdIconPanel` constructor (`0x09682ec8`) allocates exactly 124 bytes
for `ImdDragManager`, constructs it with the panel, and stores the pointer at
original panel offset `+0x44` when drag support is available.

The constructor installs the original ViewPanel callback resources, including
verbatim resource name:

```
dragStartCallback
```

Other callback-resource strings in the same constructor include:

```
selectCallback
extendSelectCallback
boxSelectCallback
deselectCallback
renameCallback
enterCallback
leaveCallback
enterPanelCallback
leavePanelCallback
commandCallback
pasteCallback
purgeDeletedCallback
```

So source dragging is initiated by the ViewPanel callback layer, not by a
separate desktop-only X event path.

## ImdIconPanel::dragStart

Original `ImdIconPanel::dragStart()` first invokes the panel virtual that resolves
exactly to:

```
ImdIconPanel::destroyRenameWidget()
```

It then checks `panel + 0x44` and dispatches the drag to the stored
`ImdDragManager` virtual with the original `(int,long,long,XEvent *)` arguments.

Base `ImdIconPanel::dragEnd()` is an empty eight-byte body. The
`ImdModelBasedIconPanel` override owns additional model/reference cleanup after
DnD completion.

## ImdView geometry used by hit-testing

Original `ImdView` constructor is `0x09687804`. The geometry used by
`ImdViewPanelFindViewByLocation()` is:

```
ImdView + 0x04  x
ImdView + 0x08  y
ImdView + 0x0c  width
ImdView + 0x10  height
ImdView + 0x14  flags, bit 31 gates visible/hit-test participation
```

`ImdViewPanelFindViewByLocation()` (`0x09680470`) returns a view only when the
point falls inside that rectangle and bit 31 is set. The original ViewPanel
collection is y-ordered and uses a binary-search shortcut before rectangle
checks.

The reconstructed host now carries source-level equivalents of those geometry
values without claiming N32 ABI layout.

## View movement

The original exported movement entry point is:

```
0x096a2f3c ImdViewPanelMoveView(Widget, ImdView &, long, long)
```

It obtains the adopted layout, calls the layout `moveView(view,x,y)` virtual,
and on success invalidates/rebuilds ViewPanel layout state before returning
true.

The `ImdIconLayout` override is:

```
0x0969d1fc ImdIconLayout::moveView(ImdView &, long, long)
```

The current portable ViewPanel now exposes the same source-level movement seam;
layout-specific placement/persistence is still pending.

## ViewPanel drag scrolling

The original exports:

```
0x096a335c ImdViewPanelDragMotion(Widget, short, short)
0x096a35a4 ImdViewPanelDragFinished(Widget)
```

`ImdViewPanelDragMotion()` implements edge-triggered auto-scroll with a 20-pixel
step and stores active horizontal/vertical drag scroll state in the ViewPanel.
`ImdViewPanelDragFinished()` cancels the outstanding drag-scroll timeout/state.

## Reconstruction boundary

Do not replace the original architecture with a desktop-only pointer-drag
implementation. The remaining integration should preserve this chain:

```
ViewPanel dragStartCallback
  -> ImdIconPanel::dragStart()
  -> ImdDragManager::dragStart()
  -> Motif/SGI drag context
  -> ViewPanel/layout movement/drop handling
  -> ImdModelBasedIconPanel::dragEnd()
```

The exact local-move/drop branch inside `ImdDragManager::dropProc()` and the
model-backed persistence path are the next binary targets before enabling full
interactive movement.
