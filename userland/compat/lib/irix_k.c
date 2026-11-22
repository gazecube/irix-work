/* the targets of the prelude's renaming macros; weak, so a program's own definition wins */
#include <stdlib.h>
__attribute__((weak)) char *irix_k_malloc(size_t n) { return malloc(n); }
__attribute__((weak)) char *irix_k_calloc(size_t n, size_t m) { return calloc(n, m); }
__attribute__((weak)) char *irix_k_realloc(void *p, size_t n) { return realloc(p, n); }
__attribute__((weak)) int irix_k_setenv(const char *n, const char *v, int over) { return setenv(n, v, over); }
#include <stdio.h>
/* old libc internals some programs call: flush every stream; run the exit handlers (only the stdio part here) */
void _cleanup(void) { fflush(NULL); }
void _exithandle(void) { fflush(NULL); }
#include <errno.h>
#include <string.h>
/* IRIX's error message table and errno accessors (the host has neither under these names) */
char *_sys_errlist[152];
int _sys_nerr = 152;
static void __attribute__((constructor)) irix_errlist_init(void) { int i; for (i = 0; i < 152; i++) _sys_errlist[i] = strerror(i); }
int oserror(void) { return errno; }
int setoserror(int e) { errno = e; return e; }
/* libc's multithreading hooks.  The flag stays 0: libc code only calls the lock functions when it is set (the IRIX
 * thread library sets it), so they are never reached; they exist for the linker. */
int __us_rsthread_misc = 0;
int __us_rsthread_stdio = 0;
int __libc_locklocale(void) { return 0; }
int __libc_unlocklocale(int x) { (void)x; return 0; }
int __libc_islocklocale(void) { return 1; }
int __libc_lockmisc(void) { return 0; }
int __libc_unlockmisc(int x) { (void)x; return 0; }
int __libc_lockdir(void) { return 0; }
int __libc_unlockdir(int x) { (void)x; return 0; }
/* the libc-internal spellings of the errno accessors, called by libc sources built here */
int _oserror(void) { return errno; }
int _setoserror(int e) { errno = e; return e; }
#include <stdio.h>
/* a program that only calls getline (the host's three-argument one) reaches it through the prelude's rename */
__attribute__((weak)) long irix_k_getline(char **l, size_t *n, FILE *f) { return getline(l, n, f); }
#include <unistd.h>
/* sysconf: a name from IRIX_SC_BASE (0x4000) up is an IRIX one, answered by the kernel through
 * syssgi(SGI_SYSCONF, name) as IRIX's own sysconf does; everything else is the host's */
extern long syssgi(int, ...);
long irix_k_sysconf(int name)
{
    if (name >= 0x4000)
        return syssgi(22 /* SGI_SYSCONF */, name - 0x4000);
    return sysconf(name);
}
#include <sys/utsname.h>
#include <unistd.h>
/* IRIX's sysinfo(command, buffer, count) (sys/systeminfo.h), answered from what the host kernel reports; the IRIX
 * values for the system name and release are the kernel personality's to give */
int irix_k_sysinfo(int cmd, char *buf, long count)
{
    struct utsname u;
    const char *v = 0;
    char host[256];
    if (cmd == 258) {                       /* SI_SET_HOSTNAME */
        int r = sethostname(buf, strnlen(buf, (size_t)count));
        return r ? -1 : (int)strlen(buf) + 1;
    }
    if (uname(&u))
        return -1;
    switch (cmd) {
    case 1: v = u.sysname; break;          /* SI_SYSNAME */
    case 2: v = u.nodename; break;         /* SI_HOSTNAME */
    case 3: v = u.release; break;          /* SI_RELEASE */
    case 4: v = u.version; break;          /* SI_VERSION */
    case 5: v = u.machine; break;          /* SI_MACHINE */
    case 6: v = u.machine; break;          /* SI_ARCHITECTURE */
    case 7: v = "0"; break;                /* SI_HW_SERIAL */
    case 8: v = "Silicon Graphics"; break; /* SI_HW_PROVIDER */
    case 9: if (getdomainname(host, sizeof host)) return -1; v = host; break;
    case 10: v = "/etc/inittab"; break;    /* SI_INITTAB_NAME */
    default: errno = EINVAL; return -1;
    }
    if (buf && count > 0) {
        strncpy(buf, v, (size_t)count - 1);
        buf[count - 1] = 0;
    }
    return (int)strlen(v) + 1;
}
/* IRIX's name service (nsd, ns_lookup): not running here, so lookups report it unavailable (NS_UNAVAIL) and the
 * callers use the files, as IRIX libc does when nsd is down */
