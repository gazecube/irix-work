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
