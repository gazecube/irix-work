#include "IrixLibcBridge.h"

#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#define IRIX_LIBC_MAX_ALLOCS 65536u
#define IRIX_ENV_CACHE_MAX 256u

typedef struct IrixGuestAllocation {
    IrixGuestAddr addr;
    uint32_t size;
} IrixGuestAllocation;

typedef struct IrixEnvCacheEntry {
    char *name;
    char *value;
    IrixGuestAddr guest_value;
} IrixEnvCacheEntry;

typedef struct IrixLibcBridgeState {
    IrixGuestRuntime *rt;
    IrixGuestAddr heap_base;
    IrixGuestAddr heap_cur;
    IrixGuestAddr heap_end;
    IrixGuestAllocation allocs[IRIX_LIBC_MAX_ALLOCS];
    size_t alloc_count;
    IrixEnvCacheEntry env_cache[IRIX_ENV_CACHE_MAX];
    size_t env_cache_count;
} IrixLibcBridgeState;

static IrixLibcBridgeState g_libc;

static IrixGuestAddr align_addr(IrixGuestAddr value, uint32_t align)
{
    uint32_t mask;
    if (align < 1u) align = 1u;
    if (align & (align - 1u)) return 0;
    mask = align - 1u;
    return (value + mask) & ~mask;
}

static IrixGuestAllocation *find_alloc(IrixGuestAddr addr)
{
    size_t i;
    for (i = 0; i < g_libc.alloc_count; ++i)
        if (g_libc.allocs[i].addr == addr)
            return &g_libc.allocs[i];
    return 0;
}

static IrixGuestAddr alloc_guest(uint32_t size, uint32_t align)
{
    IrixGuestAddr addr;
    uint32_t n = size ? size : 1u;
    addr = align_addr(g_libc.heap_cur, align);
    if (!addr || addr < g_libc.heap_cur || addr + n < addr || addr + n > g_libc.heap_end)
        return 0;
    if (g_libc.alloc_count >= IRIX_LIBC_MAX_ALLOCS)
        return 0;
    g_libc.heap_cur = align_addr(addr + n, 16u);
    g_libc.allocs[g_libc.alloc_count].addr = addr;
    g_libc.allocs[g_libc.alloc_count].size = n;
    ++g_libc.alloc_count;
    return addr;
}

static const char *guest_cstr(IrixGuestRuntime *rt, IrixGuestAddr addr)
{
    return addr ? (const char *)irix_guest_ptr(rt, addr, 1) : 0;
}

static IrixGuestAddr copy_host_string_to_guest(IrixGuestRuntime *rt, const char *s)
{
    IrixGuestAddr out;
    size_t n;
    void *dst;
    if (!s) return 0;
    n = strlen(s) + 1u;
    if (n > UINT32_MAX) return 0;
    out = alloc_guest((uint32_t)n, 1u);
    if (!out) return 0;
    dst = irix_guest_ptr(rt, out, n);
    if (!dst) return 0;
    memcpy(dst, s, n);
    return out;
}

static int guest_compare(IrixGuestRuntime *rt, const IrixGuestCpu *caller,
                         IrixGuestAddr fn, IrixGuestAddr a, IrixGuestAddr b)
{
    IrixGuestCpu cb = *caller;
    IrixGuestAddr saved_pc = rt->current_pc;
    cb.gpr[4] = (uint64_t)(int64_t)(int32_t)a;
    cb.gpr[5] = (uint64_t)(int64_t)(int32_t)b;
    if (irix_guest_call(rt, &cb, fn) != 0) {
        rt->current_pc = saved_pc;
        return 0;
    }
    rt->current_pc = saved_pc;
    return (int)(int32_t)cb.gpr[2];
}

static void bridge_malloc(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)rt;
    cpu->gpr[2] = alloc_guest((uint32_t)cpu->gpr[4], 16u);
}

static void bridge_calloc(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    uint64_t count = (uint32_t)cpu->gpr[4];
    uint64_t size = (uint32_t)cpu->gpr[5];
    uint64_t total = count * size;
    IrixGuestAddr addr = total <= UINT32_MAX ? alloc_guest((uint32_t)total, 16u) : 0;
    if (addr) {
        void *p = irix_guest_ptr(rt, addr, (size_t)total);
        if (p) memset(p, 0, (size_t)total);
    }
    cpu->gpr[2] = addr;
}

