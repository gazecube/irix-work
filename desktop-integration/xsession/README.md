# IRIX desktop as a real X session

`irix-desktop.desktop` + `irix-desktop-session` start the IRIX desktop directly
from a display manager (Ly, GDM, SDDM, ...). 4Dwm manages the real screen.

Install (needs root):

    sudo install -m755 irix-desktop-session /usr/local/libexec/irix-desktop-session
    sudo install -m644 irix-desktop.desktop /usr/share/xsessions/irix-desktop.desktop

Choose "IRIX Desktop" at the login screen. Output goes to
`~/.cache/irix-session.log`.

Note: `../dtlogin/irix-session.desktop` is the CDE dtlogin-import template. It
runs `dtlogin-desktop-session` against itself, so it must not be used as a
display-manager session entry (it re-executes itself forever).

## Log Out

The stock Toolchest entry runs the guest `endsession` shell script through the
generic runner, which starts the wrong program and depends on the guest shell.
The session launcher now provides `irix-session --end [-f]` instead: confirm
(IRIX `xconfirm`, falling back to `xmessage`), `tellwm end_session`, then
SIGUSR1 to the running session script (via
`$XDG_RUNTIME_DIR/irix-session-<display>.pid`), which stops 4Dwm, Toolchest
and fm so the X session ends.

The runtime tree's `usr/lib/X11/system.chestrc` has to call it (this file is
in the untracked IRIX-TREE, so it is a local edit):

    "Log Out"  f.checkexec.sh "$IRIX_SESSION_SCRIPT --end"

`IRIX_SESSION_SCRIPT` is exported by the launcher.
