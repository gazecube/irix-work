# MFBX — multi-framebuffer X

A fork of X.org's xserver with SGI-style overlay/popup/underlay planes built in,
so the IRIX desktop (4Dwm, Motif, IRIS GL apps) sees real
`SERVER_OVERLAY_VISUALS` instead of per-app shape/window workarounds.

- `xserver/` — shallow clone of xorg-server tag `xorg-server-21.1.25` (what Arch
  ships). Not tracked by this repo (see .gitignore); recreate with
  `git clone --depth 1 --branch xorg-server-21.1.25 https://gitlab.freedesktop.org/xorg/xserver.git MFBX/xserver`.
  Our changes live on a branch inside that clone and are exported as patches into
  `MFBX/patches/` so this repo carries them.

## What the tree already has
`fb/fboverlay.c` + `fboverlay.h` (`fbOverlay*`): a two-layer framebuffer (up to
`FB_OVERLAY_MAX` = 2) with per-layer visuals/colormaps and a transparent key.
Nothing in-tree uses it any more — no driver scans out two layers.

## Plan
1. Phase 1: virtual driver (dummy-style) on `fbOverlay`, nested window output.
   Prove visuals, transparency (index 0) and per-plane colormaps.
2. Phase 2: layers live as GPU textures (glamor); composite at scanout. (The
   "layers in VRAM" idea.)
3. Phase 3: run as the real Xorg for the IRIX session; publish
   `SERVER_OVERLAY_VISUALS` from the server; drop the bridge's overlay hacks
   (IRIX_GL_PUBLISH_OVERLAY, IRIX_GL_OVERLAY_SHAPE).
4. Later: more than two planes (popup + overlay + underlay) if FB_OVERLAY_MAX
   needs raising.

## Terms
SGI planes: image (main), overlay, popup, underlay; overlay planes are 8-bit
PseudoColor with their own colormap and one transparent pixel value.
