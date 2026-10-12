#include "IrixKernelInfo.h"

#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Constants from IRIX <sys/invent.h> and <sys/sbd.h>. */
#define INV_PROCESSOR    1
#define INV_MEMORY       3
#define INV_CPUBOARD     1
#define INV_CPUCHIP      2
#define INV_FPUCHIP      3
#define INV_DCACHE       3
#define INV_ICACHE       4
#define INV_SIDCACHE     8
#define INV_MAIN_MB      9
#define INV_IP30BOARD    16
#define C0_IMP_R10000    0x09

int irix_kinfo_nprocs(void)
{
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (int)n : 1;
}

static int host_cpu_mhz(void)
{
    FILE *f = fopen("/proc/cpuinfo", "r");
    char line[256];
    double mhz = 0.0;
    if (f) {
        while (fgets(line, sizeof(line), f)) {
            if (!strncmp(line, "cpu MHz", 7)) {
                const char *c = strchr(line, ':');
                if (c) mhz = atof(c + 1);
                break;
            }
        }
        fclose(f);
    }
    return mhz > 0.0 ? (int)(mhz + 0.5) : 100;
}

static long host_mem_mb(void)
{
    FILE *f = fopen("/proc/meminfo", "r");
    char line[256];
    long kb = 0;
    if (f) {
        while (fgets(line, sizeof(line), f)) {
            if (sscanf(line, "MemTotal: %ld kB", &kb) == 1)
                break;
        }
        fclose(f);
    }
    return kb > 0 ? (kb + 512) / 1024 : 64;
}

/* Size in bytes of the largest cache of the given level (from sysfs). */
static long host_cache_bytes(int level, long fallback)
{
    char path[160], buf[64];
    long best = 0;
    int i;
    for (i = 0; i < 8; ++i) {
        FILE *f;
        int lvl = 0;
        long v = 0;
        char unit = 0;
        snprintf(path, sizeof(path),
                 "/sys/devices/system/cpu/cpu0/cache/index%d/level", i);
        f = fopen(path, "r");
        if (!f) continue;
        if (fscanf(f, "%d", &lvl) != 1) lvl = 0;
        fclose(f);
        if (lvl != level) continue;
        snprintf(path, sizeof(path),
                 "/sys/devices/system/cpu/cpu0/cache/index%d/size", i);
        f = fopen(path, "r");
        if (!f) continue;
        if (fgets(buf, sizeof(buf), f) && sscanf(buf, "%ld%c", &v, &unit) >= 1) {
            if (unit == 'K') v *= 1024;
            else if (unit == 'M') v *= 1024 * 1024;
            if (v > best) best = v;
        }
        fclose(f);
    }
    return best ? best : fallback;
}

static void put(IrixKinfoInvent *r, int cls, int type, int ctrl, int unit,
                int state)
{
    r->inv_class = cls;
    r->inv_type = type;
    r->inv_controller = ctrl;
    r->inv_unit = unit;
    r->inv_state = state;
}

size_t irix_kinfo_inventory(IrixKinfoInvent *out, size_t max)
{
    /* The host is presented as an IP30 (Octane) class machine with an
     * R10000 CPU: that is a combination hinv(1) and the desktop know how to
     * describe.  Frequency, memory size and cache sizes come from the host. */
    const int cpu_rev = (C0_IMP_R10000 << 8) | (2 << 4) | 6;
    size_t n = 0;
    if (n < max) put(&out[n++], INV_PROCESSOR, INV_CPUBOARD,
                     host_cpu_mhz(), -1, INV_IP30BOARD);
    if (n < max) put(&out[n++], INV_PROCESSOR, INV_CPUCHIP, 0, -1, cpu_rev);
    if (n < max) put(&out[n++], INV_PROCESSOR, INV_FPUCHIP, 0, -1, cpu_rev);
    if (n < max) put(&out[n++], INV_MEMORY, INV_DCACHE, 0, 0,
                     (int)host_cache_bytes(1, 32 * 1024));
    if (n < max) put(&out[n++], INV_MEMORY, INV_ICACHE, 0, 0,
                     (int)host_cache_bytes(1, 32 * 1024));
    if (n < max) put(&out[n++], INV_MEMORY, INV_SIDCACHE, 0, -1,
                     (int)host_cache_bytes(2, 1024 * 1024));
    if (n < max) put(&out[n++], INV_MEMORY, INV_MAIN_MB, 0, 0,
                     (int)host_mem_mb());
    return n;
}

/* ---- system activity -------------------------------------------------- */

