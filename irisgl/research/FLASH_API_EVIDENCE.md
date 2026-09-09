# IRIS GL flash.c API evidence

This file records only evidence recovered from surviving SGI documentation and SGI source consumers. It is intentionally not a guessed replacement header.

Target program: `irix/cmd/stress/Gfx/flash.c`

## Verified C specifications

### prefsize

IRIX 5.3 manual page:

```c
void prefsize(x, y)
long x, y;
```

Semantics: specifies the preferred width and height in pixels for the next graphics window. `winopen` activates the constraint.

Source: https://techpubs.jurassic.nl/manpages_0530/cat3/standard/prefsize.html

### winopen

IRIX 5.3 manual page:

```c
long winopen(name)
String name;
```

The returned value is the graphics server/window identifier. Only the lower 16 bits are significant because a graphics window identifier is the value portion of a REDRAW queue entry. Returns -1 if no additional windows are available.

Source: https://techpubs.jurassic.nl/manpages_0530/cat3/standard/winopen.html

An older SGI GL2 manual gives the equivalent historical declaration:

```c
long winopen(name)
char name[];
```

Source: https://www.typewritten.org/Manual/SGI/GL2/W2.5/man3g/winopen.html

### gconfig

IRIX 5.3 manual page:

```c
void gconfig(void)
```

`gconfig` commits requested GL framebuffer configuration changes. The manual explicitly lists `RGBmode`, `cmode`, `doublebuffer`, `singlebuffer`, and related requests as requiring `gconfig` before they take effect.

It also states that `gconfig` resets the current color in each draw mode to zero and resets each draw mode writemask to the available bitplanes. In double-buffer mode it enables the back buffer and disables the front buffer.

Source: https://techpubs.jurassic.nl/manpages_0530/cat3/standard/gconfig.html

### RGBcolor

IRIX 5.3 manual page:

```c
void RGBcolor(red, green, blue)
short red, green, blue;
```

The color values are 0..255. Values above 255 are clamped; negative values are documented as unpredictable. Calling `RGBcolor` while the current framebuffer is in color-map mode is an error.

Source: https://techpubs.jurassic.nl/manpages_0530/cat3/standard/rgbcolor.html

### qtest

IRIX 5.3 manual page:

```c
long qtest(void)
```

Returns zero when the event queue is empty, otherwise returns the device number of the first queued entry without removing it.

Source: https://techpubs.jurassic.nl/manpages_0530/cat3/standard/qtest.html

### qread

IRIX 5.3 manual page:

```c
long qread(data)
short *data;
```

Returns the device identifier of the first queued event, stores its value through `data`, and removes it. If the queue is empty, `qread` blocks until an event is available.

Source: https://techpubs.jurassic.nl/manpages_0530/cat3/standard/qread.html

### reshapeviewport

IRIX 5.3 manual page:

```c
void reshapeviewport(void)
```

The manual defines it as equivalent to:

```c
long xsize, ysize;
getsize(&xsize, &ysize);
viewport(0, xsize-1, 0, ysize-1);
```

It is explicitly intended to be called when REDRAW events are received for resizable windows.

Source: https://techpubs.jurassic.nl/manpages_0530/cat3/standard/reshapeviewport.html

## Verified behavior without final C declaration copied yet

### RGBmode

Surviving SGI documentation states that `RGBmode` requests RGB display mode and does not take effect until `gconfig` is called.

Source: https://www.typewritten.org/Manual/SGI/GL2/W2.5/man3g/RGBmode.html

Do not finalize its public prototype in reconstructed `gl.h` until the actual IRIX header or an IRIX-era C specification is recovered.

### foreground

IRIX 5.3 documentation states that `foreground` must be called before `winopen` to keep the graphical process in the foreground rather than allowing `winopen` to background it.

Source: https://techpubs.jurassic.nl/manpages_0530/cat3/pas/foreground.html

Do not finalize its public C prototype until the corresponding C man page or actual header is recovered.

### clear

IRIX 5.3 documentation states that `clear` fills the current viewport with the current color in the currently active framebuffer/draw mode. Current pattern, writemask, and screen mask affect it; several other GL tests such as z buffering are ignored.

Source: https://techpubs.jurassic.nl/manpages_0530/cat3/pas/clear.html

Do not finalize its public C prototype until the actual C specification or header is recovered.

## Still unresolved from device.h

- exact declaration/typedef of `Device`
- exact numeric value of `REDRAW`
- any public event-related typedefs/macros required by `qread`/`qtest`

The SGI `promgl.c` consumer confirms that REDRAW is explicitly queued with `qdevice(REDRAW)` in real SGI code, but the constant value must come from the public header or another authoritative binary/header source.

## Current rule

No reconstructed public header should be committed from this evidence alone. The next step is to recover an authentic IRIX `gl.h` and `device.h` from installed-system material, development packages, archived headers, or original binaries/debug data. This file exists to prevent already-verified signatures and semantics from being lost while that search continues.
