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
#ifndef _PATH_USERPATH
#define _PATH_USERPATH	"/usr/sbin:/usr/bsd:/sbin:/usr/bin:/usr/bin/X11:/usr/local/bin"
#endif
#ifndef _PATH_ROOTPATH
#define _PATH_ROOTPATH	"/usr/sbin:/usr/bsd:/sbin:/usr/bin:/etc:/usr/etc:/usr/bin/X11:/usr/local/bin"
#endif

/* sysconf(_SC_CAP) and sysconf(_SC_MAC) are answered by libirixcompat. */
#define _SC_CAP		0x10001
#define _SC_MAC		0x10002
long irix_sysconf(int);
#define sysconf		irix_sysconf

/* sys_errlist[] and sys_nerr were dropped from glibc. */
const char *const *irix_errlist(void);
#define sys_nerr	133
#define sys_errlist	(irix_errlist())

#ifndef _PATH_ISSUE
#define _PATH_ISSUE	"/etc/issue"		/* IRIX <paths.h> */
#endif
#ifndef _PATH_SCHEME
#define _PATH_SCHEME	"/usr/lib/iaf/scheme"	/* IRIX <paths.h> */
#endif
#ifndef CDEL
#define CDEL		0377			/* IRIX <sys/termios.h> */
#endif

/*
 * SVR4 libc: after fclose(stdout), fdopen(1, ...) reattaches the standard
 * stream (getty relies on it).  glibc leaves a closed stream dead, but
 * stdin/stdout/stderr are assignable, so rebind them to the new FILE.
 */
static inline FILE *irix_fdopen(int fd, const char *mode)
{
	FILE *f = fdopen(fd, mode);

	if (f && fd == 0)
		stdin = f;
	else if (f && fd == 1)
		stdout = f;
	else if (f && fd == 2)
		stderr = f;
	return f;
}
#define fdopen irix_fdopen

/* syslog() also appends to $IRIX_SYSLOG_FILE when set (debugging aid). */
#include <syslog.h>
void irix_syslog(int, const char *, ...);
#define syslog irix_syslog

long sginap(long);		/* IRIX: sleep in clock ticks (100/s); 0 yields */
int getdtablehi(void);		/* IRIX: highest possibly open descriptor + 1 */

/* <termio.h>/<sys/termios.h> defaults used by init's default gettydefs */
#ifndef SSPEED
#define SSPEED		B9600
#endif
#ifndef CNSWTCH
#define CNSWTCH		0
#endif

/* utmp locations in an IRIX tree, and the message strings of IRIX <utmp.h> */
#include <utmp.h>
#ifndef UTMP_FILE
#define UTMP_FILE	"/var/adm/utmp"
#endif
#ifndef WTMP_FILE
#define WTMP_FILE	"/var/adm/wtmp"
#endif
#ifndef RUNLVL_MSG
#define RUNLVL_MSG	"run-level %c"
#endif
#ifndef BOOT_MSG
#define BOOT_MSG	"system boot"
#endif

typedef unsigned int major_t;
typedef unsigned int minor_t;
#ifndef SIGEMT
#define SIGEMT		SIGSTKFLT	/* no SIGEMT on Linux/x86 */
#endif

/* SVR4 FILE flag tests (fp->_flag & _IORW ...): the Makefile rewrites
 * fp->_flag to irix_fflag(fp), which reports the stream's access mode. */
#include <fcntl.h>
#define _IOREAD		0x01
#define _IOWRT		0x02
#define _IORW		0x80
static inline int irix_fflag(FILE *fp)
{
	int m = fcntl(fileno(fp), F_GETFL);

	if (m < 0)
		return 0;
	switch (m & O_ACCMODE) {
	case O_RDWR: return _IORW;
	case O_WRONLY: return _IOWRT;
	default: return _IOREAD;
	}
}

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
