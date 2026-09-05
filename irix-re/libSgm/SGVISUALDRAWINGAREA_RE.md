# SgVisualDrawingArea reverse engineering notes

Target: IRIX 6.5.0 `libSgm.so.1`, n32 MIPS, debug info present.

This file records binary-backed findings. Descriptive names are used only where the Xt/Motif ABI or matching SGI documentation makes the meaning unambiguous.

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

The original n32 class record is 208 bytes. The Xt/Motif class-part layout is:

```
+0x000 CoreClassPart
+0x074 CompositeClassPart
+0x088 ConstraintClassPart
+0x0a4 XmManagerClassPart
+0x0c0 XmDrawingAreaClassPart
+0x0c4 SgVisualDrawingAreaClassPart, 12 bytes, all zero
```

The recovered CoreClassPart is:

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
+0x38  packed flags          01 00 01 00
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

The Composite class methods are inherited. The Constraint class part is zero. The Manager class part has inherited translations and an inherited extension pointer; its other class-specific entries are zero. The DrawingArea class-part extension is zero. The final 12-byte Sg class part is also entirely zero, so no Sg-specific virtual class methods exist here.

## Motif base-class extension

The object at `0x0b254310` is an `XmBaseClassExtRec` version 2 and is exactly `0x78` bytes on n32.

Recovered contents:

```
next_extension          NULL
record_type             NULLQUARK
version                 2
record_size             0x78
initializePrehook       NULL
setValuesPrehook        NULL
initializePosthook      NULL
setValuesPosthook       NULL
secondaryObjectClass    NULL
secondaryObjectCreate   NULL
getSecResData           NULL
flags[32]               all zero
getValuesPrehook        NULL
getValuesPosthook       NULL
classPartInitPrehook    NULL
classPartInitPosthook   NULL
ext_resources           NULL
compiled_ext_resources  NULL
num_ext_resources       0
use_sub_resources       False
widgetNavigable         _XtInherit
focusChange             NULL
wrapperData             NULL
```

The `_XtInherit` relocation is at extension offset `+0x6c`, which is the `widgetNavigable` slot in the Motif v2 ABI.

## Public resource table

The class has exactly three public resources. The table begins at `0x0b2542b8`.

```
+0x104  XmNvisual             XmCVisual             Visual *   CopyFromParent
+0x108  SgNinstallColormap    SgCInstallColormap    Boolean    False
+0x109  SgNditherBackground   SgCDitherBackground   Boolean    False
```

The latter two names/classes occur literally in the binary. The first resource uses the SGI/Xt fast-quark representation; runtime use plus SGI's public `SgVisualDrawingArea(3X)` documentation identifies it as `XmNvisual`.

## Private instance state

`XmDrawingAreaRec` ends at n32 offset `+0x104`. The Sg instance part continues through `+0x123`:

```
+0x104  Visual * visual
+0x108  Boolean installColormap
+0x109  Boolean ditherBackground
+0x10c  Widget colormapShell
+0x110  Boolean shellOverrideRedirect
+0x114  Colormap *savedInstalledColormaps
+0x118  int savedInstalledColormapCount
+0x11c  Boolean colormapInstalled
+0x120  String requestedBackgroundName
```

`+0x110` is identified from both code and documentation: `Realize` calls `XtGetValues()` on the containing shell for `XtNoverrideRedirect`, and install/uninstall use this byte to decide whether server-installed colormaps must be saved/restored directly.

The one-entry private XtResource table used by `Initialize` is at `0x0b254460` and has:

```
size          4
instance off  0x120
default type  XtRImmediate
default       NULL
```

Its fast-quark name corresponds to the Core background resource and its converted type is `String`; the result is passed directly to `XAllocNamedColor()`. This preserves the original requested background color text alongside Core's converted Pixel value.

## Initialize

Class `Initialize` is at `0x0b22a5b0`.

Exact recovered flow:

1. Clear `colormapInstalled` at `+0x11c`.
2. If `ditherBackground` is false, return.
3. Clear `ditherBackground` before attempting construction.
4. Require a non-CopyFromParent Visual whose class is `TrueColor` or `StaticColor`.
5. Require Core `background_pixmap == XmUNSPECIFIED_PIXMAP`.
6. Use `XtGetSubresources()` to retrieve the requested background color as a string into `+0x120`.
7. Call `XAllocNamedColor()` in the widget colormap, producing allocated and exact colors.
8. If the allocated pixel already equals Core `background_pixel` and all allocated RGB components equal the exact RGB components, return without dithering.
9. For each RGB component compute:

```
complement = clamp((2 * exact) - allocated, 0, 65535)
```

