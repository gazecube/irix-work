#include "IrixPosixBridge.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

#define IRIX_POSIX_TMP_BASE 0x72800000u
#define IRIX_POSIX_TMP_SIZE 0x00100000u

/* IRIX/SVR4 open(2) flag values. */
#define IRIX_O_NDELAY   0x0004
#define IRIX_O_APPEND   0x0008
#define IRIX_O_SYNC     0x0010
#define IRIX_O_DSYNC    0x0020
#define IRIX_O_RSYNC    0x0040
#define IRIX_O_NONBLOCK 0x0080
#define IRIX_O_CREAT    0x0100
#define IRIX_O_TRUNC    0x0200
#define IRIX_O_EXCL     0x0400
#define IRIX_O_NOCTTY   0x0800

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

static const char *guest_cstr(IrixGuestRuntime *rt, IrixGuestAddr addr)
{
    return addr ? (const char *)irix_guest_ptr(rt, addr, 1) : 0;
}

static void wr32(IrixGuestRuntime *rt, IrixGuestAddr p, uint32_t v)
{
    if (p) irix_guest_write32be(rt, p, v);
}

static void wr64(IrixGuestRuntime *rt, IrixGuestAddr p, uint64_t v)
{
    if (p) irix_guest_write64be(rt, p, v);
}

static uint32_t irix_dev(dev_t d)
{
    return ((uint32_t)major(d) << 18) | ((uint32_t)minor(d) & 0x3ffffu);
}

/* IRIX n32 uses the large-file stat layout.  This matches the SGI xstat64
   shape documented by the historical Linux IRIX compatibility layer:
     dev, pad[3], ino64, mode, nlink, uid, gid, rdev, pad[2],
     size64, pad, three {sec,nsec}, blksize, blocks64, fstype[16], pad[8].
   All guest scalar fields are big-endian. */
static int write_irix_stat64(IrixGuestRuntime *rt, IrixGuestAddr out,
                             const struct stat *st)
{
    unsigned char *p;
    if (!out || !st) return -1;
    p = (unsigned char *)irix_guest_ptr(rt, out, 152u);
    if (!p) return -1;
    memset(p, 0, 152u);

    wr32(rt, out + 0u,  irix_dev(st->st_dev));
    wr64(rt, out + 16u, (uint64_t)st->st_ino);
    wr32(rt, out + 24u, (uint32_t)st->st_mode);
    wr32(rt, out + 28u, (uint32_t)st->st_nlink);
    wr32(rt, out + 32u, (uint32_t)st->st_uid);
    wr32(rt, out + 36u, (uint32_t)st->st_gid);
    wr32(rt, out + 40u, irix_dev(st->st_rdev));
    wr64(rt, out + 56u, (uint64_t)(int64_t)st->st_size);
    wr32(rt, out + 64u, 0u);
#if defined(__APPLE__)
    wr32(rt, out + 68u, (uint32_t)st->st_atimespec.tv_sec);
    wr32(rt, out + 72u, (uint32_t)st->st_atimespec.tv_nsec);
    wr32(rt, out + 76u, (uint32_t)st->st_mtimespec.tv_sec);
    wr32(rt, out + 80u, (uint32_t)st->st_mtimespec.tv_nsec);
    wr32(rt, out + 84u, (uint32_t)st->st_ctimespec.tv_sec);
    wr32(rt, out + 88u, (uint32_t)st->st_ctimespec.tv_nsec);
#else
    wr32(rt, out + 68u, (uint32_t)st->st_atim.tv_sec);
    wr32(rt, out + 72u, (uint32_t)st->st_atim.tv_nsec);
    wr32(rt, out + 76u, (uint32_t)st->st_mtim.tv_sec);
    wr32(rt, out + 80u, (uint32_t)st->st_mtim.tv_nsec);
    wr32(rt, out + 84u, (uint32_t)st->st_ctim.tv_sec);
    wr32(rt, out + 88u, (uint32_t)st->st_ctim.tv_nsec);
#endif
    wr32(rt, out + 92u, (uint32_t)st->st_blksize);
    wr64(rt, out + 96u, (uint64_t)(int64_t)st->st_blocks);
    memcpy(p + 104u, "xfs", 4u);
    return 0;
}