static void bridge_realloc(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr old_addr = (uint32_t)cpu->gpr[4];
    uint32_t new_size = (uint32_t)cpu->gpr[5];
    IrixGuestAllocation *old = find_alloc(old_addr);
    IrixGuestAddr addr;
    uint32_t copy_size;
    void *dst;
    void *src;

    if (!old_addr) {
        cpu->gpr[2] = alloc_guest(new_size, 16u);
        return;
    }
    if (!new_size) {
        cpu->gpr[2] = 0;
        return;
    }
    addr = alloc_guest(new_size, 16u);
    if (!addr) { cpu->gpr[2] = 0; return; }
    copy_size = old && old->size < new_size ? old->size : new_size;
    dst = irix_guest_ptr(rt, addr, copy_size);
    src = irix_guest_ptr(rt, old_addr, copy_size);
    if (dst && src) memcpy(dst, src, copy_size);
    cpu->gpr[2] = addr;
}

static void bridge_memalign(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)rt;
    cpu->gpr[2] = alloc_guest((uint32_t)cpu->gpr[5], (uint32_t)cpu->gpr[4]);
}

static void bridge_free(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)rt;
    (void)cpu;
}

static void bridge_strdup(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    cpu->gpr[2] = copy_host_string_to_guest(rt, guest_cstr(rt, (uint32_t)cpu->gpr[4]));
}

static void bridge_strlen(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *s = guest_cstr(rt, (uint32_t)cpu->gpr[4]);
    cpu->gpr[2] = s ? (uint64_t)strlen(s) : 0;
}

static void bridge_strcmp(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *a = guest_cstr(rt, (uint32_t)cpu->gpr[4]);
    const char *b = guest_cstr(rt, (uint32_t)cpu->gpr[5]);
    cpu->gpr[2] = (uint64_t)(int64_t)(int32_t)((a && b) ? strcmp(a, b) : (a ? 1 : (b ? -1 : 0)));
}

static void bridge_strncmp(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *a = guest_cstr(rt, (uint32_t)cpu->gpr[4]);
    const char *b = guest_cstr(rt, (uint32_t)cpu->gpr[5]);
    uint32_t n = (uint32_t)cpu->gpr[6];
    cpu->gpr[2] = (uint64_t)(int64_t)(int32_t)((a && b) ? strncmp(a, b, n) : (a ? 1 : (b ? -1 : 0)));
}

static void bridge_strcasecmp(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *a = guest_cstr(rt, (uint32_t)cpu->gpr[4]);
    const char *b = guest_cstr(rt, (uint32_t)cpu->gpr[5]);
    cpu->gpr[2] = (uint64_t)(int64_t)(int32_t)((a && b) ? strcasecmp(a, b) : (a ? 1 : (b ? -1 : 0)));
}

static void bridge_strncasecmp(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *a = guest_cstr(rt, (uint32_t)cpu->gpr[4]);
    const char *b = guest_cstr(rt, (uint32_t)cpu->gpr[5]);
    uint32_t n = (uint32_t)cpu->gpr[6];
    cpu->gpr[2] = (uint64_t)(int64_t)(int32_t)((a && b) ? strncasecmp(a, b, n) : (a ? 1 : (b ? -1 : 0)));
}

static void bridge_strcpy(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr dst_addr = (uint32_t)cpu->gpr[4];
    const char *src = guest_cstr(rt, (uint32_t)cpu->gpr[5]);
    if (src) {
        size_t n = strlen(src) + 1u;
        void *dst = irix_guest_ptr(rt, dst_addr, n);
        if (dst) memcpy(dst, src, n);
    }
    cpu->gpr[2] = dst_addr;
}

static void bridge_strncpy(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr dst_addr = (uint32_t)cpu->gpr[4];
    const char *src = guest_cstr(rt, (uint32_t)cpu->gpr[5]);
    uint32_t n = (uint32_t)cpu->gpr[6];
    char *dst = (char *)irix_guest_ptr(rt, dst_addr, n);
    if (dst && src) strncpy(dst, src, n);
    cpu->gpr[2] = dst_addr;
}

static void bridge_strcat(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr dst_addr = (uint32_t)cpu->gpr[4];
    char *dst = (char *)irix_guest_ptr(rt, dst_addr, 1);
    const char *src = guest_cstr(rt, (uint32_t)cpu->gpr[5]);
    if (dst && src) strcat(dst, src);
    cpu->gpr[2] = dst_addr;
}

