# FM startup architecture reverse-engineering notes

Targets: original IRIX `/usr/sbin/fm` constructors:

```text
ImdBackWindow::ImdBackWindow(char const *, WidgetRec *)
FmWindow::FmWindow(char const *, char const *, FmApp *, bool, Arg *, unsigned int)
```

## Startup split

`FmApp::processArgs()` is the central startup/RPC ingress.

The two primary window paths are:

```text
-b/background mode
    -> singleton ImdBackWindow

normal directory mode
    -> FmWindow
```

## ImdBackWindow construction

The exact constructor symbol is:

```text
__ct__13ImdBackWindowGPCcP10_WidgetRec
```

The component name passed by `FmApp::processArgs()` is exactly:

```text
backWindow
```

The constructor creates or wires the following major components directly:

```text
VkComponent base
FmMenuActions
QuickFindDialog
HostFindDialog
ImdViewPanel
ImdInvertedTeeRenderer
ImdIconLayout
BackPanel
BackLayout
ImdFileIconTarget
BackWindowRefTarget
ImdOldFileIconTarget
BackWindowOldRefTarget
FmMenuHandler
```

It also acquires models through the IID broker layer using:

```text
ImdBroker::contextBroker()
ImdRefModel::desktopContainerDNA()
ImdRefModel::overlayContainerDNA()
ImdBroker::getModel()
ImdBroker::releaseModel()
```

The desktop and overlay container paths are materialized on disk with `mkdirp()` when needed before their models are attached.

## Desktop view construction

The desktop/background view is built around `ImdViewPanel`, with an `ImdInvertedTeeRenderer` and layout implementation attached through:

```text
ImdViewPanelAdoptRenderer()
ImdViewPanelAdoptLayout()
```

The constructor begins view change blocking while models, callbacks and layout are connected, then restores presentation state and ends the block.

This indicates that desktop population is intentionally staged to avoid intermediate redraw/model-change churn.

## Desktop integration properties

The `ImdBackWindow` constructor publishes both SGI desktop window properties directly with `setWindowAsProperty()`.

Exact property names:

```text
_SGI_DESKS_DESKTOP_WINDOW
_SGI_DESKS_BACKGROUND_WINDOW
```

The first publishes the desktop shell/window used by legacy `telldesktop` discovery.

The second publishes the actual background view window.

This means the desktop registration contract is established during `ImdBackWindow` construction itself.

The legacy control path therefore becomes:

```text
FmApp -b
    -> ImdBackWindow("backWindow", ...)
    -> setWindowAsProperty(root,
                           desktopWindow,
                           "_SGI_DESKS_DESKTOP_WINDOW")
    -> setWindowAsProperty(root,
                           backgroundWindow,
                           "_SGI_DESKS_BACKGROUND_WINDOW")
    -> propertyChangeEH receives
       _SGI_DESKS_DESKTOP_COMMAND
```

## Other constructor behavior

Binary-proven constructor operations also include:

```text
QuickFindDialog creation
HostFindDialog creation
X atom interning
popup shell creation/realization
background window lowering/mapping
selection-holder setup in FmMenuActions
model callback registration
drag/drop target construction
session path creation
input-only helper widget construction
cursor resource/color setup
FmMenuHandler construction
presentation state restoration
```

The constructor installs event handling after the desktop windows and menu/model machinery are created, so IPC and root-window behavior are part of the background window object's lifecycle.

## FmWindow construction

The normal directory-window constructor symbol is:

```text
__ct__8FmWindowGPCcT1P5FmAppbP3ArgUi
```

The observed call from `FmApp::processArgs()` is consistent with:

```c
FmWindow("FileManager",
         directory,
         app,
         alternateMode,
         args,
         argCount);
```

The constructor starts with a `VkWindow` base, creates its `ImdString`/`ImdDNA` state and `FmMenuActions`, then creates its menu panes and resolves named menu items before calling:

```text
FmWindow::_createPanels()
```

This cleanly separates the window shell/menu layer from the directory/model/view panel construction that follows.

## Reimplementation boundary

The Linux FM should preserve this startup decomposition:

```text
FmApp
  |
  +-> background singleton
  |     -> ImdBackWindow-compatible desktop core
  |     -> desktop + overlay models
  |     -> background panel/layout
  |     -> SGI root properties
  |     -> legacy desktop command receiver
  |
  `-> ordinary directory window
        -> FmWindow-compatible shell
        -> menu/action layer
        -> panel/model creation
        -> presentation state
```

This gives the full FM reconstruction a stable object/lifecycle boundary before deeper icon, layout and file-operation work.
