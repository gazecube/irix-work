# MFBX - multi-framebuffer X

A fork of X.org's xserver with SGI-style overlay/popup/underlay planes built in,
so the IRIX desktop (4Dwm, Motif, IRIS GL apps) sees real
`SERVER_OVERLAY_VISUALS` instead of per-app shape/window workarounds.

- `xserver/` - shallow clone of xorg-server tag `xorg-server-21.1.25` (what Arch
  ships). Not tracked by this repo (see .gitignore); recreate with
  `git clone --depth 1 --branch xorg-server-21.1.25 https://gitlab.freedesktop.org/xorg/xserver.git MFBX/xserver`.
  Our changes live on a branch inside that clone and are exported as patches into
  `MFBX/patches/` so this repo carries them.

## What the tree already has
`fb/fboverlay.c` + `fboverlay.h` (`fbOverlay*`): a two-layer framebuffer (up to
`FB_OVERLAY_MAX` = 2) with per-layer visuals/colormaps and a transparent key.
Nothing in-tree uses it any more - no driver scans out two layers.

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

## Status (2026-10-22)
- Branch `mfbx` in `xserver/` holds our changes; `MFBX/patches/` has them as
  `git format-patch` output (apply on top of tag xorg-server-21.1.25).
- `hw/xmfb/` (built as `Xmfb`, a copy of Xvfb): 24-bit image plane + 8-bit
  PseudoColor overlay plane on `fbOverlay`, chroma-key composite (overlay pixel
  0 is transparent) into the Xvfb framebuffer file, per-plane colormaps, overlay
  colormap pixel 0 reserved.
- Needed one change outside xmfb: the Composite extension forced every child
  window onto its parent's pixmap (composite/compwindow.c, compCreateWindow);
  it now only does that for pixmaps of the same depth.
- Build: `meson setup MFBX/build -Dxorg=true -Dxephyr=true -Dglamor=true`
  from `MFBX/xserver`, then `ninja -C MFBX/build hw/xmfb/Xmfb`.
- Test: `Xmfb :9 -screen 0 1280x1024x24 -fbdir DIR -ac`, then a client that
  maps a depth 24 window and a depth 8 PseudoColor window; read the composite
  from `DIR/Xvfb_screen0` (xwd format).

- Nested window: `Xmfb :9 -screen 0 1280x1024x24 -host :0 [-host-title T]
  [-host-pos X Y]`. Framebuffer in SysV shm shown through MIT-SHM at 50 Hz;
  host keyboard (evdev keycodes) and pointer forwarded; cursor is composited
  above every plane (a hardware-sprite stand-in), host cursor hidden.
- Gotchas found: pointer axes must be absolute with range = screen size; the
  stock software cursor is hidden by overlay windows, hence our own.

## Not done yet
- Transparency (needs layer-aware clipping in mi/mivaltree.c, see below),
  popup/underlay planes, SERVER_OVERLAY_VISUALS root property, GetImage on the root window
  (crashed once in the release build, not reproduced under ASan), compositing
  on the GPU, nested-window output, running the IRIX session on it.

## Planes (current design)
Four planes, bottom to top: underlay, image, overlay, popup (layer numbers 0..3,
also the z-order). Image is depth 24 / 32bpp; the other three are 8-bit
PseudoColor, each with its own visual, pixmap and colormap, and pixel 0
transparent (reserved: clients cannot allocate it). A window lives in the plane
of its visual.
- `mi/mivaltree.c`: with `miNumLayers > 1` windows only obscure windows of their
  own layer (siblings and children), so the image plane keeps its contents under
  overlay windows; overlay pixel 0 shows the image through. Hook:
  `miWindowLayerProc`. Stock behaviour when `miNumLayers == 1`.
- `composite/compwindow.c`: do not force a child onto its parent's pixmap when
  the depths differ.
- `hw/xmfb/InitOutput.c`: plane setup (replaces fbOverlay), per-plane colormaps,
  layer-aware pointer hit test (higher plane wins), `SERVER_OVERLAY_VISUALS`
  published on the root ({visual, 1, 0, 1} overlay, {.., 1, 0, 2} popup,
  {.., 0, 0, -1} underlay), composite with the cursor on top.
- Underlay: visible where the image plane pixel is 0xff00ff (magenta) - an
  approximation until the IRIX behaviour is known.
- Test client: MFBX/tests/planes.c (see Test below).

## Open
- Run the IRIX session on it (4Dwm crashed the server once: investigate).
- Composite redirect is not supported with planes.
- GPU compositing in VRAM.