static int host_open_flags(uint32_t f)
{
    int out;
    switch (f & 3u) {
    case 1u: out = O_WRONLY; break;
    case 2u: out = O_RDWR; break;
    default: out = O_RDONLY; break;
    }
    if (f & IRIX_O_APPEND) out |= O_APPEND;
    if (f & IRIX_O_NONBLOCK) out |= O_NONBLOCK;
    if (f & IRIX_O_NDELAY) out |= O_NONBLOCK;
    if (f & IRIX_O_CREAT) out |= O_CREAT;
    if (f & IRIX_O_TRUNC) out |= O_TRUNC;
    if (f & IRIX_O_EXCL) out |= O_EXCL;
#ifdef O_NOCTTY
    if (f & IRIX_O_NOCTTY) out |= O_NOCTTY;
#endif
#ifdef O_SYNC
    if (f & IRIX_O_SYNC) out |= O_SYNC;
#endif
#ifdef O_DSYNC
    if (f & IRIX_O_DSYNC) out |= O_DSYNC;
#endif
#ifdef O_RSYNC
    if (f & IRIX_O_RSYNC) out |= O_RSYNC;
#endif
    return out;
}

static void ret_i32(IrixGuestCpu *cpu, int v)
{
    cpu->gpr[2] = (uint64_t)(int64_t)(int32_t)v;
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
    ret_i32(cpu, rc);
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
    const char *p = guest_cstr(rt, (uint32_t)cpu->gpr[4]);
    ret_i32(cpu, p ? chdir(p) : -1);
}

static void bridge_access(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *p = guest_cstr(rt, (uint32_t)cpu->gpr[4]);
    ret_i32(cpu, p ? access(p, (int)cpu->gpr[5]) : -1);
}

static void bridge_stat(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *p = guest_cstr(rt, (uint32_t)cpu->gpr[4]);
    IrixGuestAddr out = (uint32_t)cpu->gpr[5];
    struct stat st;
    int rc = p ? stat(p, &st) : -1;
    if (rc == 0 && write_irix_stat64(rt, out, &st) != 0) rc = -1;
    ret_i32(cpu, rc);
}

static void bridge_lstat(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *p = guest_cstr(rt, (uint32_t)cpu->gpr[4]);
    IrixGuestAddr out = (uint32_t)cpu->gpr[5];
    struct stat st;
    int rc = p ? lstat(p, &st) : -1;
    if (rc == 0 && write_irix_stat64(rt, out, &st) != 0) rc = -1;
    ret_i32(cpu, rc);
}

static void bridge_fstat(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr out = (uint32_t)cpu->gpr[5];
    struct stat st;
    int rc = fstat((int)cpu->gpr[4], &st);
    if (rc == 0 && write_irix_stat64(rt, out, &st) != 0) rc = -1;
    ret_i32(cpu, rc);
}

static void bridge_open(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *p = guest_cstr(rt, (uint32_t)cpu->gpr[4]);
    uint32_t flags = (uint32_t)cpu->gpr[5];
    mode_t mode = (mode_t)(uint32_t)cpu->gpr[6];
    ret_i32(cpu, p ? open(p, host_open_flags(flags), mode) : -1);
}

static void bridge_close(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)rt;
    ret_i32(cpu, close((int)cpu->gpr[4]));
}

static void bridge_read(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr a = (uint32_t)cpu->gpr[5];
    uint32_t n = (uint32_t)cpu->gpr[6];
    void *p = irix_guest_ptr(rt, a, n);
    ssize_t rc = p ? read((int)cpu->gpr[4], p, n) : -1;
    cpu->gpr[2] = (uint64_t)(int64_t)rc;
}

static void bridge_write(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr a = (uint32_t)cpu->gpr[5];
    uint32_t n = (uint32_t)cpu->gpr[6];
    const void *p = irix_guest_ptr(rt, a, n);
    ssize_t rc = p ? write((int)cpu->gpr[4], p, n) : -1;
    cpu->gpr[2] = (uint64_t)(int64_t)rc;
}

static void bridge_lseek(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    off_t off = (off_t)(int64_t)cpu->gpr[5];
    (void)rt;
    cpu->gpr[2] = (uint64_t)(int64_t)lseek((int)cpu->gpr[4], off, (int)cpu->gpr[6]);
}

static void bridge_unlink(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *p = guest_cstr(rt, (uint32_t)cpu->gpr[4]);
    ret_i32(cpu, p ? unlink(p) : -1);
}

static void bridge_rmdir(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *p = guest_cstr(rt, (uint32_t)cpu->gpr[4]);
    ret_i32(cpu, p ? rmdir(p) : -1);
}

