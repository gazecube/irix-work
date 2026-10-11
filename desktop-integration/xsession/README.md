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

## Toolchest menu commands, Log Out and host apps

Toolchest runs every `f.checkexec`/`f.exec` command as
`/usr/lib/desktop/waitmap /bin/sh -c "<command>"`. Two problems followed:

* the original `waitmap` traps under the guest runner before it runs the
  command, so **no** menu command ever started; and
* the guest process passes an empty environment.

`waitmap-passthrough.c` replaces it with a helper that imports the parent's
environment from `/proc/<ppid>/environ` and execs its arguments. It and the
native Log Out (`irix-endsession.c`: IRIX `xconfirm`, falling back to
`xmessage`, then `tellwm end_session` and SIGUSR1 to the session script via
`$XDG_RUNTIME_DIR/irix-session-<display>.pid`) are built by the normal CMake
build into the runtime root (`usr/lib/desktop/waitmap`,
`usr/bin/X11/irix-endsession`).

Local, untracked edits this relies on:

* `IRIX-TREE/usr/lib/X11/system.chestrc`:
  `"Log Out"  f.checkexec.sh "$IRIX_RECOMP_ROOT/usr/bin/X11/irix-endsession"`
* `~/.auxchestrc` for host apps. Use **`f.exec.sh`**, not `f.checkexec`:
  checkexec only runs a command it can find in the guest tree, and host
  programs are not there:

      Menu HostApps
      {
          "Alacritty"  f.exec.sh "alacritty"
          "Firefox"    f.exec.sh "firefox"
      }
      Menu ToolChest
      {
          no-label     f.separator
          "Host Apps"  f.menu HostApps
      }

## Claude runner

`irix-claude` launches `claude-desktop` and fixes its window for 4Dwm:
removes the Motif "no decorations" hint (so 4Dwm draws a real titlebar) and
mirrors `_NET_WM_NAME` into `WM_NAME` (4Dwm ignores the former). Add to
`~/.auxchestrc`:

    "Claude" f.exec.sh "/path/to/desktop-integration/xsession/irix-claude"
