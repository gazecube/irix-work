# mediad-linux

IRIX 6.5.7m `mediad` (from `irix-657m-src/eoe/cmd/mediad`) ported to Linux, plus
an IRIX device-namespace layer (`irix-device-ecosystem/`). Initial port by Codex;
`FM-FINDINGS.md` is its notes.

Transport: TCP on 127.0.0.1, registered with rpcbind as program 391017 v1, as on
IRIX, so the unmodified IRIX `libmediaclient` (used by SaRemovableMediaClass,
which feeds the desktop's drive icons) can find it.

Install (native IRIX paths; dry run by default):

    mediad-linux/install-mediad.sh            # show what would change
    mediad-linux/install-mediad.sh --apply    # sudo -n; writes a manifest
    sudo systemctl enable --now irix-devices mediad

This installs `/usr/etc/mediad`, `/usr/lib/devicelib/{dev_linux,fmt_iso,fmt_udf}.so`,
the **original** `/etc/init.d/mediad` and `/etc/config/mediad{,.options}` from
the IRIX tree, a small host `/etc/chkconfig` stand-in, the `irix-devices` tool
and two systemd units (`mediad.service` just runs `/etc/init.d/mediad
start|stop`).  `irix-devices --host-dev` publishes `/dev/dsk`, `/dev/rdsk` and
`/dev/scsi` aliases.  mediad runs as root and mounts discs at `/<device name>`
(e.g. `/CDROM2`); `MEDIAD_STATE=<dir>` still redirects its files for testing.

UDF discs are currently reported as `iso9660` (spoof; set MEDIAD_REPORT_UDF=1
for the real `udf` format, which needs `devices/udf.ftr` compiled into
devices.otr).  The desktop icon chain (`sabgicons | bgicons`, `genDeviceIcons |
bgicons`) is started by `irix-session` (irix-re/recomp/tools/irix-session.sh.in).
