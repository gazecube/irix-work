#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

static void usage(const char *p)
{
    fprintf(stderr, "usage: %s [-v] [-m] [-c class]\n", p);
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
        if (!strcmp(klass, "processor")) {
            show_processors(&cpu, verbose);
            return 0;
        }
        if (!strcmp(klass, "memory")) {
            show_cache(&cache);
            printf("Main memory size: %ld Mbytes\n", mem_mbytes());
            return 0;
        }
        fprintf(stderr, "%s: unsupported class \"%s\"\n", argv[0], klass);
        return 1;
    }
    show_processors(&cpu, verbose);
    show_cache(&cache);
    printf("Main memory size: %ld Mbytes\n", mem_mbytes());
    return 0;
}
