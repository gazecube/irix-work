# 4Dwm upstream reconstruction baseline

The IRIX 6.5 N32 `/usr/bin/X11/4Dwm` binary contains the version string:

```
SGI LOOK -- Based on OSF/Motif 1.2.4#sgi1.0
```

That makes OSF/Motif 1.2.4 `mwm` the primary source baseline for recovering 4Dwm. The baseline is not treated as an implementation substitute: the IRIX 6.5 binary remains authoritative for every function, data structure, resource, protocol and behavior.

A pinned Motif source checkout is attached as the `upstream-motif` submodule. The pin is `thentenaar/motif` commit `21da9648d8df2690f7c00dcb6dcaf2068ab5dabc`. Its `clients/mwm/` tree preserves the classic Motif 1.2.x source-unit architecture and multiple units identify themselves as Motif Release 1.2.4. The source is LGPL-2.1-or-later; retain its original copyright/license headers whenever code is carried into a recovered source unit.

Recovery rule:

1. Match an IRIX exported symbol against the upstream mwm source.
2. Compare the IRIX MIPS implementation/control flow, constants, calls and data accesses against the mwm baseline.
3. Classify the result as `MWM-BASE`, `MWM-MODIFIED`, or `SGI-NEW` only after evidence supports it.
4. Copy/port baseline code only for behavior that survives the binary comparison.
5. Recover SGI changes from the IRIX binary; SGI behavior always wins over the baseline.
6. Keep host-specific Linux code at the X11/OS portability seam rather than changing 4Dwm semantics.

The first directly verified shared architecture is `main`: IRIX calls `InitWmGlobal`, consumes events with Xt, preprocesses active-menu events through `WmDispatchMenuEvent`, splits workspace/root events through `WmDispatchWsEvent`, client events through `WmDispatchClientEvent`, and only then calls `XtDispatchEvent`. This matches the classic mwm main-loop architecture while the IRIX binary contains additional SGI state handling.

`tools/extract_exports.sh` produces the complete exported-text inventory from the original N32 executable. `tools/classify_symbols.py` compares that inventory against `upstream-motif/clients/mwm` without claiming a machine-code match from a source-name match alone.
