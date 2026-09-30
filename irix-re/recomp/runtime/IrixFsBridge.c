#include "IrixFsBridge.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/sysmacros.h>
#include <unistd.h>

/* IRIX 5/6 open(2) flags.  Keep these separate from host O_* values. */
#define IRIX_O_ACCMODE   0x0003
#define IRIX_O_NDELAY    0x0004
#define IRIX_O_APPEND    0x0008
#define IRIX_O_SYNC      0x0010
#define IRIX_O_ASYNC     0x0040
#define IRIX_O_NONBLOCK  0x0080
#define IRIX_O_CREAT     0x0100
#define IRIX_O_TRUNC     0x0200
#define IRIX_O_EXCL      0x0400
#define IRIX_O_NOCTTY    0x0800
#define IRIX_O_DIRECT    0x8000

static IrixGuestRuntime *g_rt;

static const char *guest_cstr(IrixGuestRuntime *rt, IrixGuestAddr addr)
{
    return irix_guest_cstr(rt, addr, 65536u);
}

static void ret_int(IrixGuestCpu *cpu, int v)
{
    cpu->gpr[2] = (uint64_t)(int64_t)(int32_t)v;
}

static uint32_t irix_dev(dev_t d)
{
    return ((uint32_t)major(d) << 18) | ((uint32_t)minor(d) & 0x3ffffu);
}

static int host_open_flags(uint32_t f)
{
    int out = 0;
    switch (f & IRIX_O_ACCMODE) {
    case 1: out |= O_WRONLY; break;
    case 2: out |= O_RDWR; break;
    default: out |= O_RDONLY; break;
    }
    if (f & IRIX_O_NDELAY) out |= O_NONBLOCK;
    if (f & IRIX_O_APPEND) out |= O_APPEND;
#ifdef O_SYNC
    if (f & IRIX_O_SYNC) out |= O_SYNC;
#endif
#ifdef O_ASYNC
    if (f & IRIX_O_ASYNC) out |= O_ASYNC;
#endif
    if (f & IRIX_O_NONBLOCK) out |= O_NONBLOCK;
    if (f & IRIX_O_CREAT) out |= O_CREAT;
    if (f & IRIX_O_TRUNC) out |= O_TRUNC;
    if (f & IRIX_O_EXCL) out |= O_EXCL;
#ifdef O_NOCTTY
    if (f & IRIX_O_NOCTTY) out |= O_NOCTTY;
#endif
#ifdef O_DIRECT
    if (f & IRIX_O_DIRECT) out |= O_DIRECT;
#endif
    return out;
}

/*
 * IRIX xstat64 layout used by the historical Linux IRIX compatibility code.
 * N32 uses 32-bit pointers/longs but 64-bit inode, size, and block count here.
 * The explicit offsets below include the MIPS 8-byte alignment before int64s.
 */