10. Set `DoRed | DoGreen | DoBlue` and call `XAllocColor()` for the complement.
11. If the complement pixel equals the originally allocated pixel, return.
12. Call:

```
XmGetPixmapByDepth(screen,
                   "50_foreground",
                   complement.pixel,
                   allocated.pixel,
                   depth)
```

13. Store the returned pixmap in Core `background_pixmap`.
14. Set `ditherBackground` back to true on success.

The local helper implementing the component formula is named `complementingColorComponent` in the debug strings.

## Realize

Class `Realize` is at `0x0b1c3264`.

Exact recovered behavior:

- force width to 1 if zero.
- force height to 1 if zero.
- set `attributes->bit_gravity = NorthWestGravity`.
- set `attributes->do_not_propagate_mask` to:

```
KeyPressMask |
KeyReleaseMask |
ButtonPressMask |
ButtonReleaseMask |
PointerMotionMask
```

- OR `CWBitGravity | CWDontPropagate` into the create-window value mask.
- call `XtCreateWindow()` directly with the configured `Visual *`.
- walk ancestors until the containing shell is found and store it at `+0x10c`.
- call `XtGetValues(shell, XtNoverrideRedirect, ...)` and store the result at `+0x110`.
- if `installColormap` is true, call `SgVisualDrawingAreaInstallColormap(this)`.

This is materially different from calling the `XmDrawingArea` superclass realize method.

## Destroy

Class `Destroy` is at `0x0b1c7ff8`.

It only tests `colormapInstalled` at `+0x11c`. If true, it calls `SgVisualDrawingAreaUninstallColormap(this)`. Otherwise it returns.

## SgVisualDrawingAreaInstallColormap

Address: `0x0b1c7d20`, size 728 bytes.

Resolved external calls:

```
XGetWMColormapWindows
XSetWMColormapWindows
XtMalloc
XtFree
XFree
XListInstalledColormaps
XInstallColormap
```

Recovered behavior:

- require stored shell `+0x10c`.
- obtain display, widget window, and shell window.
- call `XGetWMColormapWindows()` on the shell.
- if the property exists, allocate a new `(count + 1)` array with `XtMalloc()`, prepend the widget window, append the old list, and call `XSetWMColormapWindows()` with `count + 1`.
- free the temporary Xt array with `XtFree()` and the Xlib-owned old array with `XFree()`.
- if no WM list exists, create a two-element local list containing the widget window followed by the shell window and install that property.
- if `shellOverrideRedirect` is true, call `XListInstalledColormaps()` for the widget window, store the returned array/count at `+0x114/+0x118`, then call `XInstallColormap()` for the widget colormap.
- set `colormapInstalled` true.

The routine does not contain the duplicate-window suppression that an earlier reconstruction assumed; the binary simply prepends the widget window when the property already exists.

## SgVisualDrawingAreaUninstallColormap

Address: `0x0b1c3368`, size 572 bytes.

Recovered behavior:

- require stored shell `+0x10c`.
- call `XGetWMColormapWindows()` on the shell.
- search for the widget window.
- when found, compact the array in place and call `XSetWMColormapWindows()` with `count - 1`.
- free the Xlib-owned WM list with `XFree()`.
- if `shellOverrideRedirect` is true, reinstall every colormap saved at `+0x114` for `+0x118` entries using `XInstallColormap()`.
- free the saved array with `XFree()`.
- clear `colormapInstalled`.

This is the IRIX 6.5.0 behavior. Older SGI documentation describes direct uninstall in more general terms, but the target binary preserves/restores the server's previously installed colormap set instead.

## Constructor

`SgCreateVisualDrawingArea` at `0x0b22a7b8` is a thin `XtCreateWidget()` wrapper using `sgVisualDrawingAreaWidgetClass`. There is no additional constructor policy.

## Reconstruction status

The original crashy implementation built a subclass by copying the host's live `XmDrawingAreaClassRec` with `memcpy()` and mutating the copy. That approach has been removed.

The current reconstruction uses:

- a statically defined subclass class record;
- the recovered zero 12-byte Sg class part;
- a native `XmBaseClassExtRec` version 2 matching the original extension semantics;
- the recovered three public resources;
- the complete private instance state used by the original routines;
- the direct `XtCreateWindow()` realize path and exact extra X attributes;
- `XtNoverrideRedirect` shell handling;
- the recovered WM colormap-window and server-colormap save/restore behavior;
- the recovered TrueColor/StaticColor `50_foreground` dither path.

This is the first Sgm widget intended to be behaviorally usable rather than merely symbol-compatible. The remaining validation step is execution against the MaXX/OpenMotif runtime through the Pandora probe.