#define SINFO_WORDS 70u   /* struct sysinfo, 280 bytes */
#define MINFO_WORDS 48u   /* struct minfo, 192 bytes   */
#define DINFO_WORDS 21u
#define SERR_WORDS   6u
#define RMINFO_WORDS 12u
#define SINFO_CPU_WORDS 11u

/* struct sysinfo word offsets */
enum {
    SI_CPU = 0, SI_WAIT = 6, SI_BREAD = 11, SI_BWRITE, SI_LREAD, SI_LWRITE,
    SI_PHREAD, SI_PHWRITE, SI_SWAPIN, SI_SWAPOUT, SI_BSWAPIN, SI_BSWAPOUT,
    SI_PSWAPOUT, SI_PSWITCH, SI_SYSCALL, SI_REMCALL, SI_FILEOP, SI_SERVE,
    SI_SYSREAD, SI_SYSWRITE, SI_SYSFORK, SI_SYSEXEC, SI_RUNQUE, SI_RUNOCC,
    SI_SWPQUE, SI_SWPOCC, SI_IGET, SI_NAMEI, SI_DIRBLK, SI_READCH,
    SI_WRITECH, SI_RCVINT, SI_XMTINT, SI_MDMINT, SI_RAWCH, SI_CANCH,
    SI_OUTCH, SI_MSG, SI_SEMA, SI_PNPFAULT, SI_WRTFAULT, SI_PTC, SI_PTS,
    SI_INTR_SVCD
};
/* struct minfo word offsets */
enum {
    MI_FREEMEM = 0, MI_FREESWAP = 2, MI_VFAULT, MI_DEMAND, MI_SWAP, MI_CACHE,
    MI_FILE, MI_PFAULT, MI_CW, MI_STEAL, MI_FREEDPGS, MI_UNMODSW, MI_UNMODFL,
    MI_TLBPIDS, MI_TFAULT, MI_RFAULT, MI_TLBFLUSH, MI_TLBSYNC, MI_TVIRT,
    MI_TPHYS, MI_TWRAP, MI_TDIRT, MI_HALLOC, MI_HFREE, MI_HEAPMEM, MI_HOVHD,
    MI_HUNUSED, MI_ICLEAN, MI_SFAULT = 30, MI_BSDNET = 32, MI_PALLOC
};

#define IRIX_PAGE_BYTES 16384u   /* IRIX page size on these machines */

typedef struct HostCpuTimes {
    unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;
    int valid;
} HostCpuTimes;

static int read_proc_stat(HostCpuTimes *cpus, int max, HostCpuTimes *total,
                          unsigned long long *ctxt, unsigned long long *forks,
                          unsigned long long *intr, unsigned long long *running)
{
    FILE *f = fopen("/proc/stat", "r");
    char line[1024];
    int ncpu = 0;
    if (!f) return 0;
    memset(total, 0, sizeof(*total));
    *ctxt = *forks = *intr = *running = 0;
    while (fgets(line, sizeof(line), f)) {
        HostCpuTimes t;
        int idx;
        memset(&t, 0, sizeof(t));
        if (!strncmp(line, "cpu ", 4)) {
            sscanf(line, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
                   &total->user, &total->nice, &total->system, &total->idle,
                   &total->iowait, &total->irq, &total->softirq, &total->steal);
        } else if (!strncmp(line, "cpu", 3) &&
                   sscanf(line, "cpu%d %llu %llu %llu %llu %llu %llu %llu %llu",
                   &idx, &t.user, &t.nice, &t.system, &t.idle, &t.iowait,
                   &t.irq, &t.softirq, &t.steal) >= 5 && idx >= 0) {
            t.valid = 1;
            if (idx < max) { cpus[idx] = t; if (idx + 1 > ncpu) ncpu = idx + 1; }
        } else if (!strncmp(line, "ctxt ", 5)) {
            sscanf(line + 5, "%llu", ctxt);
        } else if (!strncmp(line, "processes ", 10)) {
            sscanf(line + 10, "%llu", forks);
        } else if (!strncmp(line, "intr ", 5)) {
            sscanf(line + 5, "%llu", intr);
        } else if (!strncmp(line, "procs_running ", 14)) {
            sscanf(line + 14, "%llu", running);
        }
    }
    fclose(f);
    return ncpu;
}

static unsigned long long vmstat_value(const char *name)
{
    FILE *f = fopen("/proc/vmstat", "r");
    char key[64];
    unsigned long long v, out = 0;
    if (!f) return 0;
    while (fscanf(f, "%63s %llu", key, &v) == 2) {
        if (!strcmp(key, name)) { out = v; break; }
    }
    fclose(f);
    return out;
}

