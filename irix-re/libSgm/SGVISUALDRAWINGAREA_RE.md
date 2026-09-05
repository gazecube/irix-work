# SgVisualDrawingArea reverse engineering notes

Target: IRIX 6.5.0 `libSgm.so.1`, n32 MIPS, debug info present.

This file records only binary-backed findings. Descriptive names are used only where the Xt/Motif ABI makes a class-record slot unambiguous.

## Exported symbols

```
0x0b1c3368  572  SgVisualDrawingAreaUninstallColormap
0x0b1c7d20  728  SgVisualDrawingAreaInstallColormap
0x0b22a7b8   72  SgCreateVisualDrawingArea
0x0b254388  208  sgVisualDrawingAreaClassRec
0x0b254458    4  sgVisualDrawingAreaWidgetClass
```

`sgVisualDrawingAreaWidgetClass` contains the address of `sgVisualDrawingAreaClassRec`.

## Inheritance and class record

`sgVisualDrawingAreaClassRec + 0x00` has an `R_MIPS_REL32` relocation against `xmDrawingAreaClassRec`.

Therefore the original superclass is exactly `XmDrawingArea`.

The original n32 class record is 208 bytes. The beginning of the record maps cleanly onto the Xt `CoreClassPart` ABI:

```
+0x00  superclass            xmDrawingAreaClassRec
+0x04  class_name            "SgVisualDrawingArea"
+0x08  widget_size           0x124
+0x0c  class_initialize      NULL
+0x10  class_part_initialize NULL
+0x14  class_inited          0
+0x18  initialize            0x0b22a5b0
+0x1c  initialize_hook       NULL
+0x20  realize               0x0b1c3264
+0x24  actions               NULL
+0x28  num_actions           0
+0x2c  resources             0x0b2542b8
+0x30  num_resources         3
+0x34  xrm_class             0
+0x38  compress flags        0x01000100 as packed Xt fields
+0x3c  destroy               0x0b1c7ff8
+0x40  resize                _XtInherit
+0x44  expose                _XtInherit
+0x48  set_values            NULL
+0x4c  set_values_hook       NULL
+0x50  set_values_almost     _XtInherit
+0x54  get_values_hook       NULL
+0x58  accept_focus          NULL
+0x5c  version               0x00012afe
+0x60  callback_private      NULL
+0x64  tm_table              _XtInheritTranslations
+0x68  query_geometry        _XtInherit
+0x6c  display_accelerator   NULL
+0x70  extension             0x0b254310
```

The remaining class parts inherit from `XmDrawingArea` except where later binary work proves otherwise.

The object at `0x0b254310` is consistent with an Xt/Motif class extension record. It begins:

```
+0x00  0
+0x04  0
+0x08  2
+0x0c  0x78
```

The exact extension type and all hooks still need to be decoded before claiming binary compatibility.

## Resource table

The class has exactly three resources. The resource table begins at `0x0b2542b8`.

Binary-backed instance offsets:

```
+0x104  Visual *
+0x108  Boolean installColormap
+0x109  Boolean ditherBackground
```

Literal resource strings present in the binary:

```
installColormap
InstallColormap
ditherBackground
DitherBackground
SgVisualDrawingArea
```

The first resource uses shared Xt/Motif quark strings; its runtime use at `+0x104` proves it is a `Visual *` resource.

## Private instance state

The widget is not just an `XmDrawingArea` plus the three resources. The original `widget_size` is `0x124`, and methods directly access private state after the public resources:

```
+0x104  Visual * resource
+0x108  installColormap resource
+0x109  ditherBackground resource
+0x10c  Widget-like pointer used by colormap install/uninstall paths
+0x110  Boolean-like state
+0x114  pointer to Window array / colormap-window bookkeeping
+0x118  integer count
+0x11c  Boolean-like installed/state flag
+0x120  pointer used by initialize/dither path
```

The semantic names for `+0x10c` through `+0x120` are not fully proven yet. Do not rename them as final fields until call targets and data flow are resolved.

## Initialize

Class `initialize` is at `0x0b22a5b0`.

Proven behavior from disassembly:

- clears byte `+0x11c` immediately.
- reads `ditherBackground` at `+0x109`.
- reads the `Visual *` at `+0x104`.
- may clear `ditherBackground` when the visual does not meet the expected visual-class conditions.
- inspects the widget colormap/background-related core fields.
- uses private pointer `+0x120` during the dither/background setup path.
- may set `ditherBackground` back to 1 after successfully constructing replacement background state.

The exact called Xlib/Xt routines must be resolved from the GOT before reproducing this path.

## Realize

Class `realize` is at `0x0b1c3264`.

Proven behavior:

- forces nonzero width and height before creating the window.
- calls an Xt window-creation routine using the configured `Visual *` at `+0x104`.
- walks ancestors to find a shell-like parent and stores it at private `+0x10c`.
- initializes additional colormap bookkeeping beginning at `+0x110`.
- if `installColormap` at `+0x108` is true, calls `SgVisualDrawingAreaInstallColormap(this)`.

This is materially different from simply calling the `XmDrawingArea` superclass realize method.

## Destroy

Class `destroy` is at `0x0b1c7ff8`.

Proven behavior:

- tests byte `+0x11c`.
- when set, calls an internal cleanup routine.
- returns without invoking an explicitly stored subclass destroy hook.

The cleanup routine and exact ownership of `+0x114/+0x118/+0x120` remain to be resolved.

## Colormap installation

`SgVisualDrawingAreaInstallColormap` at `0x0b1c7d20` is a substantial 728-byte routine, not a simple `XInstallColormap` wrapper.

Proven behavior includes:

- requires private `+0x10c` to be non-NULL.
- obtains and edits a WM colormap-window list associated with the shell/window hierarchy.
- avoids duplicate widget windows.
- allocates a new list when insertion is required.
- stores bookkeeping at `+0x114` and `+0x118`.
- sets byte `+0x11c` on successful installation/update.

`SgVisualDrawingAreaUninstallColormap` at `0x0b1c3368` performs the inverse operation:

- obtains the current WM colormap-window list.
- removes this widget's window if present.
- updates the shell's list.
- frees temporary/list storage.
- clears `+0x11c`.

The exact Xlib calls will be named after GOT resolution.

## Constructor

`SgCreateVisualDrawingArea` at `0x0b22a7b8` is a thin wrapper around the Xt widget creation API using `sgVisualDrawingAreaWidgetClass`.

No extra constructor policy should be invented in the public creation function.

## Current reconstruction warning

The initial portable implementation that copied the host `XmDrawingAreaClassRec` and appended only the three resources is not binary-faithful and is expected to be unsafe. The original class has additional private instance state, its own realize path, a nontrivial class extension, and substantial colormap-window management.

Do not treat the current portable class as complete until the remaining call targets, extension record, and private state semantics are recovered.
