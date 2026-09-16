#include "IrixLibcBridge.h"

#include <locale.h>
#include <stdlib.h>
#include <string.h>

#define IRIX_LIBC_MAX_ALLOCS 65536u

typedef struct IrixGuestAllocation {
    IrixGuestAddr addr;
    uint32_t size;
} IrixGuestAllocation;

typedef struct IrixLibcBridgeState {
    IrixGuestRuntime *rt;
    IrixGuestAddr heap_base;
    IrixGuestAddr heap_cur;
    IrixGuestAddr heap_end;
    IrixGuestAllocation allocs[IRIX_LIBC_MAX_ALLOCS];
    size_t alloc_count;
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
    const char *src = guest_cstr(rt, (uint32_t)cpu->gpr[4]);
    IrixGuestAddr out = 0;
    if (src) {
        size_t n = strlen(src) + 1u;
        if (n <= UINT32_MAX) {
            out = alloc_guest((uint32_t)n, 1u);
            if (out) {
                void *dst = irix_guest_ptr(rt, out, n);
                if (dst) memcpy(dst, src, n);
                else out = 0;
            }
        }
    }
    cpu->gpr[2] = out;
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

static void bridge_setlocale(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    int category = (int)(int32_t)cpu->gpr[4];
    IrixGuestAddr guest_name = (uint32_t)cpu->gpr[5];
    const char *name = guest_name ? (const char *)irix_guest_ptr(rt, guest_name, 1) : 0;
    const char *result = setlocale(category, name);
    IrixGuestAddr out = 0;
    if (result) {
        size_t n = strlen(result) + 1u;
        if (n <= UINT32_MAX) {
            out = alloc_guest((uint32_t)n, 1u);
            if (out) {
                void *p = irix_guest_ptr(rt, out, n);
                if (p) memcpy(p, result, n);
                else out = 0;
            }
        }
    }
    cpu->gpr[2] = out;
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
    if (!strcmp(name, "strcpy")) return bridge_strcpy;
    if (!strcmp(name, "strncpy")) return bridge_strncpy;
    if (!strcmp(name, "strcat")) return bridge_strcat;
    if (!strcmp(name, "strncat")) return bridge_strncat;
    if (!strcmp(name, "strchr") || !strcmp(name, "index")) return bridge_strchr;
    if (!strcmp(name, "strrchr") || !strcmp(name, "rindex")) return bridge_strrchr;
    if (!strcmp(name, "strstr")) return bridge_strstr;
    if (!strcmp(name, "memcpy") || !strcmp(name, "bcopy")) return bridge_memcpy;
    if (!strcmp(name, "memmove")) return bridge_memmove;
    if (!strcmp(name, "memset")) return bridge_memset;
    if (!strcmp(name, "memcmp") || !strcmp(name, "bcmp")) return bridge_memcmp;
    if (!strcmp(name, "memchr")) return bridge_memchr;
    if (!strcmp(name, "setlocale")) return bridge_setlocale;
    return 0;
}
