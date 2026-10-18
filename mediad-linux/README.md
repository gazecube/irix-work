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
