# ewmhd: EWMH for 4Dwm

4Dwm has no EWMH support, so fullscreen requests and the drag requests of
frameless apps (Steam, Discord, Chromium/Electron/GTK windows with their own title
bars) are ignored.  `ewmhd` runs beside 4Dwm and answers them using only what 4Dwm
already understands; it changes nothing in 4Dwm.

- `_NET_WM_STATE_FULLSCREEN` (add/remove/toggle, and the state set before mapping):
  the decorations are dropped through `_MOTIF_WM_HINTS`, the window is sized to the
  screen and raised; leaving restores hints, size and place.
- `_NET_WM_MOVERESIZE`: move and the eight resize directions, driven by a pointer
  grab; Escape cancels; the drag ends on button release.
- `_NET_SUPPORTED` / `_NET_SUPPORTING_WM_CHECK` so clients know to ask.

One instance per X screen: `ewmhd` (uses `$DISPLAY`'s screen), e.g.
`DISPLAY=:1.0 ewmhd &` and `DISPLAY=:1.1 ewmhd &` on a two-screen server.

    sh build.sh        # builds ewmhd and test-ewmhd (needs libX11, libXtst)
    sh run-test.sh     # private Xvfb + the real 4Dwm + ewmhd + test client

Not implemented: `_NET_ACTIVE_WINDOW`, maximise states, `_NET_CLOSE_WINDOW`,
keyboard-driven moves, per-monitor fullscreen (each X screen is one monitor).