static int write_irix_stat64(IrixGuestRuntime *rt, IrixGuestAddr out,
                             const struct stat *st)
{
    unsigned char *p;
    if (!out || !st) return -1;
    p = (unsigned char *)irix_guest_ptr(rt, out, 152u);
    if (!p) return -1;
    memset(p, 0, 152u);

    irix_guest_write32be(rt, out + 0u, irix_dev(st->st_dev));
    irix_guest_write64be(rt, out + 16u, (uint64_t)st->st_ino);
    irix_guest_write32be(rt, out + 24u, (uint32_t)st->st_mode);
    irix_guest_write32be(rt, out + 28u, (uint32_t)st->st_nlink);
    irix_guest_write32be(rt, out + 32u, (uint32_t)st->st_uid);
    irix_guest_write32be(rt, out + 36u, (uint32_t)st->st_gid);
    irix_guest_write32be(rt, out + 40u, irix_dev(st->st_rdev));
    irix_guest_write64be(rt, out + 56u, (uint64_t)(int64_t)st->st_size);

#if defined(__APPLE__)
    irix_guest_write32be(rt, out + 68u, (uint32_t)st->st_atimespec.tv_sec);
    irix_guest_write32be(rt, out + 72u, (uint32_t)st->st_atimespec.tv_nsec);
    irix_guest_write32be(rt, out + 76u, (uint32_t)st->st_mtimespec.tv_sec);
    irix_guest_write32be(rt, out + 80u, (uint32_t)st->st_mtimespec.tv_nsec);
    irix_guest_write32be(rt, out + 84u, (uint32_t)st->st_ctimespec.tv_sec);
    irix_guest_write32be(rt, out + 88u, (uint32_t)st->st_ctimespec.tv_nsec);
#else
    irix_guest_write32be(rt, out + 68u, (uint32_t)st->st_atim.tv_sec);
    irix_guest_write32be(rt, out + 72u, (uint32_t)st->st_atim.tv_nsec);
    irix_guest_write32be(rt, out + 76u, (uint32_t)st->st_mtim.tv_sec);
    irix_guest_write32be(rt, out + 80u, (uint32_t)st->st_mtim.tv_nsec);
    irix_guest_write32be(rt, out + 84u, (uint32_t)st->st_ctim.tv_sec);
    irix_guest_write32be(rt, out + 88u, (uint32_t)st->st_ctim.tv_nsec);
#endif
    irix_guest_write32be(rt, out + 92u, (uint32_t)st->st_blksize);
    irix_guest_write64be(rt, out + 96u, (uint64_t)(int64_t)st->st_blocks);
    /* st_fstype[16] at +104. Linux's old IRIX layer used "efs" for xstat32
       and zero-filled this field for xstat64 when no native IRIX fs name existed. */
    return 0;
}

static void bridge_stat(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *path = guest_cstr(rt, (uint32_t)cpu->gpr[4]);
    struct stat st;
    int rc = path ? stat(path, &st) : -1;
    if (rc == 0 && write_irix_stat64(rt, (uint32_t)cpu->gpr[5], &st) != 0) rc = -1;
    ret_int(cpu, rc);
}

static void bridge_lstat(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *path = guest_cstr(rt, (uint32_t)cpu->gpr[4]);
    struct stat st;
    int rc = path ? lstat(path, &st) : -1;
    if (rc == 0 && write_irix_stat64(rt, (uint32_t)cpu->gpr[5], &st) != 0) rc = -1;
    ret_int(cpu, rc);
}

static void bridge_fstat(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    struct stat st;
    int rc = fstat((int)cpu->gpr[4], &st);
    if (rc == 0 && write_irix_stat64(rt, (uint32_t)cpu->gpr[5], &st) != 0) rc = -1;
    ret_int(cpu, rc);
}

static void bridge_open(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *path = guest_cstr(rt, (uint32_t)cpu->gpr[4]);
    uint32_t flags = (uint32_t)cpu->gpr[5];
    mode_t mode = (mode_t)(uint32_t)cpu->gpr[6];
    int rc = path ? open(path, host_open_flags(flags), mode) : -1;
    ret_int(cpu, rc);
}

static void bridge_creat(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *path = guest_cstr(rt, (uint32_t)cpu->gpr[4]);
    ret_int(cpu, path ? open(path, O_WRONLY | O_CREAT | O_TRUNC,
                             (mode_t)(uint32_t)cpu->gpr[5]) : -1);
}

static void bridge_close(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ (void)rt; ret_int(cpu, close((int)cpu->gpr[4])); }

static void bridge_read(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    uint32_t n = (uint32_t)cpu->gpr[6];
    void *buf = irix_guest_ptr(rt, (uint32_t)cpu->gpr[5], n);
    ssize_t rc = buf || !n ? read((int)cpu->gpr[4], buf, n) : -1;
    cpu->gpr[2] = (uint64_t)(int64_t)rc;
}

static void bridge_write(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    uint32_t n = (uint32_t)cpu->gpr[6];
    const void *buf = irix_guest_ptr(rt, (uint32_t)cpu->gpr[5], n);
    ssize_t rc = buf || !n ? write((int)cpu->gpr[4], buf, n) : -1;
    cpu->gpr[2] = (uint64_t)(int64_t)rc;
}

static void bridge_lseek(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    off_t rc;
    (void)rt;
    rc = lseek((int)cpu->gpr[4], (off_t)(int64_t)cpu->gpr[5], (int)cpu->gpr[6]);
    cpu->gpr[2] = (uint64_t)(int64_t)rc;
}

