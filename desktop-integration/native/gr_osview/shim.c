#include "irixcompat.h"
#include <sys/syscall.h>
#include <stdarg.h>
#include <errno.h>
#include "IrixKernelInfo.h"

#define MP_NPROCS 1
#define MP_NAPROCS 2
#define MP_KERNADDR 8
#define MP_SASZ 9
#define MP_SAGET 10
#define MP_SAGET1 15

#define KMEM_OFF_PHYSMEM 0x100u
#define KMEM_OFF_VAR 0x1000u
#define KMEM_OFF_BUF 0x2000u
#define KMEM_OFF_GFXINFO 0x3000u

int irix_native_ncpu_override;

int sysmp(int cmd, ...)
{
    va_list ap;
    int sub, cpu = -1;
    char *buf;
    int size, rc;
    uint32_t words[1280];
    size_t need;

    va_start(ap, cmd);
    switch (cmd) {
    case MP_NPROCS:
    case MP_NAPROCS:
        va_end(ap);
        return irix_kinfo_nprocs();
    case MP_KERNADDR:
        sub = va_arg(ap, int);
        va_end(ap);
        switch (sub) {
        case 1:  return (int)KMEM_OFF_BUF;
        case 2:  return (int)KMEM_OFF_VAR;
        case 20: return (int)KMEM_OFF_PHYSMEM;
        default: return -1;
        }
    case MP_SASZ:
        sub = va_arg(ap, int);
        va_end(ap);
        need = irix_kinfo_sa_size(sub);
        if (!need) { errno = EINVAL; return -1; }
        return (int)need;
    case MP_SAGET:
    case MP_SAGET1:
        sub = va_arg(ap, int);
        buf = va_arg(ap, char *);
        size = va_arg(ap, int);
        if (cmd == MP_SAGET1)
            cpu = va_arg(ap, int);
        va_end(ap);
        need = irix_kinfo_sa_size(sub);
        if (!need || need > sizeof(words) || (size_t)size < need) { errno = EINVAL; return -1; }
        rc = irix_kinfo_sa_get(sub, cpu, words, need / 4);
        if (rc != 0) { errno = EINVAL; return -1; }
        memcpy(buf, words, need);
        return 0;
    default:
        va_end(ap);
        errno = EINVAL;
        return -1;
    }
}

pid_t irix_sproc(void (*entry)(void *), int flags, void *arg)
{
    pid_t pid = fork();
    (void)flags;
    if (pid == 0)
        _exit(0);
    if (pid > 0)
        entry(arg);
    return pid;
}

void stg_open(void) {}

long long stg_getval(char *s)
{
    (void)s;
    return 0;
}

void stg_sptr_read(char *s, long long *buf)
{
    (void)s;
    *buf = KMEM_OFF_GFXINFO;
}

void stg_vread(long long addr, void *buf, int len)
{
    long pages = sysconf(_SC_PHYS_PAGES), psz = sysconf(_SC_PAGESIZE);
    memset(buf, 0, (size_t)len);
    if (addr == KMEM_OFF_PHYSMEM && len >= 4) {
        uint64_t bytes = (pages > 0 && psz > 0) ? (uint64_t)pages * (uint64_t)psz : 1ULL << 30;
        *(uint32_t *)buf = (uint32_t)(bytes / 16384u);
    }
}

int irix_rcmd(char **ahost, unsigned short rport, char *locuser, char *remuser, char *cmd, int *fd2p)
{
    (void)ahost; (void)rport; (void)locuser; (void)remuser; (void)cmd; (void)fd2p;
    return -1;
}

