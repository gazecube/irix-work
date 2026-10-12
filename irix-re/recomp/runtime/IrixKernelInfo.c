#include "IrixKernelInfo.h"

#include <stdio.h>
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