static void bridge_strncat(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr dst_addr = (uint32_t)cpu->gpr[4];
    char *dst = (char *)irix_guest_ptr(rt, dst_addr, 1);
    const char *src = guest_cstr(rt, (uint32_t)cpu->gpr[5]);
    uint32_t n = (uint32_t)cpu->gpr[6];
    if (dst && src) strncat(dst, src, n);
    cpu->gpr[2] = dst_addr;
}

static void bridge_strchr(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr addr = (uint32_t)cpu->gpr[4];
    const char *s = guest_cstr(rt, addr);
    const char *p = s ? strchr(s, (int)(uint32_t)cpu->gpr[5]) : 0;
    cpu->gpr[2] = p ? addr + (IrixGuestAddr)(p - s) : 0;
}

static void bridge_strrchr(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr addr = (uint32_t)cpu->gpr[4];
    const char *s = guest_cstr(rt, addr);
    const char *p = s ? strrchr(s, (int)(uint32_t)cpu->gpr[5]) : 0;
    cpu->gpr[2] = p ? addr + (IrixGuestAddr)(p - s) : 0;
}

static void bridge_strstr(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr addr = (uint32_t)cpu->gpr[4];
    const char *s = guest_cstr(rt, addr);
    const char *needle = guest_cstr(rt, (uint32_t)cpu->gpr[5]);
    const char *p = (s && needle) ? strstr(s, needle) : 0;
    cpu->gpr[2] = p ? addr + (IrixGuestAddr)(p - s) : 0;
}

static void bridge_memcpy(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr dst_addr = (uint32_t)cpu->gpr[4];
    IrixGuestAddr src_addr = (uint32_t)cpu->gpr[5];
    uint32_t n = (uint32_t)cpu->gpr[6];
    void *dst = irix_guest_ptr(rt, dst_addr, n);
    const void *src = irix_guest_ptr(rt, src_addr, n);
    if (dst && src && n) memcpy(dst, src, n);
    cpu->gpr[2] = dst_addr;
}

static void bridge_bcopy(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr src_addr = (uint32_t)cpu->gpr[4];
    IrixGuestAddr dst_addr = (uint32_t)cpu->gpr[5];
    uint32_t n = (uint32_t)cpu->gpr[6];
    void *dst = irix_guest_ptr(rt, dst_addr, n);
    const void *src = irix_guest_ptr(rt, src_addr, n);
    if (dst && src && n) memmove(dst, src, n);
    cpu->gpr[2] = 0;
}

static void bridge_memmove(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr dst_addr = (uint32_t)cpu->gpr[4];
    IrixGuestAddr src_addr = (uint32_t)cpu->gpr[5];
    uint32_t n = (uint32_t)cpu->gpr[6];
    void *dst = irix_guest_ptr(rt, dst_addr, n);
    const void *src = irix_guest_ptr(rt, src_addr, n);
    if (dst && src && n) memmove(dst, src, n);
    cpu->gpr[2] = dst_addr;
}

static void bridge_memset(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr dst_addr = (uint32_t)cpu->gpr[4];
    int c = (int)(uint32_t)cpu->gpr[5];
    uint32_t n = (uint32_t)cpu->gpr[6];
    void *dst = irix_guest_ptr(rt, dst_addr, n);
    if (dst && n) memset(dst, c, n);
    cpu->gpr[2] = dst_addr;
}

static void bridge_memcmp(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    uint32_t n = (uint32_t)cpu->gpr[6];
    const void *a = irix_guest_ptr(rt, (uint32_t)cpu->gpr[4], n);
    const void *b = irix_guest_ptr(rt, (uint32_t)cpu->gpr[5], n);
    int r = (a && b) ? memcmp(a, b, n) : (a ? 1 : (b ? -1 : 0));
    cpu->gpr[2] = (uint64_t)(int64_t)(int32_t)r;
}

static void bridge_memchr(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr addr = (uint32_t)cpu->gpr[4];
    uint32_t n = (uint32_t)cpu->gpr[6];
    const unsigned char *s = (const unsigned char *)irix_guest_ptr(rt, addr, n);
    const unsigned char *p = s ? (const unsigned char *)memchr(s, (int)(uint32_t)cpu->gpr[5], n) : 0;
    cpu->gpr[2] = p ? addr + (IrixGuestAddr)(p - s) : 0;
}

