# XDG sessions in CDE dtlogin

`dtlogin` does not parse freedesktop `.desktop` session files itself. It does,
however, load alternative-session resources from
`/etc/dt/config/C/Xresources.d`. These tools bridge the two formats while
preserving CDE's built-in Current and Home entries:

- `import-desktop-sessions.py` creates one `Xresources.d` resource file per
  visible `.desktop` file found in the configured X session directories.
- `desktop-session-launcher.py` reads the selected entry's `Exec` command and
  replaces the CDE login process with that command.
- `irix-session.desktop` registers the current development IRIX session. Its
  Exec path should be updated when the runner is installed under `/opt/IMD`.

The importer only removes files bearing its `7000.xdg-session-` prefix. Run it
again after adding or removing XDG sessions, then restart `dtlogin` so it reloads
the X resources. The active CachyOS login manager on the development host is
Ly, which already reads `/usr/share/xsessions/*.desktop` directly.
