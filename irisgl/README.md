# IRIS GL reconstruction

This subtree is for reconstruction of SGI IRIS GL from surviving SGI evidence.

Primary bring-up target: unmodified `irix/cmd/stress/Gfx/flash.c` from the IRIX 6.5.7m source tree.

Policy:

- original SGI source, binaries, headers, tests, and observed behavior are primary evidence
- preserve original names and semantics where evidence exists
- do not implement IRIS GL as an OpenGL compatibility wrapper
- modern host/window/device code belongs only at the platform boundary
- add functionality in the order demanded by original SGI tests

Initial `flash.c` dependency slice:

- `prefsize`
- `prefposition`
- `foreground`
- `winopen`
- `RGBmode`
- `gconfig`
- `RGBcolor`
- `clear`
- `qtest`
- `qread`
- `REDRAW`
- `reshapeviewport`

The first milestone is to compile and run SGI's original `flash.c` unchanged against the reconstructed library.
