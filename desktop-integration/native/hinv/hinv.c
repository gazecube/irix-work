#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <limits.h>
#include <sys/stat.h>

struct cpu_info {
    char model[256];
    char vendor[64];
    int family, model_no, stepping;
    int logical;
    int cores;
    double base_mhz;
    double max_mhz;
    char vector[32];
};

struct cache_info {
    long l1d, l1i, l2, l3;
};

static void trim(char *s)
{
    char *p = s;
    size_t n;
    while (isspace((unsigned char)*p)) p++;
    memmove(s, p, strlen(p) + 1);
    n = strlen(s);
    while (n && isspace((unsigned char)s[n - 1])) s[--n] = 0;
}

static int has_flag(const char *flags, const char *flag)
{
    size_t n = strlen(flag);
    const char *p = flags;
    while ((p = strstr(p, flag)) != NULL) {
        if ((p == flags || p[-1] == ' ') && (p[n] == ' ' || p[n] == 0))
            return 1;
        p += n;
    }
    return 0;
}

static void read_cpu(struct cpu_info *c)
{
    FILE *f = fopen("/proc/cpuinfo", "r");
    char line[8192], flags[8192] = "";
    double cur_mhz = 0;
    int physical_seen = 0;
    int seen_model = 0;
    memset(c, 0, sizeof(*c));
    c->family = c->model_no = c->stepping = -1;
    if (f) {
        while (fgets(line, sizeof(line), f)) {
            char *v = strchr(line, ':');
            if (!v) continue;
            *v++ = 0;
            trim(line);
            trim(v);
            if (!strcmp(line, "processor")) {
                c->logical++;
            } else if (!seen_model && !strcmp(line, "model name")) {
                snprintf(c->model, sizeof(c->model), "%s", v);
                seen_model = 1;
            } else if (!c->vendor[0] && !strcmp(line, "vendor_id")) {
                snprintf(c->vendor, sizeof(c->vendor), "%s", v);
            } else if (c->family < 0 && !strcmp(line, "cpu family")) {
                c->family = atoi(v);
            } else if (c->model_no < 0 && !strcmp(line, "model")) {
                c->model_no = atoi(v);
            } else if (c->stepping < 0 && !strcmp(line, "stepping")) {
                c->stepping = atoi(v);
            } else if (!cur_mhz && !strcmp(line, "cpu MHz")) {
                cur_mhz = atof(v);
            } else if (!flags[0] && !strcmp(line, "flags")) {
                snprintf(flags, sizeof(flags), "%s", v);
            } else if (!physical_seen && !strcmp(line, "cpu cores")) {
                c->cores = atoi(v);
                physical_seen = 1;
            }
        }
        fclose(f);
    }
    if (c->logical < 1)
        c->logical = (int)sysconf(_SC_NPROCESSORS_ONLN);
    if (!c->model[0])
        snprintf(c->model, sizeof(c->model), "Unknown Processor");
    {
        const char *at = strstr(c->model, "@ ");
        if (at) {
            double g = atof(at + 2);
            if (strstr(at, "GHz")) c->base_mhz = g * 1000.0;
            else if (strstr(at, "MHz")) c->base_mhz = g;
        }
    }
    {
        FILE *m = fopen("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq", "r");
        if (m) {
            long khz = 0;
            if (fscanf(m, "%ld", &khz) == 1) c->max_mhz = khz / 1000.0;
            fclose(m);
        }
    }
    if (!c->base_mhz)
        c->base_mhz = c->max_mhz ? c->max_mhz : cur_mhz;
    if (!c->max_mhz)
        c->max_mhz = c->base_mhz;
    if (has_flag(flags, "avx512f")) snprintf(c->vector, sizeof(c->vector), "AVX-512");
    else if (has_flag(flags, "avx2")) snprintf(c->vector, sizeof(c->vector), "AVX2");
    else if (has_flag(flags, "avx")) snprintf(c->vector, sizeof(c->vector), "AVX");
    else if (has_flag(flags, "sse4_2")) snprintf(c->vector, sizeof(c->vector), "SSE4.2");
    else if (has_flag(flags, "sse2")) snprintf(c->vector, sizeof(c->vector), "SSE2");
    else snprintf(c->vector, sizeof(c->vector), "x87");
}

