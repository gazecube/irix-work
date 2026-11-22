/* IRIX system calls of the irix-on-gentoo kernel, for programs built from IRIX source (numbers shared with the
 * lifted side: native-libs/nativemem/shim/irix_syscalls.h).  ENOSYS until the running kernel has them;
 * IRIX_KTRACE=1 reports each missing one once. */
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/syscall.h>
#include "../../../native-libs/nativemem/shim/irix_syscalls.h"

static long kcall_first(int nr, const char *name, int cmd, va_list ap)
{
	long a[6];
	int i;
	long r;
	a[0] = cmd;
	for (i = 1; i < 6; i++) a[i] = va_arg(ap, long);
	r = syscall(nr, a[0], a[1], a[2], a[3], a[4], a[5]);
	if (r == -1 && errno == ENOSYS && getenv("IRIX_KTRACE")) {
		static unsigned said;
		unsigned bit = 1u << (nr - IRIX_NR_syssgi);
		if (!(said & bit)) { said |= bit; fprintf(stderr, "%s: the kernel has no IRIX %s yet\n", name, name); }
	}
	return r;
}
long syssgi(int cmd, ...) { va_list ap; long r; va_start(ap, cmd); r = kcall_first(IRIX_NR_syssgi, "syssgi", cmd, ap); va_end(ap); return r; }
long sysmp(int cmd, ...) { va_list ap; long r; va_start(ap, cmd); r = kcall_first(IRIX_NR_sysmp, "sysmp", cmd, ap); va_end(ap); return r; }
long schedctl(int cmd, ...) { va_list ap; long r; va_start(ap, cmd); r = kcall_first(IRIX_NR_schedctl, "schedctl", cmd, ap); va_end(ap); return r; }
