#include "IrixStdioBridge.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#define IRIX_STDIO_BASE 0x34000000u
#define IRIX_STDIO_ARENA_SIZE 0x00010000u
#define IRIX_STDIO_SLOT_SIZE 16u
#define IRIX_STDIO_MAX_FILES (IRIX_STDIO_ARENA_SIZE / IRIX_STDIO_SLOT_SIZE)
#define IRIX_STDIO_MAGIC 0x49524650u

typedef struct IrixStdioSlot {
    FILE *host;
    unsigned used;
} IrixStdioSlot;

static IrixGuestRuntime *g_rt;
static IrixStdioSlot g_files[IRIX_STDIO_MAX_FILES];

static const char *guest_cstr(IrixGuestRuntime *rt, IrixGuestAddr addr)
{
    return addr ? (const char *)irix_guest_ptr(rt, addr, 1) : 0;
}

static IrixGuestAddr slot_addr(unsigned i)
{
    return IRIX_STDIO_BASE + i * IRIX_STDIO_SLOT_SIZE;
}

static FILE *file_from_guest(IrixGuestRuntime *rt, IrixGuestAddr addr)
{
    unsigned i;
    if (addr < IRIX_STDIO_BASE || addr >= IRIX_STDIO_BASE + IRIX_STDIO_ARENA_SIZE)
        return 0;
    if ((addr - IRIX_STDIO_BASE) % IRIX_STDIO_SLOT_SIZE)
        return 0;
    i = (unsigned)((addr - IRIX_STDIO_BASE) / IRIX_STDIO_SLOT_SIZE);
    if (i >= IRIX_STDIO_MAX_FILES || !g_files[i].used)
        return 0;
    if (irix_guest_read32be(rt, addr) != IRIX_STDIO_MAGIC ||
        irix_guest_read32be(rt, addr + 4u) != i)
        return 0;
    return g_files[i].host;
}

static IrixGuestAddr alloc_file_handle(IrixGuestRuntime *rt, FILE *host)
{
    unsigned i;
    if (!host) return 0;
    for (i = 0; i < IRIX_STDIO_MAX_FILES; ++i) {
        IrixGuestAddr addr;
        if (g_files[i].used) continue;
        g_files[i].used = 1;
        g_files[i].host = host;
        addr = slot_addr(i);
        irix_guest_write32be(rt, addr + 0u, IRIX_STDIO_MAGIC);
        irix_guest_write32be(rt, addr + 4u, i);
        irix_guest_write32be(rt, addr + 8u, 0);
        irix_guest_write32be(rt, addr + 12u, 0);
        return addr;
    }
    return 0;
}

static void free_file_handle(IrixGuestRuntime *rt, IrixGuestAddr addr)
{
    unsigned i;
    if (addr < IRIX_STDIO_BASE || addr >= IRIX_STDIO_BASE + IRIX_STDIO_ARENA_SIZE)
        return;
    if ((addr - IRIX_STDIO_BASE) % IRIX_STDIO_SLOT_SIZE)
        return;
    i = (unsigned)((addr - IRIX_STDIO_BASE) / IRIX_STDIO_SLOT_SIZE);
    if (i >= IRIX_STDIO_MAX_FILES) return;
    g_files[i].used = 0;
    g_files[i].host = 0;
    irix_guest_write32be(rt, addr + 0u, 0);
    irix_guest_write32be(rt, addr + 4u, 0);
}

static void bridge_fopen(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *path = guest_cstr(rt, (uint32_t)cpu->gpr[4]);
    const char *mode = guest_cstr(rt, (uint32_t)cpu->gpr[5]);
    FILE *f = (path && mode) ? fopen(path, mode) : 0;
    IrixGuestAddr handle = alloc_file_handle(rt, f);
    if (f && !handle) fclose(f);
    cpu->gpr[2] = handle;
}

static void bridge_fclose(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr handle = (uint32_t)cpu->gpr[4];
    FILE *f = file_from_guest(rt, handle);
    int r = f ? fclose(f) : EOF;
    if (f) free_file_handle(rt, handle);
    cpu->gpr[2] = (uint64_t)(int64_t)(int32_t)r;
}

