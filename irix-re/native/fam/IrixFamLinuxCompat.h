#ifndef IRIX_FAM_LINUX_COMPAT_H
#define IRIX_FAM_LINUX_COMPAT_H

#include <limits.h>
#include <sys/types.h>

#ifndef NGROUPS_UMAX
#ifdef NGROUPS_MAX
#define NGROUPS_UMAX NGROUPS_MAX
#else
#define NGROUPS_UMAX 65536
#endif
#endif

#ifndef _IRIX_FAM_ULONG_TYPES
#define _IRIX_FAM_ULONG_TYPES
typedef unsigned long ulong;
typedef unsigned long ulong_t;
#endif

#ifdef __cplusplus
extern "C" {
#endif

int sgikopt(const char *option, char *buf, int buflen);
int syssgi(int request, ...);
int _daemonize(int flags, int fd0, int fd1, int fd2);

#ifdef __cplusplus
}
#endif

#ifndef SGI_NOHANG
#define SGI_NOHANG 0x1000
#endif

#endif
