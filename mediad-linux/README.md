# mediad-linux

IRIX 6.5.7m `mediad` (from `irix-657m-src/eoe/cmd/mediad`) ported to Linux, plus
an IRIX device-namespace layer (`irix-device-ecosystem/`). Initial port by Codex;
`FM-FINDINGS.md` is its notes.

Transport: TCP on 127.0.0.1, registered with rpcbind as program 391017 v1, as on
IRIX, so the unmodified IRIX `libmediaclient` (used by SaRemovableMediaClass,
which feeds the desktop's drive icons) can find it.

Run (needs the `optical` group for /dev/sr*; state dir path must be short):

    irix-device-ecosystem/bin/irix-devices --state $S/ids --root $S/root --quiet
    MEDIAD_STATE=$S MEDIAD_MODULE_DIR=$PWD/mediad-linux/build IRIXDEV_STATE=$S/ids \
      IRIXDEV_ROOT=$S/root mediad-linux/build/mediad-linux -f -a -l 7
    mediad-linux/build/media-client      # dumps the monitor state as JSON

## Desktop drive icons: the original chain

`Xsession.dt` runs `sabgicons | bgicons -icondb $sysdesktopdir/SysadmBgIconDB
-icondir $sysdesktopicondir`.  With the runtime fixes in this tree the
original IRIX binaries do this against mediad-linux:

    mediad-linux  <--(portmapper 391017, XDR)--  libmediaclient (IRIX original)
        SaRemovableMediaClass.so  ->  sabgicons  ->  bgicons  ->  Icons/ + SysadmBgIconDB

`sabgicons` exits on stdin EOF (session-lifetime guard): keep its stdin open
(`tail -f /dev/null | sabgicons | bgicons ...`).  Needs, in the local-only
IRIX-TREE: `usr/lib32/imdModels/libSaRemovableMediaModel.so` ->
`../../sysadm/adminclass/SaRemovableMediaClass.so` (the model factory lives in
the admin class DSO) and `usr/sysadm/bin/runpriv` replaced by
`desktop-integration/native/runpriv-shim` (no setuid root here).
Run with `IRIXDEV_ROOT=<irix-devices staging root>` so the guest sees
/dev/{dsk,rdsk,scsi}.

Open: the *running native fm* (static recompilation) cannot dlopen
SaRemovableMediaClass.so ("IRIX N32 dynamic loading is unavailable"), so it
shows an error dialog per icon.  The DSO (and its closure: libmediaclient,
libsaremote, libSaModel, libsaAdminObj, ...) must be part of fm's recompiled
bundle, or the runtime needs a real N32 dlopen.
