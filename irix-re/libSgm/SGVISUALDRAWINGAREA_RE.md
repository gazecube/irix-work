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

The object at `0x0b254310` is consistent with a Motif base-class extension record. Its first fields are:

```
+0x00  0
+0x04  0
+0x08  2
+0x0c  0x78
```

The extension is exactly 0x78 bytes long. Most fields are zero; a single inherited/runtime hook pointer is present near the tail. The exact Motif private type still needs to be matched against the corresponding IRIX Motif ABI before naming individual extension slots.

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
+0x10c  ancestor/shell-like Widget pointer used by WM colormap management
+0x110  Boolean-like private policy/state loaded with XtGetValues during realize
+0x114  Window * saved installed-colormap list
+0x118  int saved installed-colormap count
+0x11c  Boolean-like installed/state flag
+0x120  String-like dither color subresource
```

The descriptions for `+0x10c`, `+0x110`, and `+0x11c` are intentionally conservative until their resource names/private helper semantics are fully recovered.

## Initialize

Class `initialize` is at `0x0b22a5b0`.

Resolved behavior from disassembly and GOT entries:

- clears byte `+0x11c` immediately.
- reads `ditherBackground` at `+0x109`.
- reads the `Visual *` at `+0x104`.
- may clear `ditherBackground` when the selected Visual does not meet the expected visual-class/depth conditions.
- calls `XtGetSubresources()` with one private subresource whose instance offset is `+0x120`.
- the `+0x120` value is passed to `XAllocNamedColor()` as a color-name string.
- geometry/color comparisons are then made against the widget's current colormap/background state.
- if a replacement dither background is required, calls `XAllocColor()` and then `XmGetPixmapByDepth()`.
- the literal pixmap name passed to `XmGetPixmapByDepth()` is exactly `"50_foreground"`.
- the returned pixmap is stored in the widget's core background-pixmap field.
- when pixmap creation succeeds, `ditherBackground` is set back to 1.

Resolved external calls used by this path include:

```
XtGetSubresources
XAllocNamedColor
XAllocColor
XmGetPixmapByDepth
```

This proves `ditherBackground` is an actual SGI/Motif dither construction path rather than a passive flag.

## Realize

Class `realize` is at `0x0b1c3264`.

Resolved behavior:

- forces nonzero width and height before creating the window.
- sets window-attribute bits before creation.
- calls `XtCreateWindow()` directly, passing the configured `Visual *` at `+0x104`.
- walks ancestors to find/store a shell-like parent at `+0x10c`.
- calls `XtGetValues()` for one Boolean-like resource and stores it at `+0x110`.
- if `installColormap` at `+0x108` is true, calls `SgVisualDrawingAreaInstallColormap(this)`.

This is materially different from calling the `XmDrawingArea` superclass realize method.

## Destroy

Class `destroy` is at `0x0b1c7ff8`.

Resolved behavior is very small and specific:

- tests byte `+0x11c`.
- when set, calls `SgVisualDrawingAreaUninstallColormap(this)`.
- otherwise returns immediately.

No separate ownership cleanup is performed by the class destroy entry point beyond the uninstall path.

## Colormap installation

`SgVisualDrawingAreaInstallColormap` at `0x0b1c7d20` is a substantial 728-byte routine.

Resolved Xlib/Xt call targets:

```
XGetWMColormapWindows
XSetWMColormapWindows
XtMalloc
XtFree
XFree
XListInstalledColormaps
XInstallColormap
```

Binary-proven behavior:

- requires private `+0x10c` to be non-NULL.
- uses the realized widget window and the stored ancestor/shell window.
- calls `XGetWMColormapWindows()` on the ancestor/shell window.
- searches the returned list for the widget's own window and avoids duplicate insertion.
- when insertion is required, allocates a new `(count + 1)` window array with `XtMalloc()`.
- inserts the widget window at element 0 and copies the old list after it.
- calls `XSetWMColormapWindows()` with the updated list.
- frees temporary Xlib memory with `XFree()` and Xt memory with `XtFree()` as appropriate.
- if private byte `+0x110` is set, calls `XListInstalledColormaps()` for the widget window and stores its returned array at `+0x114` and count at `+0x118`.
- then calls `XInstallColormap()` on the widget's colormap.
- sets byte `+0x11c` to 1 before returning.

Thus the original routine manages both ICCCM `WM_COLORMAP_WINDOWS` membership and, conditionally, the server's currently installed colormap set.

## Colormap uninstallation

`SgVisualDrawingAreaUninstallColormap` at `0x0b1c3368` is the inverse path.

Resolved behavior:

- requires `+0x10c` to be non-NULL.
- calls `XGetWMColormapWindows()` for the stored ancestor/shell window.
- searches for this widget's window.
- when found, compacts the list in place to remove it.
- calls `XSetWMColormapWindows()` with `count - 1`.
- frees the Xlib-owned WM list with `XFree()`.
- if private `+0x110` is set, iterates the saved colormap array at `+0x114` for `+0x118` entries and calls `XInstallColormap()` for each saved colormap.
- frees `+0x114` with `XFree()` after restoring the saved installed-colormap set.
- clears byte `+0x11c` before returning.

This confirms `+0x114/+0x118` are saved server-installed colormaps used only to restore pre-install state.

## Constructor

`SgCreateVisualDrawingArea` at `0x0b22a7b8` is a thin wrapper around the Xt widget creation API using `sgVisualDrawingAreaWidgetClass`.

No extra constructor policy should be invented in the public creation function.

## Current reconstruction warning

The initial portable implementation that copied the host `XmDrawingAreaClassRec` and appended only the three resources is not binary-faithful and is expected to be unsafe. The original class has additional private instance state, its own direct `XtCreateWindow` realize path, a Motif class extension, a dither-background subresource path, and substantial colormap-window/server-colormap management.

Do not treat the current portable class as complete until the private `+0x110` resource is named, the class extension is matched against the IRIX Motif ABI, and the host-side class record is rebuilt from the actual recovered contract rather than a mutated superclass copy.