static long read_size_kb(const char *path)
{
    FILE *f = fopen(path, "r");
    char buf[64];
    long v = 0;
    if (!f) return 0;
    if (fgets(buf, sizeof(buf), f)) {
        v = atol(buf);
        if (strchr(buf, 'M')) v *= 1024;
        else if (strchr(buf, 'G')) v *= 1024 * 1024;
    }
    fclose(f);
    return v;
}

static void read_cache(struct cache_info *c)
{
    int i;
    memset(c, 0, sizeof(*c));
    for (i = 0; i < 8; ++i) {
        char p[160], type[32] = "";
        int level = 0;
        FILE *f;
        long kb;
        snprintf(p, sizeof(p), "/sys/devices/system/cpu/cpu0/cache/index%d/level", i);
        f = fopen(p, "r");
        if (!f) break;
        if (fscanf(f, "%d", &level) != 1) level = 0;
        fclose(f);
        snprintf(p, sizeof(p), "/sys/devices/system/cpu/cpu0/cache/index%d/type", i);
        f = fopen(p, "r");
        if (f) {
            if (fscanf(f, "%31s", type) != 1) type[0] = 0;
            fclose(f);
        }
        snprintf(p, sizeof(p), "/sys/devices/system/cpu/cpu0/cache/index%d/size", i);
        kb = read_size_kb(p);
        if (level == 1 && !strcmp(type, "Data")) c->l1d = kb;
        else if (level == 1 && !strcmp(type, "Instruction")) c->l1i = kb;
        else if (level == 2) c->l2 = kb;
        else if (level == 3) c->l3 = kb;
    }
}

static long mem_mbytes(void)
{
    long pages = sysconf(_SC_PHYS_PAGES), psz = sysconf(_SC_PAGESIZE);
    if (pages <= 0 || psz <= 0) return 0;
    return (long)(((long long)pages * psz) >> 20);
}

static void print_cache(const char *label, long kb)
{
    if (kb <= 0) return;
    if (kb >= 1024 && kb % 1024 == 0) {
        if (kb == 1024)
            printf("%s: 1 Mbyte\n", label);
        else
            printf("%s: %ld Mbytes\n", label, kb / 1024);
    } else {
        printf("%s: %ld Kbytes\n", label, kb);
    }
}

static void show_processors(const struct cpu_info *c, int verbose)
{
    printf("%d %.0f MHZ %s %s\n", c->logical, c->base_mhz, c->model,
           c->logical > 1 ? "Processors" : "Processor");
    if (verbose && c->cores > 0 && c->cores != c->logical)
        printf("  %d physical cores, %d threads\n", c->cores, c->logical);
    if (verbose && c->max_mhz > c->base_mhz)
        printf("  maximum clock %.0f MHZ\n", c->max_mhz);
    if (c->family >= 0)
        printf("CPU: %s Revision: %d.%d.%d\n", c->model, c->family,
               c->model_no, c->stepping);
    else
        printf("CPU: %s\n", c->model);
    printf("FPU: %s Floating Point Unit\n", c->vector);
}

static void show_cache(const struct cache_info *k)
{
    print_cache("Data cache size", k->l1d);
    print_cache("Instruction cache size", k->l1i);
    print_cache("Secondary unified instruction/data cache size", k->l2);
    print_cache("Tertiary unified instruction/data cache size", k->l3);
}


static int read_file(const char *path, char *buf, size_t n)
{
    FILE *f = fopen(path, "r");
    size_t k;
    if (!f) { buf[0] = 0; return -1; }
    k = fread(buf, 1, n - 1, f);
    fclose(f);
    buf[k] = 0;
    while (k && (buf[k - 1] == '\n' || buf[k - 1] == ' ')) buf[--k] = 0;
    return 0;
}

