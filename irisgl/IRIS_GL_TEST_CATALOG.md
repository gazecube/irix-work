# IRIS GL stress-test catalog

Source corpus: `irix/cmd/stress/Gfx/` from the IRIX 6.5.7m source tree.

The SGI Makefile builds these tests against `-lgl -lm` (and `worms` also against `-lgutil`):

- bounce
- flash
- winset
- badifred
- worms
- mode
- cbuf
- rbuf
- gsync
- rectwrite
- parallel

## Initial bring-up ordering

### 1. flash

Smallest useful RGB/window/event test found so far.

IRIS GL surface:

- prefsize / prefposition
- foreground
- winopen
- RGBmode
- gconfig
- RGBcolor
- clear
- qtest
- qread
- REDRAW
- reshapeviewport

Target: first unmodified SGI program to run.

### 2. mode

Adds runtime color-mode reconfiguration.

Additional surface:

- mapcolor
- cmode
- color

Behavior of interest: alternates `cmode()` and `RGBmode()`, committing each through `gconfig()`.

### 3. badifred

Adds indexed-color double buffering.

Additional surface:

- doublebuffer
- swapbuffers

### 4. rbuf

Exercises runtime single/double-buffer reconfiguration in RGB mode.

Additional surface:

- singlebuffer

### 5. cbuf

Exercises runtime single/double-buffer reconfiguration in color-index mode.

### Later focused tests

- `gsync`: explicit graphics synchronization
- `rectwrite`: indexed-color raster transfer
- `winset`: multiple GL windows and current-window/context switching

### Later broad tests

- `worms`: matrices, objects, primitives, input, flush behavior
- `bounce`: RGB, double buffering, Z buffer, matrices, lighting, materials, polygons/meshes, input, objects
- `parallel`: `sproc`, shared-context behavior, `winset`, `gversion`, multi-process graphics ownership

## Rule

Do not invent an implementation layer merely because it is historically plausible. Promote an API or subsystem into the implementation when an original SGI source, header, binary, test, or observed IRIX behavior requires it.
