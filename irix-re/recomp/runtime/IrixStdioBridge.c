#include "IrixStdioBridge.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <unistd.h>

#define IRIX_STDIO_BASE 0x34000000u
#define IRIX_STDIO_ARENA_SIZE 0x00010000u
#define IRIX_STDIO_SLOT_SIZE 16u
#define IRIX_STDIO_MAX_FILES (IRIX_STDIO_ARENA_SIZE / IRIX_STDIO_SLOT_SIZE)
#define IRIX_MNTENT_BASE 0x34100000u
#define IRIX_MNTENT_ARENA_SIZE 0x00010000u
#define IRIX_MNTENT_STRUCT_SIZE 24u
#define IRIX_IOEOF 0x10u
#define IRIX_IOERR 0x20u

typedef struct IrixStdioSlot {
    FILE *host;
    unsigned used;
} IrixStdioSlot;

static IrixGuestRuntime *g_rt;
static IrixStdioSlot g_files[IRIX_STDIO_MAX_FILES];

static const char *guest_cstr(IrixGuestRuntime *rt, IrixGuestAddr addr)
{
    return irix_guest_cstr(rt, addr, 65536u);
}

static IrixGuestAddr slot_addr(unsigned i)
{
    return IRIX_STDIO_BASE + i * IRIX_STDIO_SLOT_SIZE;
}

static int slot_index(IrixGuestAddr addr)
{
    uint32_t off;
    unsigned i;
    if (addr < IRIX_STDIO_BASE || addr >= IRIX_STDIO_BASE + IRIX_STDIO_ARENA_SIZE)
        return -1;
    off = addr - IRIX_STDIO_BASE;
    if (off % IRIX_STDIO_SLOT_SIZE)
        return -1;
    i = (unsigned)(off / IRIX_STDIO_SLOT_SIZE);
    if (i >= IRIX_STDIO_MAX_FILES || !g_files[i].used)
        return -1;
    return (int)i;
}

static FILE *file_from_guest(IrixGuestRuntime *rt, IrixGuestAddr addr)
{
    int i = slot_index(addr);
    (void)rt;
    return i >= 0 ? g_files[i].host : 0;
}

static void sync_file_shadow(IrixGuestRuntime *rt, unsigned i)
{
    IrixGuestAddr addr;
    FILE *f;
    unsigned flag = 0;
    int fd = -1;

    if (i >= IRIX_STDIO_MAX_FILES || !g_files[i].used) return;
    f = g_files[i].host;
    addr = slot_addr(i);

    if (f) {
        if (feof(f)) flag |= IRIX_IOEOF;
        if (ferror(f)) flag |= IRIX_IOERR;
        fd = fileno(f);
    }

    /* Exact IRIX N32 FILE layout from SGI stdio.h:
       +0x00 int _cnt
       +0x04 unsigned char *_ptr
       +0x08 unsigned char *_base
       +0x0c unsigned char _flag
       +0x0d unsigned char _o_file
       +0x0e unsigned short _file

       Keep _cnt at zero and _ptr/_base null. SGI getc macros therefore take
       __filbuf on every byte instead of dereferencing a host buffer pointer. */
    irix_guest_write32be(rt, addr + 0u, 0u);
    irix_guest_write32be(rt, addr + 4u, 0u);
    irix_guest_write32be(rt, addr + 8u, 0u);
    irix_guest_write8(rt, addr + 12u, (uint8_t)flag);
    irix_guest_write8(rt, addr + 13u, 0u);
    irix_guest_write16be(rt, addr + 14u, (uint16_t)(fd >= 0 ? fd : 0xffff));
}

static void sync_file_by_addr(IrixGuestRuntime *rt, IrixGuestAddr addr)
{
    int i = slot_index(addr);
    if (i >= 0) sync_file_shadow(rt, (unsigned)i);
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
        sync_file_shadow(rt, i);
        return addr;
    }
    return 0;
}

