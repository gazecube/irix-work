# Imd FTR visual selection reconstruction

Primary evidence: IRIX 6.5.0 `libimdFtrIcon.so`, n32 MIPS-III, unstripped.

This note records the binary-proven behavior used by Clogin/Pandora when it
selects the visual, colormap, and depth passed to the Imd icon panel.

## Entry points

Original `libimdFtrIcon.so` symbols:

```text
0x0980c7f0  _imdFtrSetVisualInfo
0x0980c8b4  internalChooseVisual        (static name recovered from debug data)
0x0980cbe8  imdFtrChooseVisual
0x0980cc8c  imdFtrChooseVisualArgs
0x0980ce9c  imdFtrChooseXdbeVisual
0x0981299c  imdFtrGetVisualType
0x09812c74  imdFtrInitializeIconVisuals
```

Additional static source names recovered from SGI debug strings include:

```text
createCmap
SetDoubleBufferVisual
findOverlayInfo
allocMultiHeadInfo
internalChooseVisual
```

The relevant original source units were named:

```text
initVisual.c
chooseVisual.c
```

## Clogin call sites

Both recovered Clogin call sites pass visual type `2` to
`imdFtrChooseVisualArgs()`.

The recovered host signature is:

```c
Boolean imdFtrChooseVisualArgs(Widget widget,
                               int visualType,
                               ArgList args,
                               Cardinal *count,
                               int *auxVisualValue);
```

The original semantic typedef/name of the fifth argument has not yet been
recovered. Clogin supplies storage for it but does not use the value afterward.

## imdFtrChooseVisualArgs

`imdFtrChooseVisualArgs()` first finds the containing shell. The original uses
`_XtCheckSubclassFlag(widget, 0x20)` and repeatedly calls `XtParent()` until the
required shell relationship is reached.

It then fetches exactly three Xt resources:

```text
visual
colormap
depth
```

The SGI fast resource IDs in the machine code are:

```text
-39  visual
-33  colormap
-34  depth
```

If the returned `Visual *` is NULL, the original calls `XtScreen()` and uses the
screen root visual. It does not reject a zero/None colormap or depth in the
wrapper. Those values are passed to `internalChooseVisual()`.

On success, the wrapper appends exactly three `Arg` entries in this order:

```text
visual
colormap
depth
```

The caller-owned Arg count is incremented after each entry.

## internalChooseVisual

`internalChooseVisual()` begins by copying the supplied visual/colormap/depth to
its output locations and clearing the auxiliary output.

Before using the SGI registry it verifies that the input visual ID equals the
screen root visual ID. A mismatch returns False.

The function keeps a per-screen cache. Each screen cache record is 24 bytes on
n32:

```text
+0x00  pointer to the 20-word registry data
+0x04  cached resolved Visual * for slot 0
+0x08  cached resolved Visual * for slot 1
+0x0c  cached resolved Visual * for slot 2
+0x10  cached resolved Visual * for slot 3
+0x14  cached resolved Visual * for slot 4
```

The registry itself contains five records of four 32-bit words each:

```text
record +0x00  VisualID
record +0x04  Colormap
record +0x08  depth
record +0x0c  auxiliary visual value
```

The requested `visualType` is multiplied by 16 to select one record.

For a nonzero VisualID, `internalChooseVisual()` resolves it with:

```c
XGetVisualInfo(display,
               VisualIDMask | VisualScreenMask,
               &template,
               &count);
```

The returned `XVisualInfo.visual` pointer is cached in the corresponding
per-screen slot. The selected record's colormap, depth, and auxiliary word are
returned directly.

## Fast atom 0x85 and registry property

Both `internalChooseVisual()` and `imdFtrInitializeIconVisuals()` use:

```text
_XSGIFastInternAtom(display, 0x85, ...)
```

The literal property name represented by fast atom ID `0x85` has not yet been
recovered. Do not assign a guessed name to it.

The property is required to have:

```text
type    XA_INTEGER (19)
format  32
nitems  20
```

This is an SGI-specific icon visual registry. It is not
`SERVER_OVERLAY_VISUALS`: the four words are consumed as VisualID, Colormap,
depth, and an auxiliary result, not as the standard overlay-property tuple.

## imdFtrInitializeIconVisuals

The initializer first probes fast atom `0x85` and reads the root-window
property. If a valid INTEGER/32/20-word property already exists, it leaves it
alone.

Otherwise it clears an 80-byte five-record table and examines the X server.
The SGI-specific setup path is gated by an exact 16-byte vendor comparison
against:

```text
Silicon Graphics
```

Two additional atom names are recovered directly from the binary data:

```text
HW_RGB
CMAP_GROUP_SIZE
```

The initializer calls:

```c
XInternAtoms(display, names, 2, True, atoms);
```

with those names.

The visual-building path additionally requires the root visual class to be
`PseudoColor` (X visual class value 3).

It then probes `TrueColor` visuals with `XMatchVisualInfo()` in this order of
construction:

```text
8-bit TrueColor
24-bit TrueColor
```

The candidates are also checked against SGI new-multibuffering support through
`sgiXnmbxGetVisualCapabilities()` and related helper code.

## Five registry slots

The initializer zeroes all five records first. The currently binary-proven
construction behavior is:

```text
slot 0  left zero in this initialization path
slot 1  8-bit TrueColor visual/colormap/depth
slot 2  preferred TrueColor visual
        initially slot-1-compatible 8-bit TrueColor
        replaced by accepted 24-bit TrueColor when available
slot 3  left zero in this initialization path
slot 4  filled by the double-buffer visual helper when an acceptable DBE/
        SGI multibuffer visual is found
```

The original enum constant names for slots 0 through 4 have not yet been
recovered. Use numeric slot names until they are proven.

Because Clogin requests slot 2, its semantic requirement is the preferred
TrueColor visual.

## Portable compatibility consequence

A modern X server normally uses a TrueColor root visual and does not publish
this SGI registry. The original SGI registry-building path is specifically for
the historical mixed PseudoColor/TrueColor workstation setup.

For the portable Clogin probe, preserving the shell's already-valid TrueColor
visual/colormap/depth tuple is therefore the compatibility fallback when the
SGI registry is unavailable. This behavior is isolated in
`ImdFtrVisualCompat.c++` so the exact registry implementation can replace it
without changing Pandora callers.
