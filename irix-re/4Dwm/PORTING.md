# 4Dwm port

Original binary: /usr/bin/X11/4Dwm (IRIX 6.5, MIPS N32, stripped)

This directory is the source-recovery target for SGI 4Dwm. The binary exports a very large named function surface, making traditional decompilation practical despite stripping.

Binary-backed entrypoint facts:
- `main` exported at 0x1002a974.
- `main` initializes global WM state with `InitWmGlobal(argc, argv, envp)`, then enters the X event loop.
- Event dispatch is split among `WmDispatchMenuEvent`, `WmDispatchClientEvent`, and `WmDispatchWsEvent`.
- Exported subsystems include client adoption/frame management, workspace/room management, icon box handling, focus, menus, resources, timers, placement, stacking and SGI DESKS protocol support.
- Config path `~/.4Dwmrc` is embedded in the binary.
- The default configuration contains MWM-style actions including `f.menu`, `f.exec`, `f.quit_mwm`, and `f.separator`.
- SGI protocols include the `_SGI_DESKS_*` atom family, `_SGI_TELL_WM`, `_SGI_ICON_HINTS`, `_SGI_ROOTPAINTER_ID`, `_SGI_DESKS_BACKGROUND*`, and `_SGI_WM_OVERLAY`.

Direct dependencies include libimdXutil, libimdUtil, libimdFtrIcon, libXm, libXmu, libXext, libXt and libX11.

Port rule: recover 4Dwm as the actual SGI window manager architecture. Do not substitute another WM or wrap a modern WM.

Next binary pass: split the exported symbol surface into source-unit clusters, recover global WM structures from field offsets, then land `InitWmGlobal`, event dispatch and client adoption as the first runnable core.