static void bridge_qsort(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr base = (uint32_t)cpu->gpr[4];
    uint32_t nmemb = (uint32_t)cpu->gpr[5];
    uint32_t size = (uint32_t)cpu->gpr[6];
    IrixGuestAddr compar = (uint32_t)cpu->gpr[7];
    uint64_t total = (uint64_t)nmemb * (uint64_t)size;
    unsigned char *data;
    unsigned char *tmp;
    uint32_t i;

    cpu->gpr[2] = 0;
    if (nmemb < 2u || size == 0u) return;
    if (!compar || total > UINT32_MAX) { irix_guest_trap(rt, cpu, 0xeeee2001u); return; }
    data = (unsigned char *)irix_guest_ptr(rt, base, (size_t)total);
    if (!data) { irix_guest_trap(rt, cpu, 0xeeee2002u); return; }
    tmp = (unsigned char *)malloc(size);
    if (!tmp) { irix_guest_trap(rt, cpu, 0xeeee2003u); return; }

    for (i = 1; i < nmemb && !rt->trapped; ++i) {
        uint32_t j = i;
        memcpy(tmp, data + (size_t)i * size, size);
        while (j > 0u) {
            IrixGuestAddr prev = base + (j - 1u) * size;
            IrixGuestAddr cur = base + j * size;
            memcpy(data + (size_t)j * size, tmp, size);
            if (guest_compare(rt, cpu, compar, prev, cur) <= 0 || rt->trapped)
                break;
            memmove(data + (size_t)j * size,
                    data + (size_t)(j - 1u) * size, size);
            --j;
        }
        if (!rt->trapped)
            memcpy(data + (size_t)j * size, tmp, size);
    }
    free(tmp);
}

static void bridge_bsearch(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr key = (uint32_t)cpu->gpr[4];
    IrixGuestAddr base = (uint32_t)cpu->gpr[5];
    uint32_t nmemb = (uint32_t)cpu->gpr[6];
    uint32_t size = (uint32_t)cpu->gpr[7];
    IrixGuestAddr compar = (uint32_t)cpu->gpr[8];
    uint32_t lo = 0, hi = nmemb;

    cpu->gpr[2] = 0;
    if (!size || !compar || ((uint64_t)nmemb * size) > UINT32_MAX) return;
    if (nmemb && !irix_guest_ptr(rt, base, (size_t)nmemb * size)) return;
    while (lo < hi && !rt->trapped) {
        uint32_t mid = lo + (hi - lo) / 2u;
        IrixGuestAddr elem = base + mid * size;
        int r = guest_compare(rt, cpu, compar, key, elem);
        if (rt->trapped) return;
        if (r < 0) hi = mid;
        else if (r > 0) lo = mid + 1u;
        else { cpu->gpr[2] = elem; return; }
    }
}

static void bridge_getenv(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *name = guest_cstr(rt, (uint32_t)cpu->gpr[4]);
    const char *value;
    size_t i;
    if (!name) { cpu->gpr[2] = 0; return; }
    value = getenv(name);
    if (!value) { cpu->gpr[2] = 0; return; }
    for (i = 0; i < g_libc.env_cache_count; ++i) {
        if (strcmp(g_libc.env_cache[i].name, name) == 0 && strcmp(g_libc.env_cache[i].value, value) == 0) {
            cpu->gpr[2] = g_libc.env_cache[i].guest_value;
            return;
        }
    }
    if (g_libc.env_cache_count < IRIX_ENV_CACHE_MAX) {
        IrixEnvCacheEntry *e = &g_libc.env_cache[g_libc.env_cache_count];
        IrixGuestAddr guest_value = copy_host_string_to_guest(rt, value);
        if (guest_value) {
            e->name = strdup(name);
            e->value = strdup(value);
            e->guest_value = guest_value;
            if (e->name && e->value) {
                ++g_libc.env_cache_count;
                cpu->gpr[2] = guest_value;
                return;
            }
            free(e->name);
            free(e->value);
            e->name = 0;
            e->value = 0;
        }
    }
    cpu->gpr[2] = copy_host_string_to_guest(rt, value);
}

static void bridge_getpid(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)rt;
    cpu->gpr[2] = (uint64_t)(uint32_t)getpid();
}

static void bridge_getppid(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)rt;
    cpu->gpr[2] = (uint64_t)(uint32_t)getppid();
}

