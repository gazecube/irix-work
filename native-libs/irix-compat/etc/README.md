# rc2.d for the Linux port (staged, not installed)

An IRIX system gets its /etc/rc2.d/S##name symlinks from package installation
(`inst`); the runtime tree has none.  The original names and numbers are
recorded in the IRIX source tree (S00announce ... S99roboinst).  This directory
holds the subset that is safe to run **alongside Linux** while OpenRC is still
the real init:

    S00announce    boot banner
    S58rmtmpfiles  clean /tmp, /var/tmp
    S88configmsg   configuration messages
    S98xdm         the graphical login (xdm / Xsgi)

Held back until the IRIX init replaces OpenRC (each fights Linux or needs a
Linux-correct version first):

    S04usr S12filesystems S14quotas   mount/quota logic written for EFS+xfs
    S30network S32aliases-ip          would reconfigure Linux networking
    S49swap                            swap -a with IRIX swap(1M)
    S48savecore                        IRIX crash dumps
    S22acct S35audit S75cron S50mail   duplicate Linux services
    S20sysetup S23autoconfig S99roboinst  IRIX system setup / kernel autoconfig
    others (pcp, rtmond, grio, fcagent, snmp, ...)  not meaningful here

Install by copying the symlinks into the target's /etc/rc2.d; nothing installs
them automatically.

    S97gpu         load radeon/nouveau right before xdm (see init.d/gpu)
