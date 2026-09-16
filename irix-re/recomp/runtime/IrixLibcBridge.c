#include "IrixLibcBridge.h"

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
    return 0;
}
