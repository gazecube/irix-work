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
#include <errno.h>
#include <sys/syscall.h>

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

/* Same layout as include/uapi/linux/irix_statvfs.h in the irix-on-gentoo kernel. */
struct irix_statvfs64 {
	unsigned long long f_bsize, f_frsize, f_blocks, f_bfree, f_bavail;
	unsigned long long f_files, f_ffree, f_favail, f_fsid;
	char f_basetype[16];
	unsigned long long f_flag, f_namemax;
	char f_fstr[32];
	unsigned long long f_filler[16];
};

#define IRIX_SYS_statvfs64	1000	/* the kernel's IRIX syscall block */
#define IRIX_ST_RDONLY	0x01

/* Stock-kernel fallback: derive f_basetype from the statfs magic number. */
static inline int irix_statvfs64_fallback(const char *path, struct irix_statvfs64 *sp)
{
	struct statvfs64 v;
	struct statfs f;
	const char *name = "unknown";

	if (statvfs64(path, &v) < 0 || statfs(path, &f) < 0)
		return -1;
	switch ((unsigned long)f.f_type) {
	case 0x58465342: name = "xfs"; break;
	case 0xEF53: name = "ext4"; break;
	case 0x9123683E: name = "btrfs"; break;
	case 0x01021994: name = "tmpfs"; break;
	case 0x6969: name = "nfs"; break;
	case 0x9660: name = "iso9660"; break;
	case 0x4d44: name = "vfat"; break;
	case 0x9fa0: name = "proc"; break;
	case 0x62656572: name = "sysfs"; break;
	}
	memset(sp, 0, sizeof(*sp));
	sp->f_bsize = v.f_bsize;
	sp->f_frsize = v.f_frsize;
	sp->f_blocks = v.f_blocks;
	sp->f_bfree = v.f_bfree;
	sp->f_bavail = v.f_bavail;
	sp->f_files = v.f_files;
	sp->f_ffree = v.f_ffree;
	sp->f_favail = v.f_favail;
	sp->f_fsid = v.f_fsid;
	sp->f_namemax = v.f_namemax;
	sp->f_flag = (v.f_flag & ST_RDONLY) ? IRIX_ST_RDONLY : 0;
	strncpy(sp->f_basetype, name, sizeof(sp->f_basetype) - 1);
	return 0;
}

static inline int irix_statvfs64(const char *path, struct irix_statvfs64 *sp)
{
	long r = syscall(IRIX_SYS_statvfs64, path, sp);

	if (r < 0 && errno == ENOSYS)		/* not an irix-on-gentoo kernel */
		return irix_statvfs64_fallback(path, sp);
	return (int)r;
}
#define statvfs64 irix_statvfs64

#endif