static void bridge_getuid(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)rt;
    cpu->gpr[2] = (uint64_t)(uint32_t)getuid();
}

static void bridge_geteuid(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)rt;
    cpu->gpr[2] = (uint64_t)(uint32_t)geteuid();
}

static void bridge_getgid(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)rt;
    cpu->gpr[2] = (uint64_t)(uint32_t)getgid();
}

static void bridge_getegid(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)rt;
    cpu->gpr[2] = (uint64_t)(uint32_t)getegid();
}

static void bridge_setlocale(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    int category = (int)(int32_t)cpu->gpr[4];
    IrixGuestAddr guest_name = (uint32_t)cpu->gpr[5];
    const char *name = guest_name ? (const char *)irix_guest_ptr(rt, guest_name, 1) : 0;
    const char *result = setlocale(category, name);
    cpu->gpr[2] = copy_host_string_to_guest(rt, result);
}

int irix_libc_bridge_init(IrixGuestRuntime *rt, IrixGuestAddr heap_base, uint32_t heap_size)
{
    memset(&g_libc, 0, sizeof(g_libc));
    if (!heap_size || heap_base + heap_size < heap_base)
        return -1;
    if (irix_guest_map(rt, heap_base, heap_size, 3u) != 0 && !irix_guest_ptr(rt, heap_base, 1))
        return -1;
    g_libc.rt = rt;
    g_libc.heap_base = heap_base;
    g_libc.heap_cur = heap_base;
    g_libc.heap_end = heap_base + heap_size;
    return 0;
}

IrixGuestFunction irix_libc_bridge_lookup(const char *name)
{
    if (!name) return 0;
    if (!strcmp(name, "malloc") || !strcmp(name, "__nw__GUi") || !strcmp(name, "__nwa__GUi")) return bridge_malloc;
    if (!strcmp(name, "calloc")) return bridge_calloc;
    if (!strcmp(name, "realloc")) return bridge_realloc;
    if (!strcmp(name, "memalign")) return bridge_memalign;
    if (!strcmp(name, "free") || !strcmp(name, "__dl__GPv") || !strcmp(name, "__dla__GPv")) return bridge_free;
    if (!strcmp(name, "strdup")) return bridge_strdup;
    if (!strcmp(name, "strlen")) return bridge_strlen;
    if (!strcmp(name, "strcmp")) return bridge_strcmp;
    if (!strcmp(name, "strncmp")) return bridge_strncmp;
    if (!strcmp(name, "strcasecmp")) return bridge_strcasecmp;
    if (!strcmp(name, "strncasecmp")) return bridge_strncasecmp;
    if (!strcmp(name, "strcpy")) return bridge_strcpy;
    if (!strcmp(name, "strncpy")) return bridge_strncpy;
    if (!strcmp(name, "strcat")) return bridge_strcat;
    if (!strcmp(name, "strncat")) return bridge_strncat;
    if (!strcmp(name, "strchr") || !strcmp(name, "index")) return bridge_strchr;
    if (!strcmp(name, "strrchr") || !strcmp(name, "rindex")) return bridge_strrchr;
    if (!strcmp(name, "strstr")) return bridge_strstr;
    if (!strcmp(name, "memcpy")) return bridge_memcpy;
    if (!strcmp(name, "bcopy")) return bridge_bcopy;
    if (!strcmp(name, "memmove")) return bridge_memmove;
    if (!strcmp(name, "memset")) return bridge_memset;
    if (!strcmp(name, "memcmp") || !strcmp(name, "bcmp")) return bridge_memcmp;
    if (!strcmp(name, "memchr")) return bridge_memchr;
    if (!strcmp(name, "qsort")) return bridge_qsort;
    if (!strcmp(name, "bsearch")) return bridge_bsearch;
    if (!strcmp(name, "getenv")) return bridge_getenv;
    if (!strcmp(name, "getpid")) return bridge_getpid;
    if (!strcmp(name, "getppid")) return bridge_getppid;
    if (!strcmp(name, "getuid")) return bridge_getuid;
    if (!strcmp(name, "geteuid")) return bridge_geteuid;
    if (!strcmp(name, "getgid")) return bridge_getgid;
    if (!strcmp(name, "getegid")) return bridge_getegid;
    if (!strcmp(name, "setlocale")) return bridge_setlocale;
    return 0;
}