static void link_base(const char *path, char *out, size_t n)
{
    char tgt[PATH_MAX];
    ssize_t k = readlink(path, tgt, sizeof(tgt) - 1);
    const char *b;
    out[0] = 0;
    if (k <= 0) return;
    tgt[k] = 0;
    b = strrchr(tgt, '/');
    snprintf(out, n, "%s", b ? b + 1 : tgt);
}

static void pci_name(const char *vendor_hex, const char *device_hex, char *out, size_t n)
{
    static const char *paths[] = { "/usr/share/hwdata/pci.ids", "/usr/share/misc/pci.ids",
                                   "/usr/share/pci.ids", NULL };
    char line[512], vname[256] = "", dname[256] = "";
    char v[8], d[8];
    int i, in_vendor = 0;
    FILE *f = NULL;
    snprintf(v, sizeof(v), "%s", vendor_hex + (strncmp(vendor_hex, "0x", 2) ? 0 : 2));
    snprintf(d, sizeof(d), "%s", device_hex + (strncmp(device_hex, "0x", 2) ? 0 : 2));
    for (i = 0; paths[i] && !f; ++i) f = fopen(paths[i], "r");
    out[0] = 0;
    if (f) {
        while (fgets(line, sizeof(line), f)) {
            size_t l = strlen(line);
            while (l && (line[l - 1] == '\n')) line[--l] = 0;
            if (line[0] == '#' || !line[0]) continue;
            if (line[0] != '\t') {
                in_vendor = !strncasecmp(line, v, 4) && line[4] == ' ';
                if (in_vendor) snprintf(vname, sizeof(vname), "%s", line + 6);
                else if (vname[0]) break;
            } else if (in_vendor && line[1] != '\t' && !strncasecmp(line + 1, d, 4)) {
                snprintf(dname, sizeof(dname), "%s", line + 7);
                break;
            }
        }
        fclose(f);
    }
    if (vname[0] && dname[0]) snprintf(out, n, "%s %s", vname, dname);
    else if (vname[0]) snprintf(out, n, "%s device %s", vname, d);
    else snprintf(out, n, "PCI device %s:%s", v, d);
}

static int pci_addr_in_path(const char *real, char *out, size_t n)
{
    const char *p = real, *last = NULL;
    while ((p = strstr(p, "/0000:")) != NULL) {
        if (strlen(p) >= 13 && p[5] == ':' && p[8] == ':' && p[11] == '.') last = p;
        p += 1;
    }
    if (!last) return 0;
    snprintf(out, n, "%.12s", last + 1);
    return 1;
}

struct ctrl {
    char key[64];
    char driver[64];
    char name[320];
    int units;
};

static struct ctrl ctrls[16];
static int nctrls;

static int ctrl_for(const char *devpath)
{
    char real[PATH_MAX], pci[32], path[PATH_MAX], v[16], d[16], tmp[PATH_MAX];
    int i;
    if (!realpath(devpath, real)) return -1;
    if (pci_addr_in_path(real, pci, sizeof(pci)) == 0) snprintf(pci, sizeof(pci), "other");
    for (i = 0; i < nctrls; ++i)
        if (!strcmp(ctrls[i].key, pci)) return i;
    if (nctrls >= 16) return -1;
    memset(&ctrls[nctrls], 0, sizeof(ctrls[0]));
    snprintf(ctrls[nctrls].key, sizeof(ctrls[0].key), "%s", pci);
    if (strcmp(pci, "other")) {
        snprintf(path, sizeof(path), "/sys/bus/pci/devices/%s/driver", pci);
        link_base(path, ctrls[nctrls].driver, sizeof(ctrls[0].driver));
        snprintf(tmp, sizeof(tmp), "/sys/bus/pci/devices/%s/vendor", pci);
        read_file(tmp, v, sizeof(v));
        snprintf(tmp, sizeof(tmp), "/sys/bus/pci/devices/%s/device", pci);
        read_file(tmp, d, sizeof(d));
        pci_name(v, d, ctrls[nctrls].name, sizeof(ctrls[0].name));
    } else {
        snprintf(ctrls[nctrls].driver, sizeof(ctrls[0].driver), "virtual");
        snprintf(ctrls[nctrls].name, sizeof(ctrls[0].name), "host storage");
    }
    return nctrls++;
}

