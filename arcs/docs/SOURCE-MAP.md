# ARCS-UEFI source map

This file tracks provenance and reuse status for source considered by the port.

Classifications:

- `SGI-reusable` - SGI standalone source suitable for direct porting after header/provenance review.
- `SGI-reference-only` - SGI source used as behavioral/ABI reference but not copied into the port.
- `AT&T-do-not-touch` - AT&T/USL-origin implementation. Do not copy, adapt, transliterate, or use as an implementation basis.
- `machine-specific-replace` - hardware-specific code that must be replaced by a UEFI/x86-64 backend.
- `uncertain-origin-review-first` - provenance must be resolved before use.

## Initial GUI set

| Source | Status | Port role |
| --- | --- | --- |
| `stand/arcs/lib/libsc/gui/background.c` | SGI-reusable | Preserve background/calligraphy/company rendering logic; replace only hardware primitives underneath it. |
| `stand/arcs/lib/libsc/gui/core.c` | SGI-reusable, pending per-file header review | Preserve GUI object/event core. |
| `stand/arcs/lib/libsc/gui/button.c` | SGI-reusable, pending per-file header review | Preserve button widget behavior and rendering. |
| `stand/arcs/lib/libsc/gui/canvas.c` | SGI-reusable, pending per-file header review | Preserve canvas widget behavior. |
| `stand/arcs/lib/libsc/gui/dialog.c` | SGI-reusable, pending per-file header review | Preserve dialog behavior. |
| `stand/arcs/lib/libsc/gui/pcbm.c` | SGI-reusable | Preserved unchanged in `arcs/sgisrc/libsc/gui/pcbm.c`; port hardware primitives below it. |
| `stand/arcs/lib/libsc/gui/progress.c` | SGI-reusable, pending per-file header review | Preserve progress widget. |
| `stand/arcs/lib/libsc/gui/radio.c` | SGI-reusable, pending per-file header review | Preserve radio widget. |
| `stand/arcs/lib/libsc/gui/text.c` | SGI-reusable | Preserved unchanged in `arcs/sgisrc/libsc/gui/text.c`; depends on SGI textport/font path. |
| `stand/arcs/lib/libsc/gui/tfield.c` | SGI-reusable, pending per-file header review | Preserve text field behavior. |
| `stand/arcs/include/guicore.h` | SGI-reusable | Preserved unchanged in `arcs/sgisrc/include/guicore.h`; defines the graphics primitive indirection used by the GUI. |
| `stand/arcs/include/style.h` | SGI-reusable | Preserved unchanged in `arcs/sgisrc/include/style.h`; canonical standalone GUI layout/color indices. |
| `stand/arcs/include/stand_htport.h` | SGI-reusable, pending complete provenance review | Defines `htp_fncs`, textport state, palette constants, and GUI/textport interfaces. |

## Backend boundary

The SGI GUI calls through `gfxgui.fncs`. ARCS-UEFI should implement a UEFI-backed `htp_fncs` rather than rewriting the GUI renderer.

Initial backend primitives include `blankscreen`, `color`, `mapcolor`, `sboxfi`, `cmov2i`, `pnt2i`, and `drawbitmap`, plus the remaining members required by the exact `htp_fncs` definition.

The implementation beneath these primitives is reconstructed ARCS-UEFI code using UEFI GOP and UEFI input services.

## Current transplant status

The `arcs-uefi` branch now contains the first directly transplanted SGI GUI files under `arcs/sgisrc/` plus a separate reconstructed UEFI backend area under `arcs/uefi/`. The next import is the exact `stand_htport.h` contract and the remaining GUI files needed to compile `initGfxGui()` and `drawBackground()` without rewriting them.
