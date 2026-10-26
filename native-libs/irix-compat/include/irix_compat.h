/*
 * Force-included when building IRIX userland sources natively on Linux.
 * It supplies the few IRIX-only names the sources expect from system headers.
 * The IRIX sources themselves are compiled unmodified.
 */
#ifndef IRIX_COMPAT_H
#define IRIX_COMPAT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <paths.h>
#include <sys/types.h>

#ifndef _PATH_CONFIG
#define _PATH_CONFIG	"/etc/config"	/* IRIX <paths.h> */
#endif
#ifndef MNTMAXSTR
#define MNTMAXSTR	128		/* IRIX <mntent.h> */
#endif
#ifndef MNTOPT_RAW
#define MNTOPT_RAW	"raw"		/* IRIX <mntent.h> */
#endif

/*
 * IRIX statvfs64() reports the filesystem type name in f_basetype; Linux
 * reports a magic number.  Wrap it so sources using the IRIX field build.
 */
#include <sys/statvfs.h>
#include <sys/statfs.h>
#include <linux/magic.h>

struct irix_statvfs64 {
	unsigned long f_flag;
	char f_basetype[16];
};

static inline int irix_statvfs64(const char *path, struct irix_statvfs64 *sp)
{
	struct statvfs64 v;
	struct statfs f;
	const char *name = "unknown";

	if (statvfs64(path, &v) < 0 || statfs(path, &f) < 0)
		return -1;
	switch ((unsigned long)f.f_type) {
	case 0x58465342: name = "xfs"; break;		/* XFS_SUPER_MAGIC */
	case 0xEF53: name = "ext4"; break;
	case 0x9123683E: name = "btrfs"; break;
	case 0x01021994: name = "tmpfs"; break;
	case 0x6969: name = "nfs"; break;
	case 0x9660: name = "iso9660"; break;
	case 0x4d44: name = "vfat"; break;
	case 0x9fa0: name = "proc"; break;
	case 0x62656572: name = "sysfs"; break;
	case 0x1373: name = "devfs"; break;
	}
	sp->f_flag = v.f_flag;
	strncpy(sp->f_basetype, name, sizeof(sp->f_basetype) - 1);
	sp->f_basetype[sizeof(sp->f_basetype) - 1] = 0;
	return 0;
}
#define statvfs64 irix_statvfs64

#endif