static void bridge_mkdir(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *p = guest_cstr(rt, (uint32_t)cpu->gpr[4]);
    ret_i32(cpu, p ? mkdir(p, (mode_t)(uint32_t)cpu->gpr[5]) : -1);
}

static void bridge_rename(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *a = guest_cstr(rt, (uint32_t)cpu->gpr[4]);
    const char *b = guest_cstr(rt, (uint32_t)cpu->gpr[5]);
    ret_i32(cpu, (a && b) ? rename(a, b) : -1);
}

static void bridge_link(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *a = guest_cstr(rt, (uint32_t)cpu->gpr[4]);
    const char *b = guest_cstr(rt, (uint32_t)cpu->gpr[5]);
    ret_i32(cpu, (a && b) ? link(a, b) : -1);
}

static void bridge_symlink(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *a = guest_cstr(rt, (uint32_t)cpu->gpr[4]);
    const char *b = guest_cstr(rt, (uint32_t)cpu->gpr[5]);
    ret_i32(cpu, (a && b) ? symlink(a, b) : -1);
}

static void bridge_readlink(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *path = guest_cstr(rt, (uint32_t)cpu->gpr[4]);
    uint32_t n = (uint32_t)cpu->gpr[6];
    char *dst = (char *)irix_guest_ptr(rt, (uint32_t)cpu->gpr[5], n);
    ssize_t rc = (path && dst) ? readlink(path, dst, n) : -1;
    cpu->gpr[2] = (uint64_t)(int64_t)rc;
}

static void bridge_chmod(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *p = guest_cstr(rt, (uint32_t)cpu->gpr[4]);
    ret_i32(cpu, p ? chmod(p, (mode_t)(uint32_t)cpu->gpr[5]) : -1);
}

static void bridge_chown(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *p = guest_cstr(rt, (uint32_t)cpu->gpr[4]);
    ret_i32(cpu, p ? chown(p, (uid_t)(uint32_t)cpu->gpr[5], (gid_t)(uint32_t)cpu->gpr[6]) : -1);
}

static void bridge_umask(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)rt;
    cpu->gpr[2] = (uint32_t)umask((mode_t)(uint32_t)cpu->gpr[4]);
}

static void bridge_dup(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)rt;
    ret_i32(cpu, dup((int)cpu->gpr[4]));
}

static void bridge_dup2(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)rt;
    ret_i32(cpu, dup2((int)cpu->gpr[4], (int)cpu->gpr[5]));
}

static void bridge_isatty(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)rt;
    ret_i32(cpu, isatty((int)cpu->gpr[4]));
}

static void bridge_sleep(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)rt;
    cpu->gpr[2] = (uint32_t)sleep((unsigned)cpu->gpr[4]);
}

static void bridge_usleep(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)rt;
    ret_i32(cpu, usleep((useconds_t)(uint32_t)cpu->gpr[4]));
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
    if (rc != 0 || !out) { ret_i32(cpu, rc); return; }
    p = (unsigned char *)irix_guest_ptr(rt, out, 5u * 257u);
    if (!p) { ret_i32(cpu, -1); return; }
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
    /* IRIX and glibc _SC_* numbers are not ABI-compatible. */
    cpu->gpr[2] = (uint64_t)(int64_t)(int32_t)-1;
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

static void bridge_strerror(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *s = strerror((int)(int32_t)cpu->gpr[4]);
    size_t n;
    IrixGuestAddr a;
    void *p;
    if (!s) { cpu->gpr[2] = 0; return; }
    n = strlen(s) + 1u;
    a = n <= UINT32_MAX ? tmp_alloc((uint32_t)n) : 0;
    p = a ? irix_guest_ptr(rt, a, n) : 0;
    if (p) memcpy(p, s, n); else a = 0;
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
#define MAP(n) if (!strcmp(name, #n)) return bridge_##n
    MAP(gethostname); MAP(getcwd); MAP(chdir); MAP(access);
    MAP(stat); MAP(lstat); MAP(fstat);
    MAP(open); MAP(close); MAP(read); MAP(write); MAP(lseek);
    MAP(unlink); MAP(rmdir); MAP(mkdir); MAP(rename); MAP(link); MAP(symlink); MAP(readlink);
    MAP(chmod); MAP(chown); MAP(umask); MAP(dup); MAP(dup2); MAP(isatty);
    MAP(sleep); MAP(usleep); MAP(time); MAP(uname); MAP(getpagesize); MAP(sysconf); MAP(getlogin);
    MAP(strerror);
#undef MAP
    return 0;
}