static unsigned long long meminfo_kb(const char *name)
{
    FILE *f = fopen("/proc/meminfo", "r");
    char line[256];
    size_t n = strlen(name);
    unsigned long long v = 0;
    if (!f) return 0;
    while (fgets(line, sizeof(line), f)) {
        if (!strncmp(line, name, n) && line[n] == ':') {
            sscanf(line + n + 1, "%llu", &v);
            break;
        }
    }
    fclose(f);
    return v;
}

static unsigned long long diskstats_sum(int sectors_col /* 0 read, 1 write */)
{
    FILE *f = fopen("/proc/diskstats", "r");
    char line[512], name[64];
    unsigned long long total = 0;
    if (!f) return 0;
    while (fgets(line, sizeof(line), f)) {
        unsigned maj, min;
        unsigned long long r_ios, r_merge, r_sec, r_ms, w_ios, w_merge, w_sec;
        if (sscanf(line, "%u %u %63s %llu %llu %llu %llu %llu %llu %llu",
                   &maj, &min, name, &r_ios, &r_merge, &r_sec, &r_ms,
                   &w_ios, &w_merge, &w_sec) < 10)
            continue;
        /* whole disks only: skip partitions and virtual devices */
        if (!strncmp(name, "loop", 4) || !strncmp(name, "ram", 3) ||
            !strncmp(name, "dm-", 3) || !strncmp(name, "zram", 4))
            continue;
        {
            size_t l = strlen(name);
            int part = (l > 0 && name[l - 1] >= '0' && name[l - 1] <= '9' &&
                        strncmp(name, "nvme", 4) != 0 && strncmp(name, "mmcblk", 6) != 0) ||
                       ((!strncmp(name, "nvme", 4) || !strncmp(name, "mmcblk", 6)) &&
                        strchr(name, 'p') && strrchr(name, 'p') > name + 4);
            if (part) continue;
        }
        total += sectors_col == 0 ? r_ios : w_ios;
    }
    fclose(f);
    return total;
}

static void cpu_to_words(const HostCpuTimes *t, uint32_t *w)
{
    w[0] = (uint32_t)t->idle;
    w[1] = (uint32_t)(t->user + t->nice);
    w[2] = (uint32_t)(t->system + t->steal);
    w[3] = (uint32_t)t->iowait;
    w[4] = 0;                                   /* CPU_SXBRK */
    w[5] = (uint32_t)(t->irq + t->softirq);
    w[SI_WAIT - SI_CPU + 0] = (uint32_t)t->iowait;   /* W_IO */
    w[SI_WAIT - SI_CPU + 1] = 0;                     /* W_SWAP */
    w[SI_WAIT - SI_CPU + 2] = 0;                     /* W_PIO */
    w[SI_WAIT - SI_CPU + 3] = 0;                     /* W_GFXC */
    w[SI_WAIT - SI_CPU + 4] = 0;                     /* W_GFXF */
}

size_t irix_kinfo_sa_size(int sub)
{
    switch (sub) {
    case IRIX_KINFO_MPSA_SINFO:     return SINFO_WORDS * 4u;
    case IRIX_KINFO_MPSA_MINFO:     return MINFO_WORDS * 4u;
    case IRIX_KINFO_MPSA_DINFO:     return DINFO_WORDS * 4u;
    case IRIX_KINFO_MPSA_SERR:      return SERR_WORDS * 4u;
    case IRIX_KINFO_MPSA_RMINFO:    return RMINFO_WORDS * 4u;
    case IRIX_KINFO_MPSA_SINFO_CPU: return SINFO_CPU_WORDS * 4u;
    /* Sizes below are sizeof() of the IRIX 6.5.7m N32 structures. */
    case 5:  return 64u;     /* MPSA_NCSTATS    struct ncstats        */
    case 6:  return 100u;    /* MPSA_EFS        struct igetstats      */
    case 9:  return 168u;    /* MPSA_BUFINFO    struct getblkstats    */
    case 13: return 72u;     /* MPSA_VOPINFO    struct vnodestats     */
    case 14: return 4824u;   /* MPSA_TCPIPSTATS struct kna            */
    case 20: return 296u;    /* MPSA_XFSSTATS   struct xfsstats       */
    case 25: return 32u;     /* MPSA_NODE_INFO  struct nodeinfo       */
    case 26: return 28u;     /* MPSA_LPGSTATS   struct lpg_stat_info  */
    default:                        return 0;
    }
}

