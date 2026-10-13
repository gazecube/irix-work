# IRIX xdm as the display manager

`irix-xdm.service` runs the original, recompiled IRIX `xdm` (Clogin greeter)
as root on the real X server, using `/var/X11/xdm/xdm-config.real`
(`Xservers.real`: Xorg on `vt2`, access control off).

Nothing here is installed or enabled automatically.

    sudo install -m644 desktop-integration/xdm/irix-xdm.service /etc/systemd/system/
    sudo loginctl enable-linger $USER      # keeps /run/user/UID for host apps
    # from a text console (Ctrl+Alt+F3), NOT from inside the graphical session:
    sudo systemctl stop ly@tty2
    sudo systemctl start irix-xdm

Fall back to Ly at any time:

    sudo systemctl stop irix-xdm
    sudo systemctl start ly@tty2

Facts worth knowing:
- xdm does not create a logind/PAM session; the session handoff
  (`irix-xdm-session`) supplies `XDG_RUNTIME_DIR`/D-Bus for host apps.
- Xorg picks up `/etc/X11/xorg.conf.d` (e.g. the Zaphod/radeon config).
- The login program is `desktop-integration/xsession/irix-xdm-login`.