struct blk {
    char name[32];
    int ctrl;
    int is_cd;
    long long sectors;
    char model[128];
    char vendor[64];
};

static int blkcmp(const void *a, const void *b)
{
    return strcmp(((const struct blk *)a)->name, ((const struct blk *)b)->name);
}

static void show_storage(int verbose)
{
    struct blk blks[64];
    int nb = 0, i, c;
    DIR *d = opendir("/sys/block");
    struct dirent *e;
    if (!d) return;
    while ((e = readdir(d)) != NULL && nb < 64) {
        char p[PATH_MAX], t[64];
        struct blk *b = &blks[nb];
        if (e->d_name[0] == '.' || !strncmp(e->d_name, "loop", 4) || !strncmp(e->d_name, "ram", 3) ||
            !strncmp(e->d_name, "zram", 4) || !strncmp(e->d_name, "dm-", 3) || !strncmp(e->d_name, "md", 2) ||
            !strncmp(e->d_name, "nbd", 3) || !strncmp(e->d_name, "fd", 2))
            continue;
        memset(b, 0, sizeof(*b));
        snprintf(b->name, sizeof(b->name), "%s", e->d_name);
        snprintf(p, sizeof(p), "/sys/block/%s/device", e->d_name);
        if (access(p, F_OK) != 0) continue;
        b->ctrl = ctrl_for(p);
        b->is_cd = !strncmp(e->d_name, "sr", 2);
        snprintf(p, sizeof(p), "/sys/block/%s/size", e->d_name);
        read_file(p, t, sizeof(t));
        b->sectors = atoll(t);
        snprintf(p, sizeof(p), "/sys/block/%s/device/model", e->d_name);
        read_file(p, b->model, sizeof(b->model));
        snprintf(p, sizeof(p), "/sys/block/%s/device/vendor", e->d_name);
        read_file(p, b->vendor, sizeof(b->vendor));
        nb++;
    }
    closedir(d);
    qsort(blks, (size_t)nb, sizeof(blks[0]), blkcmp);
    for (c = 0; c < nctrls; ++c) {
        printf("PCI SCSI controller %d: Version %s", c, ctrls[c].driver[0] ? ctrls[c].driver : "unknown");
        if (verbose && ctrls[c].name[0]) printf(", %s", ctrls[c].name);
        printf("\n");
        for (i = 0; i < nb; ++i) {
            if (blks[i].ctrl != c) continue;
            printf("  %s: unit %d on SCSI controller %d", blks[i].is_cd ? "CDROM" : "Disk drive",
                   ctrls[c].units, c);
            if (verbose) {
                char mdl[200];
                snprintf(mdl, sizeof(mdl), "%s %s", blks[i].vendor, blks[i].model);
                trim(mdl);
                printf(" (%s", blks[i].name);
                if (mdl[0]) printf(", %s", mdl);
                if (blks[i].sectors > 0 && !blks[i].is_cd)
                    printf(", %.0f GB", (double)blks[i].sectors * 512.0 / 1e9);
                printf(")");
            }
            printf("\n");
            ctrls[c].units++;
        }
    }
}