int irix_kinfo_sa_get(int sub, int cpu, uint32_t *w, size_t nwords)
{
    HostCpuTimes cpus[256], total;
    unsigned long long ctxt, forks, intr, running;
    int ncpu = read_proc_stat(cpus, 256, &total, &ctxt, &forks, &intr, &running);
    size_t want = irix_kinfo_sa_size(sub) / 4u;
    size_t i;

    if (!want || nwords < want) return -1;
    memset(w, 0, want * 4u);

    switch (sub) {
    case IRIX_KINFO_MPSA_SINFO:
    case IRIX_KINFO_MPSA_SINFO_CPU: {
        HostCpuTimes sel;
        int n = ncpu > 0 ? ncpu : 1;
        if (cpu >= 0) {
            if (cpu >= n || (ncpu > 0 && !cpus[cpu].valid)) return -1;
            sel = cpus[cpu];
        } else {
            sel = total;
            if (!ncpu) memset(&sel, 0, sizeof(sel));
        }
        cpu_to_words(&sel, w);
        if (sub == IRIX_KINFO_MPSA_SINFO_CPU)
            break;
        /* Counters the host keeps system-wide are reported on the sum and
         * on CPU 0 only, so per-CPU views still add up. */
        if (cpu < 0 || cpu == 0) {
            w[SI_BREAD]    = (uint32_t)diskstats_sum(0);
            w[SI_BWRITE]   = (uint32_t)diskstats_sum(1);
            w[SI_PHREAD]   = w[SI_BREAD];
            w[SI_PHWRITE]  = w[SI_BWRITE];
            w[SI_SWAPIN]   = (uint32_t)vmstat_value("pswpin");
            w[SI_SWAPOUT]  = (uint32_t)vmstat_value("pswpout");
            w[SI_PSWITCH]  = (uint32_t)ctxt;
            w[SI_SYSFORK]  = (uint32_t)forks;
            w[SI_RUNQUE]   = (uint32_t)running;
            w[SI_RUNOCC]   = running ? (uint32_t)total.user : 0u;
            w[SI_PNPFAULT] = (uint32_t)vmstat_value("pgfault");
            w[SI_WRTFAULT] = (uint32_t)vmstat_value("pgmajfault");
            w[SI_INTR_SVCD] = (uint32_t)intr;
        }
        break;
    }
    case IRIX_KINFO_MPSA_MINFO: {
        unsigned long long freekb = meminfo_kb("MemFree");
        unsigned long long cachekb = meminfo_kb("Cached");
        unsigned long long swapfree = meminfo_kb("SwapFree");
        w[MI_FREEMEM + 1] = (uint32_t)(freekb * 1024ull / IRIX_PAGE_BYTES);
        w[MI_FREESWAP] = (uint32_t)(swapfree * 2ull);        /* 512-byte blocks */
        w[MI_VFAULT]   = (uint32_t)vmstat_value("pgfault");
        w[MI_DEMAND]   = (uint32_t)vmstat_value("pgalloc_normal");
        w[MI_SWAP]     = (uint32_t)(vmstat_value("pswpin") + vmstat_value("pswpout"));
        w[MI_CACHE]    = (uint32_t)(cachekb * 1024ull / IRIX_PAGE_BYTES);
        w[MI_PFAULT]   = (uint32_t)vmstat_value("pgmajfault");
        w[MI_STEAL]    = (uint32_t)(vmstat_value("pgsteal_kswapd") +
                                    vmstat_value("pgsteal_direct"));
        w[MI_FREEDPGS] = (uint32_t)vmstat_value("pgfree");
        w[MI_PALLOC]   = (uint32_t)vmstat_value("pgalloc_normal");
        break;
    }
    case IRIX_KINFO_MPSA_RMINFO: {
        unsigned long long totkb = meminfo_kb("MemTotal");
        unsigned long long freekb = meminfo_kb("MemFree");
        unsigned long long availkb = meminfo_kb("MemAvailable");
        unsigned long long bufkb = meminfo_kb("Buffers");
        unsigned long long dirtykb = meminfo_kb("Dirty");
        unsigned long long swapfree = meminfo_kb("SwapFree");
        #define PG(kb) ((uint32_t)((kb) * 1024ull / IRIX_PAGE_BYTES))
        w[0]  = PG(freekb);                     /* freemem   */
        w[1]  = PG(availkb + swapfree);         /* availsmem */
        w[2]  = PG(availkb);                    /* availrmem */
        w[3]  = PG(bufkb);                      /* bufmem    */
        w[4]  = PG(totkb);                      /* physmem   */
        w[5]  = PG(dirtykb);                    /* dchunkpages */
        w[10] = PG(freekb);                     /* emptymem  */
        w[11] = PG(availkb);                    /* ravailrmem */
        #undef PG
        break;
    }
    case IRIX_KINFO_MPSA_DINFO:
    case IRIX_KINFO_MPSA_SERR:
        break;                                  /* all zero: no RFS, no overflow */
    case 25: {                                  /* struct nodeinfo, one node */
        unsigned long long totkb = meminfo_kb("MemTotal");
        unsigned long long freekb = meminfo_kb("MemFree");
        w[0] = 0;                               /* node_device */
        w[1] = (uint32_t)(totkb * 1024ull);     /* totalmem (bytes) */
        w[2] = (uint32_t)(freekb * 1024ull);    /* freemem  (bytes) */
        break;
    }
    case 5: case 6: case 9: case 13: case 14: case 20: case 26:
        break;                                  /* counters not kept: zero */
    default:
        return -1;
    }
    (void)i;
    return 0;
}