int _ns_lookup(void *map, char *domain, char *table, const char *name, char *library, char *buf, size_t len)
{
    (void)map; (void)domain; (void)table; (void)name; (void)library; (void)buf; (void)len;
    return 2;
}
int ns_lookup(void *map, char *domain, char *table, const char *name, char *library, char *buf, size_t len) __attribute__((alias("_ns_lookup")));
/* switches of IRIX's getpwent that programs set (ls, ...): the host's passwd functions do the lookups here */
int _getpwent_no_shadow, _pw_stayopen;
#include <sys/xattr.h>
/* IRIX extended attributes (attr_get and friends, sys/attributes.h) on Linux's: user attributes are "user.NAME",
 * root ones (ATTR_ROOT) "trusted.NAME"; ATTR_DONTFOLLOW takes the l* calls.  XFS stores both the same way. */
static void xa_name(char *out, size_t n, const char *name, int flags) { snprintf(out, n, "%s.%s", (flags & 2) ? "trusted" : "user", name); }
int _attr_get(const char *path, const char *name, char *value, int *len, int flags)
{
    char n[300]; ssize_t r;
    xa_name(n, sizeof n, name, flags);
    r = (flags & 1) ? lgetxattr(path, n, value, (size_t)*len) : getxattr(path, n, value, (size_t)*len);
    if (r < 0) return -1;
    *len = (int)r;
    return 0;
}
int _attr_getf(int fd, const char *name, char *value, int *len, int flags)
{
    char n[300]; ssize_t r;
    xa_name(n, sizeof n, name, flags);
    r = fgetxattr(fd, n, value, (size_t)*len);
    if (r < 0) return -1;
    *len = (int)r;
    return 0;
}
int _attr_set(const char *path, const char *name, const char *value, int len, int flags)
{
    char n[300];
    int x = (flags & 0x10) ? XATTR_CREATE : (flags & 0x20) ? XATTR_REPLACE : 0;
    xa_name(n, sizeof n, name, flags);
    return (flags & 1) ? lsetxattr(path, n, value, (size_t)len, x) : setxattr(path, n, value, (size_t)len, x);
}
int _attr_setf(int fd, const char *name, const char *value, int len, int flags)
{
    char n[300];
    int x = (flags & 0x10) ? XATTR_CREATE : (flags & 0x20) ? XATTR_REPLACE : 0;
    xa_name(n, sizeof n, name, flags);
    return fsetxattr(fd, n, value, (size_t)len, x);
}
int _attr_remove(const char *path, const char *name, int flags)
{
    char n[300];
    xa_name(n, sizeof n, name, flags);
    return (flags & 1) ? lremovexattr(path, n) : removexattr(path, n);
}
int _attr_removef(int fd, const char *name, int flags) { char n[300]; xa_name(n, sizeof n, name, flags); return fremovexattr(fd, n); }
int attr_get(const char *p, const char *n, char *v, int *l, int f) __attribute__((weak, alias("_attr_get")));
int attr_getf(int d, const char *n, char *v, int *l, int f) __attribute__((weak, alias("_attr_getf")));
int attr_set(const char *p, const char *n, const char *v, int l, int f) __attribute__((weak, alias("_attr_set")));
int attr_setf(int d, const char *n, const char *v, int l, int f) __attribute__((weak, alias("_attr_setf")));
int attr_remove(const char *p, const char *n, int f) __attribute__((weak, alias("_attr_remove")));
int attr_removef(int d, const char *n, int f) __attribute__((weak, alias("_attr_removef")));
#include <stdarg.h>
__attribute__((weak)) int irix_k_abs(int v) { return v < 0 ? -v : v; }
__attribute__((weak)) int irix_k_asprintf(char **out, const char *fmt, ...) { va_list ap; int r; va_start(ap, fmt); r = vasprintf(out, fmt, ap); va_end(ap); return r; }
__attribute__((weak)) char *irix_k_sbrk(long n) { return (char *)sbrk(n); }
#include <fcntl.h>
/* getdtablehi: one more than the highest open descriptor */
int getdtablehi(void)
{
    int fd, hi = 0, n = getdtablesize();
    if (n > 4096) n = 4096;
    for (fd = 0; fd < n; fd++) if (fcntl(fd, F_GETFD) != -1) hi = fd + 1;
    return hi;
}
__attribute__((weak)) char *irix_k_index(const char *s, int c) { return strchr(s, c); }
__attribute__((weak)) char *irix_k_rindex(const char *s, int c) { return strrchr(s, c); }
/* cfree: the old name of free (glibc dropped it) */
void cfree(void *p) { free(p); }