static void bridge_fread(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr dst_addr = (uint32_t)cpu->gpr[4];
    uint32_t size = (uint32_t)cpu->gpr[5];
    uint32_t count = (uint32_t)cpu->gpr[6];
    FILE *f = file_from_guest(rt, (uint32_t)cpu->gpr[7]);
    uint64_t total = (uint64_t)size * count;
    void *dst = total <= SIZE_MAX ? irix_guest_ptr(rt, dst_addr, (size_t)total) : 0;
    cpu->gpr[2] = (f && (total == 0 || dst)) ? (uint64_t)fread(dst, size, count, f) : 0;
}

static void bridge_fwrite(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr src_addr = (uint32_t)cpu->gpr[4];
    uint32_t size = (uint32_t)cpu->gpr[5];
    uint32_t count = (uint32_t)cpu->gpr[6];
    FILE *f = file_from_guest(rt, (uint32_t)cpu->gpr[7]);
    uint64_t total = (uint64_t)size * count;
    const void *src = total <= SIZE_MAX ? irix_guest_ptr(rt, src_addr, (size_t)total) : 0;
    cpu->gpr[2] = (f && (total == 0 || src)) ? (uint64_t)fwrite(src, size, count, f) : 0;
}

static void bridge_fgets(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr dst_addr = (uint32_t)cpu->gpr[4];
    int size = (int)(int32_t)cpu->gpr[5];
    FILE *f = file_from_guest(rt, (uint32_t)cpu->gpr[6]);
    char *dst = size > 0 ? (char *)irix_guest_ptr(rt, dst_addr, (size_t)size) : 0;
    cpu->gpr[2] = (f && dst && fgets(dst, size, f)) ? dst_addr : 0;
}

static void bridge_fseek(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    FILE *f = file_from_guest(rt, (uint32_t)cpu->gpr[4]);
    long off = (long)(int32_t)cpu->gpr[5];
    int whence = (int)(int32_t)cpu->gpr[6];
    cpu->gpr[2] = (uint64_t)(int64_t)(int32_t)(f ? fseek(f, off, whence) : -1);
}

static void bridge_ftell(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    FILE *f = file_from_guest(rt, (uint32_t)cpu->gpr[4]);
    long v = f ? ftell(f) : -1L;
    cpu->gpr[2] = (uint64_t)(int64_t)(int32_t)v;
}

static void bridge_rewind(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    FILE *f = file_from_guest(rt, (uint32_t)cpu->gpr[4]);
    if (f) rewind(f);
    cpu->gpr[2] = 0;
}

static void bridge_feof(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    FILE *f = file_from_guest(rt, (uint32_t)cpu->gpr[4]);
    cpu->gpr[2] = f ? (uint64_t)(uint32_t)feof(f) : 0;
}

static void bridge_ferror(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    FILE *f = file_from_guest(rt, (uint32_t)cpu->gpr[4]);
    cpu->gpr[2] = f ? (uint64_t)(uint32_t)ferror(f) : 0;
}

static void bridge_clearerr(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    FILE *f = file_from_guest(rt, (uint32_t)cpu->gpr[4]);
    if (f) clearerr(f);
    cpu->gpr[2] = 0;
}

int irix_stdio_bridge_init(IrixGuestRuntime *rt)
{
    memset(g_files, 0, sizeof(g_files));
    g_rt = rt;
    if (irix_guest_map(rt, IRIX_STDIO_BASE, IRIX_STDIO_ARENA_SIZE, 3u) != 0 &&
        !irix_guest_ptr(rt, IRIX_STDIO_BASE, 1))
        return -1;
    return 0;
}

IrixGuestFunction irix_stdio_bridge_lookup(const char *name)
{
    (void)g_rt;
    if (!name) return 0;
#define M(n) if (!strcmp(name, #n)) return bridge_##n
    M(fopen); M(fclose); M(fread); M(fwrite); M(fgets);
    M(fseek); M(ftell); M(rewind); M(feof); M(ferror); M(clearerr);
#undef M
    return 0;
}