#define PATH1(name, call) \
static void bridge_##name(IrixGuestRuntime *rt, IrixGuestCpu *cpu) { \
    const char *a=guest_cstr(rt,(uint32_t)cpu->gpr[4]); ret_int(cpu,a?(call):-1); }
#define PATH2(name, call) \
static void bridge_##name(IrixGuestRuntime *rt, IrixGuestCpu *cpu) { \
    const char *a=guest_cstr(rt,(uint32_t)cpu->gpr[4]); \
    const char *b=guest_cstr(rt,(uint32_t)cpu->gpr[5]); ret_int(cpu,(a&&b)?(call):-1); }

PATH1(unlink, unlink(a))
PATH1(rmdir, rmdir(a))
PATH1(remove, remove(a))
PATH2(rename, rename(a,b))
PATH2(link, link(a,b))
PATH2(symlink, symlink(a,b))

static int host_mkdirp(const char *path, mode_t mode)
{
    char *copy;
    char *p;
    struct stat st;
    size_t n;

    if (!path || !*path) {
        errno = ENOENT;
        return -1;
    }

    n = strlen(path);
    copy = (char *)malloc(n + 1u);
    if (!copy) {
        errno = ENOMEM;
        return -1;
    }
    memcpy(copy, path, n + 1u);

    while (n > 1u && copy[n - 1u] == '/')
        copy[--n] = '\0';

    p = copy + (copy[0] == '/' ? 1 : 0);
    for (; *p; ++p) {
        if (*p != '/') continue;
        *p = '\0';
        if (*copy && mkdir(copy, mode) != 0 && errno != EEXIST) {
            int saved = errno;
            free(copy);
            errno = saved;
            return -1;
        }
        if (*copy && stat(copy, &st) != 0) {
            int saved = errno;
            free(copy);
            errno = saved;
            return -1;
        }
        if (*copy && !S_ISDIR(st.st_mode)) {
            free(copy);
            errno = ENOTDIR;
            return -1;
        }
        *p = '/';
    }

    if (mkdir(copy, mode) != 0 && errno != EEXIST) {
        int saved = errno;
        free(copy);
        errno = saved;
        return -1;
    }
    if (stat(copy, &st) != 0) {
        int saved = errno;
        free(copy);
        errno = saved;
        return -1;
    }
    if (!S_ISDIR(st.st_mode)) {
        free(copy);
        errno = ENOTDIR;
        return -1;
    }

    free(copy);
    return 0;
}

static void bridge_mkdir(IrixGuestRuntime *rt,IrixGuestCpu *cpu)
{ const char*a=guest_cstr(rt,(uint32_t)cpu->gpr[4]); ret_int(cpu,a?mkdir(a,(mode_t)(uint32_t)cpu->gpr[5]):-1); }
static void bridge_mkdirp(IrixGuestRuntime *rt,IrixGuestCpu *cpu)
{ const char*a=guest_cstr(rt,(uint32_t)cpu->gpr[4]); ret_int(cpu,a?host_mkdirp(a,(mode_t)(uint32_t)cpu->gpr[5]):-1); }
static void bridge_chmod(IrixGuestRuntime *rt,IrixGuestCpu *cpu)
{ const char*a=guest_cstr(rt,(uint32_t)cpu->gpr[4]); ret_int(cpu,a?chmod(a,(mode_t)(uint32_t)cpu->gpr[5]):-1); }
static void bridge_fchmod(IrixGuestRuntime *rt,IrixGuestCpu *cpu)
{ (void)rt; ret_int(cpu,fchmod((int)cpu->gpr[4],(mode_t)(uint32_t)cpu->gpr[5])); }
static void bridge_chown(IrixGuestRuntime *rt,IrixGuestCpu *cpu)
{ const char*a=guest_cstr(rt,(uint32_t)cpu->gpr[4]); ret_int(cpu,a?chown(a,(uid_t)(uint32_t)cpu->gpr[5],(gid_t)(uint32_t)cpu->gpr[6]):-1); }
static void bridge_fchown(IrixGuestRuntime *rt,IrixGuestCpu *cpu)
{ (void)rt; ret_int(cpu,fchown((int)cpu->gpr[4],(uid_t)(uint32_t)cpu->gpr[5],(gid_t)(uint32_t)cpu->gpr[6])); }

