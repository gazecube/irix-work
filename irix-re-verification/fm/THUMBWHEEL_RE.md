# FM scaleZoom thumbwheel reverse-engineering notes

Target fm specimen:

```text
irix-re/evidence/fm
SHA-256 65430bf4937027314b1a940b1e7f109d01f3dc48275223216360e3b50a809818
```

Target Sgm specimen:

```text
preserved IRIX libSgm.so.1
ELF32 big-endian MIPS N32
```

The recovered FileManager resources define `scaleZoom` as a vertical scaling
control with minimum 60, maximum 500, value 100, homePosition 100,
showValue false, and traversalOn false.

## Public SGI header

The recovered `ThumbWheel.h` is the original Silicon Graphics public header.
It defines the public widget types, `sgThumbWheelWidgetClass`,
`SgCreateThumbWheel()`, and the exact callback payload:

```c
typedef struct {
    int reason;
    XEvent *event;
    int value;
} SgThumbWheelCallbackStruct;
```

The public Sg-specific resources are:

```text
homePosition
angleRange
unitsPerRotation
animate
showHomeButton
```

The obsolete `lowerBound` / `upperBound` spellings alias the Motif
`minimum` / `maximum` resources.

## Binary-proven public entry points

The preserved `libSgm.so.1` exports:

```text
SgCreateThumbWheel      0x0b20f890 size 96
SgThumbWheelGetValue    0x0b2100c4 size 12
SgThumbWheelSetValue    0x0b2100d0 size 220
sgThumbWheelClassRec    0x0b24430c size 154
sgThumbWheelWidgetClass 0x0b2443a8 size 4
```

It also contains the source/header names:

```text
ThumbWheel.c
./Sgm/ThumbWheelP.h
./Sgm/ThumbWheel.h
```

`SgThumbWheelGetValue()` is fully recovered. It reads the 32-bit value at
original n32 widget offset `+0xcc` and stores it through the caller's output
pointer.

`SgThumbWheelSetValue()` first compares the requested value against n32 offset
`+0xcc`. If unchanged it returns immediately. If changed, it stores the new
value, recomputes private drawing state and, when the widget is realized,
redraws and performs a direction-sensitive update. The recovered body stores a
recomputed private value at n32 offset `+0xf4`.

`SgCreateThumbWheel()` is a thin wrapper around the normal Xt widget creation
path. It passes the exported `sgThumbWheelWidgetClass` together with parent,
name, ArgList and Arg count; there is no separate wrapper object.

## Class record

The raw n32 class record gives these CoreClassPart facts directly:

```text
widget_size                 0x150
initialize                  0x0b1a65b4
realize                     0x0b1a72d0
actions                     0x0b244264
num_actions                 21
resources                   0x0b243f20
num_resources               11
compress_motion             1
compress_exposure           2
compress_enterleave         1
visible_interest            0
destroy                     0x0b1a7324
resize                      0x0b1a6c24
expose                      0x0b1a4720
set_values                  0x0b1a5614
query_geometry              0x0b1a6b8c
```

The 11-resource count is especially useful. Together with the public header
and callback behavior, it strongly constrains the resource table to the
standard value/range/orientation/callback resources plus the five public
ThumbWheel-specific resources. The exact resource-table offsets/defaults still
need to be decoded before they are called binary-proven.

The four compression bytes appear in the class record as `01 02 01 00`, which
maps cleanly onto the CoreClassPart compression fields.

## Loader-paradox correction

The earlier attempt to `dlopen()` MaXX's `libSgm.so.1` beside the reconstructed
library is not a valid long-term implementation. The reconstructed library has
the same SONAME and already exports ThumbWheel placeholder symbols, so dynamic
loader resolution can return the already-loaded reconstruction instead of a
second historical Sgm implementation.

The correct path is therefore to replace the reconstructed
`sgThumbWheelClassRec` placeholder with a real native ThumbWheel class and let
`fm` use that through its normal reconstructed libSgm dependency.

## Next binary work

Before claiming the missing private header has been reconstructed, decode:

1. the 11-entry XtResource table at `0x0b243f20`;
2. the 21-entry action table at `0x0b244264` and its translation table;
3. the six ThumbWheel-owned Core method bodies above;
4. all original instance offsets touched by those methods.

Those facts are enough to write a host-native `ThumbWheelP.h` compatibility
layout without inventing original private member names where the binary does
not reveal them.