long irix_kinfo_scache_bytes(void)
{
    return host_cache_bytes(2, 1024 * 1024);
}

int irix_kinfo_scache_assoc(void)
{
    return 2;
}

/* ---- sysget(SGT_KSYM) objects ----------------------------------------- */

typedef struct KsymDef { const char *name; int words; } KsymDef;
static const KsymDef ksyms[] = {
    { "avenrun", 3 },          { "physmem", 1 },        { "kpbase", 1 },
    { "freemem", 1 },          { "usermem", 1 },        { "dchunkpages", 1 },
    { "bufmem", 1 },           { "chunkpages", 1 },     { "maxclick", 1 },
    { "physmem_start", 1 },    { "syssegsz", 1 },       { "vn_vnumber", 1 },
    { "vn_epoch", 1 },         { "vn_nfree", 1 },       { "min_file_pages", 1 },
    { "min_free_pages", 1 },   { "swplo", 1 },          { "time", 1 },
    { "splockmeter", 1 },
};

int irix_kinfo_ksym_info(const char *name, int *size, int *num)
{
    size_t i;
    if (!name) return -1;
    if (!strcmp(name, "gfxinfo")) {      /* struct gfxinfo (8 words) per cpu */
        *size = 32;
        *num = irix_kinfo_nprocs();
        return 0;
    }
    for (i = 0; i < sizeof(ksyms) / sizeof(ksyms[0]); ++i) {
        if (!strcmp(name, ksyms[i].name)) {
            *size = ksyms[i].words * 4;
            *num = 1;
            return 0;
        }
    }
    return -1;
}

int irix_kinfo_ksym_get(const char *name, uint32_t *w, size_t nwords)
{
    int size, num;
    size_t total, i;
    unsigned long long totkb, freekb, availkb, bufkb, dirtykb;
    if (irix_kinfo_ksym_info(name, &size, &num) != 0) return -1;
    total = (size_t)size / 4u * (size_t)num;
    if (nwords < total) return -1;
    memset(w, 0, total * 4u);
    totkb = meminfo_kb("MemTotal");
    freekb = meminfo_kb("MemFree");
    availkb = meminfo_kb("MemAvailable");
    bufkb = meminfo_kb("Buffers");
    dirtykb = meminfo_kb("Dirty");
    #define PG(kb) ((uint32_t)((kb) * 1024ull / IRIX_PAGE_BYTES))
    if (!strcmp(name, "avenrun")) {
        double a[3] = { 0, 0, 0 };
        FILE *f = fopen("/proc/loadavg", "r");
        if (f) {
            if (fscanf(f, "%lf %lf %lf", &a[0], &a[1], &a[2]) != 3)
                a[0] = a[1] = a[2] = 0;
            fclose(f);
        }
        for (i = 0; i < 3; ++i)
            w[i] = (uint32_t)(a[i] * 1024.0 + 0.5);
    } else if (!strcmp(name, "physmem") || !strcmp(name, "maxclick")) {
        w[0] = PG(totkb);
    } else if (!strcmp(name, "freemem")) {
        w[0] = PG(freekb);
    } else if (!strcmp(name, "usermem")) {
        w[0] = PG(availkb);
    } else if (!strcmp(name, "dchunkpages")) {
        w[0] = PG(dirtykb);
    } else if (!strcmp(name, "bufmem")) {
        w[0] = PG(bufkb);
    } else if (!strcmp(name, "vn_vnumber")) {
        w[0] = 1;
    } else if (!strcmp(name, "time")) {
        w[0] = (uint32_t)time(0);
    }
    #undef PG
    return 0;
}