static void bridge_readlink(IrixGuestRuntime *rt,IrixGuestCpu *cpu)
{
    const char*a=guest_cstr(rt,(uint32_t)cpu->gpr[4]); uint32_t n=(uint32_t)cpu->gpr[6];
    char*b=(char*)irix_guest_ptr(rt,(uint32_t)cpu->gpr[5],n);
    ssize_t rc=(a&&(b||!n))?readlink(a,b,n):-1; cpu->gpr[2]=(uint64_t)(int64_t)rc;
}

static void bridge_dup(IrixGuestRuntime *rt,IrixGuestCpu *cpu)
{ (void)rt; ret_int(cpu,dup((int)cpu->gpr[4])); }
static void bridge_dup2(IrixGuestRuntime *rt,IrixGuestCpu *cpu)
{ (void)rt; ret_int(cpu,dup2((int)cpu->gpr[4],(int)cpu->gpr[5])); }
static void bridge_pipe(IrixGuestRuntime *rt,IrixGuestCpu *cpu)
{
    int fds[2]; int rc=pipe(fds); IrixGuestAddr p=(uint32_t)cpu->gpr[4];
    if(rc==0){irix_guest_write32be(rt,p,(uint32_t)fds[0]);irix_guest_write32be(rt,p+4u,(uint32_t)fds[1]);}
    ret_int(cpu,rc);
}
static void bridge_fsync(IrixGuestRuntime *rt,IrixGuestCpu *cpu)
{ (void)rt; ret_int(cpu,fsync((int)cpu->gpr[4])); }
static void bridge_fdatasync(IrixGuestRuntime *rt,IrixGuestCpu *cpu)
{
    (void)rt;
#ifdef __linux__
    ret_int(cpu,fdatasync((int)cpu->gpr[4]));
#else
    ret_int(cpu,fsync((int)cpu->gpr[4]));
#endif
}
static void bridge_isatty(IrixGuestRuntime *rt,IrixGuestCpu *cpu)
{ (void)rt; ret_int(cpu,isatty((int)cpu->gpr[4])); }
static void bridge_umask(IrixGuestRuntime *rt,IrixGuestCpu *cpu)
{ (void)rt; cpu->gpr[2]=(uint32_t)umask((mode_t)(uint32_t)cpu->gpr[4]); }
static void bridge_truncate(IrixGuestRuntime *rt,IrixGuestCpu *cpu)
{ const char*a=guest_cstr(rt,(uint32_t)cpu->gpr[4]); ret_int(cpu,a?truncate(a,(off_t)(int64_t)cpu->gpr[5]):-1); }
static void bridge_ftruncate(IrixGuestRuntime *rt,IrixGuestCpu *cpu)
{ (void)rt; ret_int(cpu,ftruncate((int)cpu->gpr[4],(off_t)(int64_t)cpu->gpr[5])); }

void irix_fs_bridge_init(IrixGuestRuntime *rt)
{ g_rt=rt; (void)g_rt; }

IrixGuestFunction irix_fs_bridge_lookup(const char *name)
{
    if(!name)return 0;
#define MAP(n) if(!strcmp(name,#n))return bridge_##n
    MAP(stat); MAP(lstat); MAP(fstat);
    MAP(open); MAP(creat); MAP(close); MAP(read); MAP(write); MAP(lseek);
    MAP(unlink); MAP(remove); MAP(rename); MAP(link); MAP(symlink); MAP(readlink);
    MAP(mkdir); MAP(mkdirp); MAP(rmdir); MAP(chmod); MAP(fchmod); MAP(chown); MAP(fchown);
    MAP(dup); MAP(dup2); MAP(pipe); MAP(fsync); MAP(fdatasync); MAP(isatty); MAP(umask);
    MAP(truncate); MAP(ftruncate);
#undef MAP
    return 0;
}
