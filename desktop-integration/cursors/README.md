# X default cursors in 4Dwm's pointer red

4Dwm draws its pointer as the standard X `left_ptr` with `*pointerColor: red` (red fill,
white outline).  Programs that load cursors from an Xcursor theme (GTK, Qt, Chromium,
Firefox) know nothing about that, so this makes a theme with the same images.

    ./mkxcursors-pcf.py IRIX-TREE/usr/lib/X11/fonts/misc/cursor.pcf ~/.icons/x-red [fg-hex] [bg-hex]

It reads IRIX's own cursor font (Xsgi's cursor.pcf) directly, without an X server, draws
each of the 77 X shapes red on white, and links the freedesktop/CSS names (default, text,
pointer, wait, ...) to the nearest shape.  The real X shape names are never replaced by
aliases.  Measured on 2026-11-03: the Xsgi font and X.org's built-in cursor font have identical
glyphs for all the shapes compared.

`~/.xinitrc` selects it for the IRIX session (XCURSOR_THEME=x-red and ~/.icons/default
inheriting x-red); the Mex session keeps the mex theme.  IRIX programs run through the
runtime create core cursors themselves (IrixX11BridgeExtra.c bridge_XCreateFontCursor), so
they are unaffected.

Testing note: a shell started from a session inherits that session's XCURSOR_THEME.  Run
cursor tests with `env -u XCURSOR_THEME -u XCURSOR_PATH ...` or the result is the session's
theme, not the cursor under test.
