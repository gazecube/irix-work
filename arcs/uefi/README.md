# UEFI backend

This directory contains reconstructed ARCS-UEFI platform code. It is not SGI source.

The port boundary is the original standalone `struct htp_fncs` interface. The SGI GUI renderer remains above that interface; this directory supplies UEFI GOP and input implementations beneath it.

First graphics milestone:

1. instantiate an `htp_state` backed by GOP;
2. implement the original `htp_fncs` primitive set;
3. load original SGI font/logo data into the `htp_state`;
4. call original `initGfxGui()` and `drawBackground()`;
5. only then port the higher-level maintenance menu widgets.

Do not copy hardware-specific SGI graphics drivers here. Their role is replaced by the UEFI backend.