#include <sys/ioctl.h>
#undef ioctl
int irix_ioctl(int fd, unsigned long req, ...)
{
    va_list ap;
    void *arg;
    va_start(ap, req);
    arg = va_arg(ap, void *);
    va_end(ap);
    if (req == SIOCGIFSTATS) {
        struct ifreq *ifr = (struct ifreq *)arg;
        FILE *f = fopen("/proc/net/dev", "r");
        char line[512];
        memset(&ifr->ifr_ifru.ifru_stats, 0, sizeof(struct ifstats));
        if (!f) { errno = ENXIO; return -1; }
        while (fgets(line, sizeof(line), f)) {
            char name[64];
            unsigned long long v[16];
            char *c = strchr(line, ':');
            if (!c) continue;
            *c = 0;
            if (sscanf(line, " %63s", name) != 1) continue;
            if (strcmp(name, ifr->ifr_ifrn.ifrn_name) != 0) continue;
            if (sscanf(c + 1, "%llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
                       &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7],
                       &v[8], &v[9], &v[10], &v[11], &v[12], &v[13], &v[14], &v[15]) >= 14) {
                struct ifstats *st = &ifr->ifr_ifru.ifru_stats;
                st->ifs_ipackets = (unsigned int)v[1];
                st->ifs_ierrors = (unsigned short)v[2];
                st->ifs_opackets = (unsigned int)v[9];
                st->ifs_oerrors = (unsigned short)v[10];
                st->ifs_collisions = (unsigned int)v[13];
            }
            fclose(f);
            return 0;
        }
        fclose(f);
        errno = ENXIO;
        return -1;
    }
    return syscall(54 /* SYS_ioctl on i386 */, fd, req, arg);
}

#include <sys/statfs.h>
#include <sys/vfs.h>
#undef statfs
#undef fstatfs

int irix_statfs(const char *path, struct statfs *buf, int len, int fstyp)
{
    (void)len; (void)fstyp;
    return statfs(path, buf);
}

int irix_fstatfs(int fd, struct statfs *buf, int len, int fstyp)
{
    (void)len; (void)fstyp;
    return fstatfs(fd, buf);
}

int sysfs(int opcode, const char *name)
{
    (void)opcode; (void)name;
    return 1;
}

static long meminfo_swap_blocks(const char *key)
{
    FILE *f = fopen("/proc/meminfo", "r");
    char line[256];
    long kb = 0;
    size_t n = strlen(key);
    if (!f) return 0;
    while (fgets(line, sizeof(line), f))
        if (!strncmp(line, key, n) && line[n] == ':') { kb = atol(line + n + 1); break; }
    fclose(f);
    return kb * 2;
}

int swapctl(int cmd, void *arg)
{
    long total = meminfo_swap_blocks("SwapTotal");
    long freeb = meminfo_swap_blocks("SwapFree");
    long v;
    switch (cmd) {
    case 1: v = freeb; break;
    case 2: v = total; break;
    case 3: v = total; break;
    case 4: v = total; break;
    case 5: v = total; break;
    case 6: v = total - freeb; break;
    default: errno = EINVAL; return -1;
    }
    if (arg) *(long *)arg = v;
    return 0;
}

#include <invent.h>

static inventory_t inv_buf[64];
static IrixKinfoInvent inv_raw[64];
static size_t inv_count, inv_pos;
static int inv_loaded;

inventory_t *getinvent(void)
{
    if (!inv_loaded) {
        size_t i;
        inv_count = irix_kinfo_inventory(inv_raw, 64);
        for (i = 0; i < inv_count; ++i) {
            memset(&inv_buf[i], 0, sizeof(inv_buf[i]));
            inv_buf[i].inv_class = inv_raw[i].inv_class;
            inv_buf[i].inv_type = inv_raw[i].inv_type;
            inv_buf[i].inv_controller = inv_raw[i].inv_controller;
            inv_buf[i].inv_unit = inv_raw[i].inv_unit;
            inv_buf[i].inv_state = inv_raw[i].inv_state;
        }
        inv_loaded = 1;
    }
    if (inv_pos >= inv_count)
        return NULL;
    return &inv_buf[inv_pos++];
}

int setinvent(void)
{
    inv_pos = 0;
    return 0;
}

void endinvent(void)
{
    inv_pos = 0;
}