static void show_network(int verbose)
{
    DIR *d = opendir("/sys/class/net");
    struct dirent *e;
    int ether = 0, wlan = 0;
    char names[64][32];
    int n = 0, i;
    if (!d) return;
    while ((e = readdir(d)) != NULL && n < 64) {
        char p[PATH_MAX];
        if (e->d_name[0] == '.' || !strcmp(e->d_name, "lo")) continue;
        snprintf(p, sizeof(p), "/sys/class/net/%s/device", e->d_name);
        if (access(p, F_OK) != 0) continue;
        snprintf(names[n++], sizeof(names[0]), "%s", e->d_name);
    }
    closedir(d);
    for (i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j)
            if (strcmp(names[i], names[j]) > 0) {
                char t[32];
                memcpy(t, names[i], 32); memcpy(names[i], names[j], 32); memcpy(names[j], t, 32);
            }
    for (i = 0; i < n; ++i) {
        char p[PATH_MAX], drv[64], real[PATH_MAX], pci[32], v[16], dv[16], name[320] = "";
        int wireless;
        snprintf(p, sizeof(p), "/sys/class/net/%s/wireless", names[i]);
        wireless = access(p, F_OK) == 0;
        snprintf(p, sizeof(p), "/sys/class/net/%s/device/driver", names[i]);
        link_base(p, drv, sizeof(drv));
        printf("%s: %s, driver %s", wireless ? "Wireless LAN" : "PCI Ethernet",
               names[i], drv[0] ? drv : "unknown");
        if (wireless) wlan++; else ether++;
        if (verbose) {
            snprintf(p, sizeof(p), "/sys/class/net/%s/device", names[i]);
            if (realpath(p, real) && pci_addr_in_path(real, pci, sizeof(pci))) {
                snprintf(p, sizeof(p), "/sys/bus/pci/devices/%s/vendor", pci);
                read_file(p, v, sizeof(v));
                snprintf(p, sizeof(p), "/sys/bus/pci/devices/%s/device", pci);
                read_file(p, dv, sizeof(dv));
                pci_name(v, dv, name, sizeof(name));
            }
            if (name[0]) printf(" (%s)", name);
            snprintf(p, sizeof(p), "/sys/class/net/%s/address", names[i]);
            read_file(p, v, sizeof(v));
            snprintf(p, sizeof(p), "/sys/class/net/%s/address", names[i]);
            {
                char mac[64];
                read_file(p, mac, sizeof(mac));
                if (mac[0]) printf(", address %s", mac);
            }
        }
        printf("\n");
    }
    (void)ether; (void)wlan;
}

static void show_graphics(int verbose)
{
    DIR *d = opendir("/sys/class/drm");
    struct dirent *e;
    int idx = 0;
    char cards[16][16];
    int nc = 0, i, j;
    if (!d) return;
    while ((e = readdir(d)) != NULL && nc < 16) {
        if (strncmp(e->d_name, "card", 4) || strchr(e->d_name, '-')) continue;
        snprintf(cards[nc++], sizeof(cards[0]), "%s", e->d_name);
    }
    closedir(d);
    for (i = 0; i < nc; ++i)
        for (j = i + 1; j < nc; ++j)
            if (strcmp(cards[i], cards[j]) > 0) {
                char t[16];
                memcpy(t, cards[i], 16); memcpy(cards[i], cards[j], 16); memcpy(cards[j], t, 16);
            }
    for (i = 0; i < nc; ++i) {
        char p[PATH_MAX], v[16], dv[16], drv[64], name[320];
        DIR *cd;
        snprintf(p, sizeof(p), "/sys/class/drm/%s/device/vendor", cards[i]);
        read_file(p, v, sizeof(v));
        snprintf(p, sizeof(p), "/sys/class/drm/%s/device/device", cards[i]);
        read_file(p, dv, sizeof(dv));
        snprintf(p, sizeof(p), "/sys/class/drm/%s/device/driver", cards[i]);
        link_base(p, drv, sizeof(drv));
        if (v[0]) pci_name(v, dv, name, sizeof(name));
        else snprintf(name, sizeof(name), "%s", cards[i]);
        printf("Graphics board: %s", name);
        if (drv[0]) printf(" (%s)", drv);
        printf("\n");
        idx++;
        if (verbose) {
            snprintf(p, sizeof(p), "/sys/class/drm");
            cd = opendir(p);
            if (cd) {
                struct dirent *ce;
                size_t cl = strlen(cards[i]);
                while ((ce = readdir(cd)) != NULL) {
                    char st[32], mode[64], q[PATH_MAX];
                    if (strncmp(ce->d_name, cards[i], cl) || ce->d_name[cl] != '-') continue;
                    snprintf(q, sizeof(q), "/sys/class/drm/%s/status", ce->d_name);
                    read_file(q, st, sizeof(st));
                    if (strcmp(st, "connected")) continue;
                    snprintf(q, sizeof(q), "/sys/class/drm/%s/modes", ce->d_name);
                    mode[0] = 0;
                    {
                        FILE *mf = fopen(q, "r");
                        if (mf) { if (!fgets(mode, sizeof(mode), mf)) mode[0] = 0; fclose(mf); }
                    }
                    trim(mode);
                    printf("  Monitor: %s%s%s\n", ce->d_name + cl + 1, mode[0] ? " " : "", mode);
                }
                closedir(cd);
            }
        }
    }
    (void)idx;
}

