#include "IrixPosixBridge.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

#define IRIX_POSIX_TMP_BASE 0x72800000u
#define IRIX_POSIX_TMP_SIZE 0x00100000u

static IrixGuestRuntime *g_rt;
static IrixGuestAddr g_tmp_cur;

static IrixGuestAddr tmp_alloc(uint32_t n)
{
    IrixGuestAddr a;
    n = (n + 7u) & ~7u;
    if (!n || g_tmp_cur + n < g_tmp_cur ||
        g_tmp_cur + n > IRIX_POSIX_TMP_BASE + IRIX_POSIX_TMP_SIZE)
        return 0;
    a = g_tmp_cur;
    g_tmp_cur += n;
    return a;
}

static void wr32(IrixGuestRuntime *rt, IrixGuestAddr p, uint32_t v)
{
    if (p) irix_guest_write32be(rt, p, v);
}

static void bridge_gethostname(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr dst_addr = (uint32_t)cpu->gpr[4];
    uint32_t len = (uint32_t)cpu->gpr[5];
    char *dst = len ? (char *)irix_guest_ptr(rt, dst_addr, len) : 0;
    int rc = -1;
    if (dst && len) {
        rc = gethostname(dst, (size_t)len);
        if (rc == 0) dst[len - 1u] = '\0';
    }
    cpu->gpr[2] = (uint64_t)(int64_t)(int32_t)rc;
}

static void bridge_getcwd(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr dst_addr = (uint32_t)cpu->gpr[4];
    uint32_t len = (uint32_t)cpu->gpr[5];
    char *dst = len ? (char *)irix_guest_ptr(rt, dst_addr, len) : 0;
    cpu->gpr[2] = (dst && getcwd(dst, (size_t)len)) ? dst_addr : 0;
}

static void bridge_chdir(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *p = (const char *)irix_guest_ptr(rt, (uint32_t)cpu->gpr[4], 1);
    cpu->gpr[2] = (uint64_t)(int64_t)(int32_t)(p ? chdir(p) : -1);
}

static void bridge_access(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *p = (const char *)irix_guest_ptr(rt, (uint32_t)cpu->gpr[4], 1);
    cpu->gpr[2] = (uint64_t)(int64_t)(int32_t)(p ? access(p, (int)cpu->gpr[5]) : -1);
}

static void bridge_sleep(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)rt;
    cpu->gpr[2] = (uint32_t)sleep((unsigned)cpu->gpr[4]);
}

static void bridge_usleep(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)rt;
    cpu->gpr[2] = (uint64_t)(int64_t)(int32_t)usleep((useconds_t)(uint32_t)cpu->gpr[4]);
}

static void bridge_time(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    time_t t = time(0);
    IrixGuestAddr out = (uint32_t)cpu->gpr[4];
    if (out) wr32(rt, out, (uint32_t)t);
    cpu->gpr[2] = (uint32_t)t;
}

static void bridge_uname(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    struct utsname u;
    IrixGuestAddr out = (uint32_t)cpu->gpr[4];
    unsigned char *p;
    int rc = uname(&u);
    if (rc != 0 || !out) {
        cpu->gpr[2] = (uint64_t)(int64_t)(int32_t)rc;
        return;
    }
    /* IRIX utsname is five 257-byte character arrays in this era. Keep the
       guest representation self-contained and NUL-terminated. */
    p = (unsigned char *)irix_guest_ptr(rt, out, 5u * 257u);
    if (!p) {
        cpu->gpr[2] = (uint64_t)(int64_t)(int32_t)-1;
        return;
    }
    memset(p, 0, 5u * 257u);
    strncpy((char *)p + 0u * 257u, u.sysname, 256u);
    strncpy((char *)p + 1u * 257u, u.nodename, 256u);
    strncpy((char *)p + 2u * 257u, u.release, 256u);
    strncpy((char *)p + 3u * 257u, u.version, 256u);
    strncpy((char *)p + 4u * 257u, u.machine, 256u);
    cpu->gpr[2] = 0;
}

static void bridge_getpagesize(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)rt;
    cpu->gpr[2] = (uint32_t)getpagesize();
}

static void bridge_sysconf(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)rt;
    /* IRIX and glibc _SC_* numbers are not ABI-compatible. We only handle
       the values commonly requested during Xt-era bootstrap when recognized;
       unknown values intentionally return -1 rather than forwarding a wrong id. */
    switch ((int32_t)cpu->gpr[4]) {
    default:
        cpu->gpr[2] = (uint64_t)(int64_t)(int32_t)-1;
        break;
    }
}

static void bridge_getlogin(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *s = getlogin();
    IrixGuestAddr a = 0;
    if (s) {
        size_t n = strlen(s) + 1u;
        if (n <= UINT32_MAX) {
            a = tmp_alloc((uint32_t)n);
            if (a) {
                void *p = irix_guest_ptr(rt, a, n);
                if (p) memcpy(p, s, n); else a = 0;
            }
        }
    }
    cpu->gpr[2] = a;
}

void irix_posix_bridge_init(IrixGuestRuntime *rt)
{
    g_rt = rt;
    g_tmp_cur = IRIX_POSIX_TMP_BASE;
    if (irix_guest_map(rt, IRIX_POSIX_TMP_BASE, IRIX_POSIX_TMP_SIZE, 3u) != 0 &&
        !irix_guest_ptr(rt, IRIX_POSIX_TMP_BASE, 1))
        g_tmp_cur = 0;
}

IrixGuestFunction irix_posix_bridge_lookup(const char *name)
{
    if (!name) return 0;
    if (!strcmp(name, "gethostname")) return bridge_gethostname;
    if (!strcmp(name, "getcwd")) return bridge_getcwd;
    if (!strcmp(name, "chdir")) return bridge_chdir;
    if (!strcmp(name, "access")) return bridge_access;
    if (!strcmp(name, "sleep")) return bridge_sleep;
    if (!strcmp(name, "usleep")) return bridge_usleep;
    if (!strcmp(name, "time")) return bridge_time;
    if (!strcmp(name, "uname")) return bridge_uname;
    if (!strcmp(name, "getpagesize")) return bridge_getpagesize;
    if (!strcmp(name, "sysconf")) return bridge_sysconf;
    if (!strcmp(name, "getlogin")) return bridge_getlogin;
    return 0;
}