static void free_file_handle(IrixGuestRuntime *rt, IrixGuestAddr addr)
{
    int i = slot_index(addr);
    void *p;
    if (i < 0) return;
    g_files[i].used = 0;
    g_files[i].host = 0;
    p = irix_guest_ptr(rt, addr, IRIX_STDIO_SLOT_SIZE);
    if (p) memset(p, 0, IRIX_STDIO_SLOT_SIZE);
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

static int get_one(IrixGuestRuntime *rt, IrixGuestAddr handle)
{
    FILE *f = file_from_guest(rt, handle);
    int c = f ? fgetc(f) : EOF;
    sync_file_by_addr(rt, handle);
    return c;
}

static void bridge___filbuf(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    cpu->gpr[2] = (uint64_t)(int64_t)(int32_t)get_one(rt, (uint32_t)cpu->gpr[4]);
}

static void bridge___semgetc(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    cpu->gpr[2] = (uint64_t)(int64_t)(int32_t)get_one(rt, (uint32_t)cpu->gpr[4]);
}

static void bridge_fgetc(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    bridge___filbuf(rt, cpu);
}

static void bridge_getc(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    bridge___filbuf(rt, cpu);
}

static void bridge_ungetc(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr handle = (uint32_t)cpu->gpr[5];
    FILE *f = file_from_guest(rt, handle);
    int r = f ? ungetc((int)(unsigned char)cpu->gpr[4], f) : EOF;
    sync_file_by_addr(rt, handle);
    cpu->gpr[2] = (uint64_t)(int64_t)(int32_t)r;
}

static void bridge_fputc(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr handle = (uint32_t)cpu->gpr[5];
    FILE *f = file_from_guest(rt, handle);
    int r = f ? fputc((int)(unsigned char)cpu->gpr[4], f) : EOF;
    sync_file_by_addr(rt, handle);
    cpu->gpr[2] = (uint64_t)(int64_t)(int32_t)r;
}

static void bridge_putc(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    bridge_fputc(rt, cpu);
}

static void bridge_fread(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr dst_addr = (uint32_t)cpu->gpr[4];
    uint32_t size = (uint32_t)cpu->gpr[5];
    uint32_t count = (uint32_t)cpu->gpr[6];
    IrixGuestAddr handle = (uint32_t)cpu->gpr[7];
    FILE *f = file_from_guest(rt, handle);
    uint64_t total = (uint64_t)size * count;
    void *dst = total <= SIZE_MAX ? irix_guest_ptr(rt, dst_addr, (size_t)total) : 0;
    size_t r = (f && (total == 0 || dst)) ? fread(dst, size, count, f) : 0;
    sync_file_by_addr(rt, handle);
    cpu->gpr[2] = (uint64_t)r;
}

static void bridge_fwrite(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr src_addr = (uint32_t)cpu->gpr[4];
    uint32_t size = (uint32_t)cpu->gpr[5];
    uint32_t count = (uint32_t)cpu->gpr[6];
    IrixGuestAddr handle = (uint32_t)cpu->gpr[7];
    FILE *f = file_from_guest(rt, handle);
    uint64_t total = (uint64_t)size * count;
    const void *src = total <= SIZE_MAX ? irix_guest_ptr(rt, src_addr, (size_t)total) : 0;
    size_t r = (f && (total == 0 || src)) ? fwrite(src, size, count, f) : 0;
    sync_file_by_addr(rt, handle);
    cpu->gpr[2] = (uint64_t)r;
}

static void bridge_fgets(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr dst_addr = (uint32_t)cpu->gpr[4];
    int size = (int)(int32_t)cpu->gpr[5];
    IrixGuestAddr handle = (uint32_t)cpu->gpr[6];
    FILE *f = file_from_guest(rt, handle);
    char *dst = size > 0 ? (char *)irix_guest_ptr(rt, dst_addr, (size_t)size) : 0;
    char *r = (f && dst) ? fgets(dst, size, f) : 0;
    sync_file_by_addr(rt, handle);
    cpu->gpr[2] = r ? dst_addr : 0;
}

static void bridge_fputs(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *s = guest_cstr(rt, (uint32_t)cpu->gpr[4]);
    IrixGuestAddr handle = (uint32_t)cpu->gpr[5];
    FILE *f = file_from_guest(rt, handle);
    int r = (f && s) ? fputs(s, f) : EOF;
    sync_file_by_addr(rt, handle);
    cpu->gpr[2] = (uint64_t)(int64_t)(int32_t)r;
}

static void bridge_fflush(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr handle = (uint32_t)cpu->gpr[4];
    FILE *f = file_from_guest(rt, handle);
    int r = f ? fflush(f) : fflush(0);
    if (f) sync_file_by_addr(rt, handle);
    cpu->gpr[2] = (uint64_t)(int64_t)(int32_t)r;
}

static void bridge_fseek(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr handle = (uint32_t)cpu->gpr[4];
    FILE *f = file_from_guest(rt, handle);
    long off = (long)(int32_t)cpu->gpr[5];
    int whence = (int)(int32_t)cpu->gpr[6];
    int r = f ? fseek(f, off, whence) : -1;
    sync_file_by_addr(rt, handle);
    cpu->gpr[2] = (uint64_t)(int64_t)(int32_t)r;
}

static void bridge_ftell(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr handle = (uint32_t)cpu->gpr[4];
    FILE *f = file_from_guest(rt, handle);
    long v = f ? ftell(f) : -1L;
    sync_file_by_addr(rt, handle);
    cpu->gpr[2] = (uint64_t)(int64_t)(int32_t)v;
}

static void bridge_rewind(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr handle = (uint32_t)cpu->gpr[4];
    FILE *f = file_from_guest(rt, handle);
    if (f) rewind(f);
    sync_file_by_addr(rt, handle);
    cpu->gpr[2] = 0;
}

static void bridge_feof(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr handle = (uint32_t)cpu->gpr[4];
    FILE *f = file_from_guest(rt, handle);
    int r = f ? feof(f) : 0;
    sync_file_by_addr(rt, handle);
    cpu->gpr[2] = (uint64_t)(uint32_t)r;
}

static void bridge_ferror(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr handle = (uint32_t)cpu->gpr[4];
    FILE *f = file_from_guest(rt, handle);
    int r = f ? ferror(f) : 0;
    sync_file_by_addr(rt, handle);
    cpu->gpr[2] = (uint64_t)(uint32_t)r;
}

static void bridge_clearerr(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr handle = (uint32_t)cpu->gpr[4];
    FILE *f = file_from_guest(rt, handle);
    if (f) clearerr(f);
    sync_file_by_addr(rt, handle);
    cpu->gpr[2] = 0;
}

static char *mnt_token(char **cursor)
{
    char *p = *cursor;
    char *start;

    while (*p && isspace((unsigned char)*p))
        ++p;
    start = p;
    while (*p && !isspace((unsigned char)*p))
        ++p;
    if (*p)
        *p++ = '\0';
    *cursor = p;
    return start;
}

static IrixGuestAddr copy_mnt_string(IrixGuestRuntime *rt, IrixGuestAddr *cur,
                                     const char *text)
{
    size_t n = strlen(text) + 1u;
    IrixGuestAddr out = *cur;
    void *dst;

    if ((uint64_t)out + n > (uint64_t)IRIX_MNTENT_BASE + IRIX_MNTENT_ARENA_SIZE)
        return 0;
    dst = irix_guest_ptr(rt, out, n);
    if (!dst)
        return 0;
    memcpy(dst, text, n);
    *cur += (IrixGuestAddr)n;
    return out;
}

static void bridge_setmntent(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *path = guest_cstr(rt, (uint32_t)cpu->gpr[4]);
    const char *mode = guest_cstr(rt, (uint32_t)cpu->gpr[5]);
    FILE *f;
    IrixGuestAddr handle;
    const char *p;

    cpu->gpr[2] = 0;
    if (!path || !mode)
        return;

    f = fopen(path, mode);
    if (!f)
        return;

    for (p = mode; *p; ++p) {
        if (*p == 'w' || *p == 'a' || *p == '+') {
            if (flock(fileno(f), LOCK_EX) < 0) {
                fclose(f);
                return;
            }
            break;
        }
    }

    handle = alloc_file_handle(rt, f);
    if (!handle) {
        fclose(f);
        return;
    }
    cpu->gpr[2] = handle;
}

static void bridge_getmntent(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr handle = (uint32_t)cpu->gpr[4];
    FILE *f = file_from_guest(rt, handle);
    char line[512];
    char *p;
    char *field[6];
    IrixGuestAddr cur = IRIX_MNTENT_BASE + IRIX_MNTENT_STRUCT_SIZE;
    IrixGuestAddr str_addr[4];
    int i;
    int freq = 0;
    int passno = 0;

    cpu->gpr[2] = 0;
    if (!f)
        return;

    for (;;) {
        if (!fgets(line, sizeof(line), f)) {
            sync_file_by_addr(rt, handle);
            return;
        }
        p = line;
        while (*p && isspace((unsigned char)*p))
            ++p;
        if (*p == '#' || *p == '\0')
            continue;
        break;
    }

    p = line;
    for (i = 0; i < 6; ++i)
        field[i] = mnt_token(&p);

    if (!field[0][0] || !field[1][0] || !field[2][0] || !field[3][0]) {
        sync_file_by_addr(rt, handle);
        return;
    }

    for (i = 0; i < 4; ++i) {
        str_addr[i] = copy_mnt_string(rt, &cur, field[i]);
        if (!str_addr[i]) {
            sync_file_by_addr(rt, handle);
            return;
        }
    }

    if (field[4][0])
        freq = atoi(field[4]);
    if (field[5][0])
        passno = atoi(field[5]);

    irix_guest_write32be(rt, IRIX_MNTENT_BASE + 0u, str_addr[0]);
    irix_guest_write32be(rt, IRIX_MNTENT_BASE + 4u, str_addr[1]);
    irix_guest_write32be(rt, IRIX_MNTENT_BASE + 8u, str_addr[2]);
    irix_guest_write32be(rt, IRIX_MNTENT_BASE + 12u, str_addr[3]);
    irix_guest_write32be(rt, IRIX_MNTENT_BASE + 16u, (uint32_t)freq);
    irix_guest_write32be(rt, IRIX_MNTENT_BASE + 20u, (uint32_t)passno);

    sync_file_by_addr(rt, handle);
    if (!rt->trapped)
        cpu->gpr[2] = IRIX_MNTENT_BASE;
}

static void bridge_hasmntopt(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr mnt_addr = (uint32_t)cpu->gpr[4];
    const char *opt = guest_cstr(rt, (uint32_t)cpu->gpr[5]);
    IrixGuestAddr opts_addr;
    const char *opts;
    const char *p;
    size_t optlen;

    cpu->gpr[2] = 0;
    if (!mnt_addr || !opt || !irix_guest_ptr(rt, mnt_addr, IRIX_MNTENT_STRUCT_SIZE))
        return;

    opts_addr = irix_guest_read32be(rt, mnt_addr + 12u);
    opts = guest_cstr(rt, opts_addr);
    if (!opts || rt->trapped)
        return;

    optlen = strlen(opt);
    p = opts;
    while (*p) {
        const char *start = p;
        size_t len;

        while (*p && *p != ',')
            ++p;
        len = (size_t)(p - start);

        if (len >= optlen && !strncmp(start, opt, optlen)) {
            cpu->gpr[2] = opts_addr + (IrixGuestAddr)(start - opts);
            return;
        }

        if (*p == ',')
            ++p;
    }
}

static void bridge_endmntent(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr handle = (uint32_t)cpu->gpr[4];
    FILE *f = file_from_guest(rt, handle);

    if (f) {
        fclose(f);
        free_file_handle(rt, handle);
    }
    cpu->gpr[2] = 1u;
}

static void bridge_fileno(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr handle = (uint32_t)cpu->gpr[4];
    FILE *f = file_from_guest(rt, handle);
    int fd = f ? fileno(f) : -1;
    sync_file_by_addr(rt, handle);
    cpu->gpr[2] = (uint64_t)(int64_t)(int32_t)fd;
}

int irix_stdio_bridge_init(IrixGuestRuntime *rt)
{
    memset(g_files, 0, sizeof(g_files));
    g_rt = rt;
    if (irix_guest_map(rt, IRIX_STDIO_BASE, IRIX_STDIO_ARENA_SIZE, 3u) != 0 &&
        !irix_guest_ptr(rt, IRIX_STDIO_BASE, 1))
        return -1;
    if (irix_guest_map(rt, IRIX_MNTENT_BASE, IRIX_MNTENT_ARENA_SIZE, 3u) != 0 &&
        !irix_guest_ptr(rt, IRIX_MNTENT_BASE, 1))
        return -1;
    return 0;
}

IrixGuestFunction irix_stdio_bridge_lookup(const char *name)
{
    (void)g_rt;
    if (!name) return 0;
#define M(n) if (!strcmp(name, #n)) return bridge_##n
    M(fopen); M(fclose); M(fread); M(fwrite); M(fgets); M(fputs);
    M(fgetc); M(getc); M(ungetc); M(fputc); M(putc); M(fflush);
    M(fseek); M(ftell); M(rewind); M(feof); M(ferror); M(clearerr); M(fileno);
    M(setmntent); M(getmntent); M(endmntent); M(hasmntopt);
    M(__filbuf); M(__semgetc);
#undef M
    return 0;
}
