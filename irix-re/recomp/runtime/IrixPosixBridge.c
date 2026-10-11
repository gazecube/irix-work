#include "IrixPosixBridge.h"

#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <pwd.h>
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
#define IRIX_POSIX_DIR_MAX 64u
#define IRIX_POSIX_DIRENT_SIZE 280u
#define IRIX_L_CUSERID 9u

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
static struct {
    IrixGuestAddr token;
    DIR *host_dir;
} g_dirs[IRIX_POSIX_DIR_MAX];

/*
 * IRIX sigset_t is four 32-bit words.  Keep the guest process mask here
 * instead of applying IRIX signal numbers to the Linux host process.
 */
static uint32_t g_irix_sigmask[4];

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
    return irix_guest_cstr(rt, addr, 65536u);
}

static const char *host_path(IrixGuestRuntime *rt, const char *guest,
                             char *buffer, size_t size,
                             const char *operation)
{
    return irix_guest_resolve_path(rt, guest, buffer, size, operation, 0);
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

static int guest_gid_allowed(gid_t gid)
{
    return gid == (gid_t)-1 || gid == getgid() || gid == getegid();
}

static void bridge_setregid(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    gid_t rgid = (gid_t)(uint32_t)cpu->gpr[4];
    gid_t egid = (gid_t)(uint32_t)cpu->gpr[5];
    (void)rt;

    /*
     * Mirror the setreuid compatibility boundary: IRIX applications may
     * normalize their real/effective group IDs during startup, but the guest
     * must not mutate the Linux runner's credentials. Accept no-op/current
     * identity transitions and reject unrelated host identities.
     */
    if (guest_gid_allowed(rgid) && guest_gid_allowed(egid)) {
        ret_i32(cpu, 0);
        return;
    }
    errno = EPERM;
    ret_i32(cpu, -1);
}

static void bridge_setreuid(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    uid_t ruid = (uid_t)(uint32_t)cpu->gpr[4];
    uid_t euid = (uid_t)(uint32_t)cpu->gpr[5];
    (void)rt;

    /*
     * Keep guest credential changes inside the IRIX personality. xwsh drops
     * to the passwd entry's uid during startup; applying that to the host
     * runner would mutate the compatibility process itself. For the common
     * unprivileged no-op case, preserve the IRIX success result. Reject an
     * attempted transition to identities the host process does not already
     * possess rather than changing host credentials.
     */
    if ((ruid == (uid_t)-1 || ruid == getuid() || ruid == geteuid()) &&
        (euid == (uid_t)-1 || euid == getuid() || euid == geteuid())) {
        ret_i32(cpu, 0);
        return;
    }
    errno = EPERM;
    ret_i32(cpu, -1);
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
    const char *guest = guest_cstr(rt, (uint32_t)cpu->gpr[4]);
    char buffer[PATH_MAX];
    const char *p = guest ? host_path(rt, guest, buffer, sizeof(buffer), "chdir") : 0;
    ret_i32(cpu, p ? chdir(p) : -1);
}

static void bridge_access(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *guest = guest_cstr(rt, (uint32_t)cpu->gpr[4]);
    char buffer[PATH_MAX];
    const char *p = guest ? host_path(rt, guest, buffer, sizeof(buffer), "access") : 0;
    ret_i32(cpu, p ? access(p, (int)cpu->gpr[5]) : -1);
}

static void bridge_stat(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *guest = guest_cstr(rt, (uint32_t)cpu->gpr[4]);
    char buffer[PATH_MAX];
    const char *p = guest ? host_path(rt, guest, buffer, sizeof(buffer), "stat") : 0;
    IrixGuestAddr out = (uint32_t)cpu->gpr[5];
    struct stat st;
    int rc = p ? stat(p, &st) : -1;
    if (rc == 0 && write_irix_stat64(rt, out, &st) != 0) rc = -1;
    ret_i32(cpu, rc);
}

static void bridge_stat64(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    bridge_stat(rt, cpu);
}

static void bridge_lstat(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *guest = guest_cstr(rt, (uint32_t)cpu->gpr[4]);
    char buffer[PATH_MAX];
    const char *p = guest ? host_path(rt, guest, buffer, sizeof(buffer), "lstat") : 0;
    IrixGuestAddr out = (uint32_t)cpu->gpr[5];
    struct stat st;
    int rc = p ? lstat(p, &st) : -1;
    if (rc == 0 && write_irix_stat64(rt, out, &st) != 0) rc = -1;
    ret_i32(cpu, rc);
}

static void bridge_lstat64(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    bridge_lstat(rt, cpu);
}

static void bridge_fstat(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr out = (uint32_t)cpu->gpr[5];
    struct stat st;
    int rc = fstat((int)cpu->gpr[4], &st);
    if (rc == 0 && write_irix_stat64(rt, out, &st) != 0) rc = -1;
    ret_i32(cpu, rc);
}

static void bridge_fstat64(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    bridge_fstat(rt, cpu);
}

static void bridge_open(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *guest = guest_cstr(rt, (uint32_t)cpu->gpr[4]);
    char buffer[PATH_MAX];
    const char *p = guest ? host_path(rt, guest, buffer, sizeof(buffer), "open") : 0;
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

static void bridge_opendir(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *guest = guest_cstr(rt, (uint32_t)cpu->gpr[4]);
    char buffer[PATH_MAX];
    const char *path = guest ? host_path(rt, guest, buffer, sizeof(buffer),
                                         "resource:opendir") : 0;
    size_t i;
    DIR *dir = path ? opendir(path) : 0;
    IrixGuestAddr token = 0;
    if (!dir) {
        cpu->gpr[2] = 0;
        return;
    }
    for (i = 0; i < IRIX_POSIX_DIR_MAX; ++i)
        if (!g_dirs[i].host_dir) break;
    if (i == IRIX_POSIX_DIR_MAX || !(token = tmp_alloc(IRIX_POSIX_DIRENT_SIZE))) {
        closedir(dir);
        cpu->gpr[2] = 0;
        return;
    }
    g_dirs[i].token = token;
    g_dirs[i].host_dir = dir;
    cpu->gpr[2] = token;
}

static void bridge_readdir(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr token = (uint32_t)cpu->gpr[4];
    size_t i;
    struct dirent *entry = 0;
    (void)rt;
    for (i = 0; i < IRIX_POSIX_DIR_MAX; ++i)
        if (g_dirs[i].host_dir && g_dirs[i].token == token) break;
    if (i < IRIX_POSIX_DIR_MAX) entry = readdir(g_dirs[i].host_dir);
    if (!entry) {
        cpu->gpr[2] = 0;
        return;
    }
    {
        size_t name_len = strnlen(entry->d_name, 256u);
        uint16_t record_len = (uint16_t)(18u + name_len + 1u);
        IrixGuestAddr out = token;
        unsigned char *p = (unsigned char *)irix_guest_ptr(rt, out,
                                                           IRIX_POSIX_DIRENT_SIZE);
        if (!p) { cpu->gpr[2] = 0; return; }
        memset(p, 0, IRIX_POSIX_DIRENT_SIZE);
        irix_guest_write64be(rt, out, (uint64_t)entry->d_ino);
        irix_guest_write64be(rt, out + 8u, (uint64_t)entry->d_off);
        irix_guest_write16be(rt, out + 16u, record_len);
        memcpy(p + 18u, entry->d_name, name_len);
        cpu->gpr[2] = out;
    }
}

static void bridge_closedir(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr token = (uint32_t)cpu->gpr[4];
    size_t i;
    (void)rt;
    for (i = 0; i < IRIX_POSIX_DIR_MAX; ++i) {
        if (g_dirs[i].host_dir && g_dirs[i].token == token) {
            int rc = closedir(g_dirs[i].host_dir);
            g_dirs[i].host_dir = 0;
            g_dirs[i].token = 0;
            ret_i32(cpu, rc);
            return;
        }
    }
    errno = EBADF;
    ret_i32(cpu, -1);
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
    const char *guest = guest_cstr(rt, (uint32_t)cpu->gpr[4]);
    char buffer[PATH_MAX];
    const char *p = guest ? host_path(rt, guest, buffer, sizeof(buffer), "unlink") : 0;
    ret_i32(cpu, p ? unlink(p) : -1);
}

static void bridge_rmdir(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *guest = guest_cstr(rt, (uint32_t)cpu->gpr[4]);
    char buffer[PATH_MAX];
    const char *p = guest ? host_path(rt, guest, buffer, sizeof(buffer), "rmdir") : 0;
    ret_i32(cpu, p ? rmdir(p) : -1);
}

static void bridge_mkdir(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *guest = guest_cstr(rt, (uint32_t)cpu->gpr[4]);
    char buffer[PATH_MAX];
    const char *p = guest ? host_path(rt, guest, buffer, sizeof(buffer), "mkdir") : 0;
    ret_i32(cpu, p ? mkdir(p, (mode_t)(uint32_t)cpu->gpr[5]) : -1);
}

static void bridge_rename(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *ga = guest_cstr(rt, (uint32_t)cpu->gpr[4]);
    const char *gb = guest_cstr(rt, (uint32_t)cpu->gpr[5]);
    char ba[PATH_MAX], bb[PATH_MAX];
    const char *a = ga ? host_path(rt, ga, ba, sizeof(ba), "rename") : 0;
    const char *b = gb ? host_path(rt, gb, bb, sizeof(bb), "rename") : 0;
    ret_i32(cpu, (a && b) ? rename(a, b) : -1);
}

static void bridge_link(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *ga = guest_cstr(rt, (uint32_t)cpu->gpr[4]);
    const char *gb = guest_cstr(rt, (uint32_t)cpu->gpr[5]);
    char ba[PATH_MAX], bb[PATH_MAX];
    const char *a = ga ? host_path(rt, ga, ba, sizeof(ba), "link") : 0;
    const char *b = gb ? host_path(rt, gb, bb, sizeof(bb), "link") : 0;
    ret_i32(cpu, (a && b) ? link(a, b) : -1);
}

static void bridge_symlink(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *a = guest_cstr(rt, (uint32_t)cpu->gpr[4]);
    const char *guest_b = guest_cstr(rt, (uint32_t)cpu->gpr[5]);
    char buffer[PATH_MAX];
    const char *b = guest_b ? host_path(rt, guest_b, buffer, sizeof(buffer), "symlink") : 0;
    ret_i32(cpu, (a && b) ? symlink(a, b) : -1);
}

static void bridge_readlink(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *guest = guest_cstr(rt, (uint32_t)cpu->gpr[4]);
    char buffer[PATH_MAX];
    const char *path = guest ? host_path(rt, guest, buffer, sizeof(buffer), "readlink") : 0;
    uint32_t n = (uint32_t)cpu->gpr[6];
    char *dst = (char *)irix_guest_ptr(rt, (uint32_t)cpu->gpr[5], n);
    ssize_t rc = (path && dst) ? readlink(path, dst, n) : -1;
    cpu->gpr[2] = (uint64_t)(int64_t)rc;
}

static void bridge_chmod(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *guest = guest_cstr(rt, (uint32_t)cpu->gpr[4]);
    char buffer[PATH_MAX];
    const char *p = guest ? host_path(rt, guest, buffer, sizeof(buffer), "chmod") : 0;
    ret_i32(cpu, p ? chmod(p, (mode_t)(uint32_t)cpu->gpr[5]) : -1);
}

static void bridge_chown(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *guest = guest_cstr(rt, (uint32_t)cpu->gpr[4]);
    char buffer[PATH_MAX];
    const char *p = guest ? host_path(rt, guest, buffer, sizeof(buffer), "chown") : 0;
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
    if (!p) { errno = EFAULT; ret_i32(cpu, -1); return; }

    /*
     * Do not leak the x86/Linux host identity into IRIX feature selection.
     * This is the same compatibility boundary as systeminfo(): guest software
     * must see a coherent SGI/IRIX machine.
     */
    memset(p, 0, 5u * 257u);
    strncpy((char *)p + 0u * 257u, "IRIX", 256u);
    strncpy((char *)p + 1u * 257u, u.nodename[0] ? u.nodename : "IRIS", 256u);
    strncpy((char *)p + 2u * 257u, "6.5", 256u);
    strncpy((char *)p + 3u * 257u, "6.5.7", 256u);
    strncpy((char *)p + 4u * 257u, "IP30", 256u);
    cpu->gpr[2] = 0;
}

static void bridge_getpagesize(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)rt;
    /* IRIX/MIPS uses 16 KiB pages; host x86 page size is irrelevant here. */
    cpu->gpr[2] = 16384u;
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

static void bridge_cuserid(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const struct passwd *pw = getpwuid(geteuid());
    IrixGuestAddr out = (IrixGuestAddr)(uint32_t)cpu->gpr[4];
    char *guest;
    size_t length;

    if (!pw || !pw->pw_name) {
        errno = ENOENT;
        cpu->gpr[2] = 0;
        return;
    }

    length = strlen(pw->pw_name);
    if (length >= IRIX_L_CUSERID) {
        errno = ERANGE;
        cpu->gpr[2] = 0;
        return;
    }

    if (!out)
        out = tmp_alloc(IRIX_L_CUSERID);
    guest = out ? (char *)irix_guest_ptr(rt, out, IRIX_L_CUSERID) : 0;
    if (!guest) {
        errno = out ? EFAULT : ENOMEM;
        cpu->gpr[2] = 0;
        return;
    }

    memcpy(guest, pw->pw_name, length + 1u);
    cpu->gpr[2] = out;
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

/*
 * IRIX sigprocmask(2) ABI:
 *   sigset_t = { uint32_t sigbits[4] }
 *   SIG_NOP       = 0
 *   SIG_BLOCK     = 1
 *   SIG_UNBLOCK   = 2
 *   SIG_SETMASK   = 3
 *   SIG_SETMASK32 = 0x100
 *
 * Do not forward this directly to host sigprocmask(): IRIX and Linux signal
 * numbering/mask ABIs are different, and guest mask changes must not alter the
 * runtime process' own signal handling.
 */
static void bridge_sigprocmask(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    int how = (int)(int32_t)cpu->gpr[4];
    IrixGuestAddr set_addr = (IrixGuestAddr)(uint32_t)cpu->gpr[5];
    IrixGuestAddr old_addr = (IrixGuestAddr)(uint32_t)cpu->gpr[6];
    uint32_t incoming[4];
    unsigned i;

    if (old_addr && !irix_guest_ptr(rt, old_addr, 16u)) {
        errno = EFAULT;
        ret_i32(cpu, -1);
        return;
    }
    if (set_addr && !irix_guest_ptr(rt, set_addr, 16u)) {
        errno = EFAULT;
        ret_i32(cpu, -1);
        return;
    }

    if (old_addr) {
        for (i = 0; i < 4u; ++i)
            irix_guest_write32be(rt, old_addr + i * 4u, g_irix_sigmask[i]);
    }

    /*
     * As on IRIX/POSIX, a NULL set is a pure query.  In particular 4Dwm's
     * LaunchAutoApps path calls sigprocmask(SIG_NOP, NULL, &oldmask).
     */
    if (!set_addr || how == 0) {
        (void)0;
        ret_i32(cpu, 0);
        return;
    }

    for (i = 0; i < 4u; ++i)
        incoming[i] = irix_guest_read32be(rt, set_addr + i * 4u);

    switch (how) {
    case 1: /* SIG_BLOCK */
        for (i = 0; i < 4u; ++i)
            g_irix_sigmask[i] |= incoming[i];
        break;
    case 2: /* SIG_UNBLOCK */
        for (i = 0; i < 4u; ++i)
            g_irix_sigmask[i] &= ~incoming[i];
        break;
    case 3: /* SIG_SETMASK */
        for (i = 0; i < 4u; ++i)
            g_irix_sigmask[i] = incoming[i];
        break;
    case 0x100: /* SGI SIG_SETMASK32 */
        g_irix_sigmask[0] = incoming[0];
        break;
    default:
        errno = EINVAL;
        ret_i32(cpu, -1);
        return;
    }

    (void)0;
    ret_i32(cpu, 0);
}

void irix_posix_bridge_init(IrixGuestRuntime *rt)
{
    g_rt = rt;
    g_tmp_cur = IRIX_POSIX_TMP_BASE;
    memset(g_irix_sigmask, 0, sizeof(g_irix_sigmask));
    memset(g_dirs, 0, sizeof(g_dirs));
    if (irix_guest_map(rt, IRIX_POSIX_TMP_BASE, IRIX_POSIX_TMP_SIZE, 3u) != 0 &&
        !irix_guest_ptr(rt, IRIX_POSIX_TMP_BASE, 1))
        g_tmp_cur = 0;
}

IrixGuestFunction irix_posix_bridge_lookup(const char *name)
{
    if (!name) return 0;
#define MAP(n) if (!strcmp(name, #n)) return bridge_##n
    MAP(gethostname); MAP(getcwd); MAP(chdir); MAP(access); MAP(setreuid); MAP(setregid);
    /* IRIX n32's stat64 uses the same 152-byte guest layout as stat here. */
    MAP(stat); MAP(stat64); MAP(lstat); MAP(lstat64); MAP(fstat); MAP(fstat64);
    MAP(open); MAP(close); MAP(read); MAP(write); MAP(lseek);
    MAP(opendir); MAP(readdir); MAP(closedir);
    MAP(unlink); MAP(rmdir); MAP(mkdir); MAP(rename); MAP(link); MAP(symlink); MAP(readlink);
    MAP(chmod); MAP(chown); MAP(umask); MAP(dup); MAP(dup2); MAP(isatty);
    MAP(sleep); MAP(usleep); MAP(time); MAP(uname); MAP(getpagesize); MAP(sysconf); MAP(getlogin); MAP(cuserid);
    MAP(sigprocmask);
    MAP(strerror);
#undef MAP
    return 0;
}
