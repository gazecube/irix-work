# `flash.c` bring-up target

Primary source target: SGI `irix/cmd/stress/Gfx/flash.c`.

Observed call order in the original source:

1. `prefposition(...)` or `prefsize(100, 100)`
2. `foreground()`
3. `winopen(...)`
4. `RGBmode()`
5. `gconfig()`
6. event polling with `qtest()` / `qread()`
7. on `REDRAW`, call `reshapeviewport()`
8. `RGBcolor(0, 0, 255)` then `clear()`
9. delay
10. `RGBcolor(255, 0, 0)` then `clear()`
11. delay
12. repeat

This file must remain unmodified as the acceptance target.

Before implementing any of these entry points, recover their exact declarations, public types, constants, and any available binary/internal evidence.