static void show_audio(void)
{
    FILE *f = fopen("/proc/asound/cards", "r");
    char line[256];
    if (!f) return;
    while (fgets(line, sizeof(line), f)) {
        char *colon, *dash;
        if (!isdigit((unsigned char)line[0]) && line[0] != ' ') continue;
        if (!strchr(line, '[')) continue;
        colon = strchr(line, ':');
        if (!colon) continue;
        colon++;
        while (isspace((unsigned char)*colon)) colon++;
        dash = strstr(colon, " - ");
        {
            size_t l = strlen(colon);
            while (l && (colon[l - 1] == '\n')) colon[--l] = 0;
        }
        if (dash) {
            *dash = 0;
            printf("Audio: %s (%s)\n", dash + 3, colon);
        } else {
            printf("Audio: %s\n", colon);
        }
    }
    fclose(f);
}

static void usage(const char *p)
{
    fprintf(stderr, "usage: %s [-v] [-m] [-c processor|memory|disk|network|graphics|audio]\n", p);
    exit(2);
}

int main(int argc, char **argv)
{
    struct cpu_info cpu;
    struct cache_info cache;
    int verbose = 0, mem_only = 0, i;
    const char *klass = NULL;

    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-v")) verbose = 1;
        else if (!strcmp(argv[i], "-m")) mem_only = 1;
        else if (!strcmp(argv[i], "-c") && i + 1 < argc) klass = argv[++i];
        else usage(argv[0]);
    }
    read_cpu(&cpu);
    read_cache(&cache);

    if (mem_only) {
        printf("Main memory size: %ld Mbytes\n", mem_mbytes());
        return 0;
    }
    if (klass) {
        if (!strcmp(klass, "processor")) { show_processors(&cpu, verbose); return 0; }
        if (!strcmp(klass, "memory")) {
            show_cache(&cache);
            printf("Main memory size: %ld Mbytes\n", mem_mbytes());
            return 0;
        }
        if (!strcmp(klass, "disk") || !strcmp(klass, "scsi")) { show_storage(verbose); return 0; }
        if (!strcmp(klass, "network")) { show_network(verbose); return 0; }
        if (!strcmp(klass, "graphics")) { show_graphics(verbose); return 0; }
        if (!strcmp(klass, "audio")) { show_audio(); return 0; }
        fprintf(stderr, "%s: unsupported class \"%s\"\n", argv[0], klass);
        return 1;
    }
    show_processors(&cpu, verbose);
    show_cache(&cache);
    printf("Main memory size: %ld Mbytes\n", mem_mbytes());
    show_storage(verbose);
    show_network(verbose);
    show_graphics(verbose);
    show_audio();
    return 0;
}
