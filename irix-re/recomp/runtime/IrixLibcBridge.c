#define _GNU_SOURCE
#include "IrixLibcBridge.h"
#include <sys/stat.h>

#include <locale.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#define IRIX_LIBC_MAX_ALLOCS 65536u
#define IRIX_ENV_CACHE_MAX 256u

typedef struct IrixGuestAllocation {
    IrixGuestAddr addr;
    uint32_t size;
    uint32_t capacity;
    int in_use;
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
    IrixGuestAddr errno_addr;
    IrixGuestAddr optarg_addr;
    IrixGuestAddr opterr_addr;
    IrixGuestAddr optind_addr;
    IrixGuestAddr optopt_addr;
    IrixGuestAddr getopt_sp_addr;
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

static IrixGuestAddr alloc_guest(uint32_t size, uint32_t align);

static IrixGuestAllocation *find_alloc(IrixGuestAddr addr)
{
    size_t i;
    for (i = 0; i < g_libc.alloc_count; ++i)
        if (g_libc.allocs[i].in_use &&
            g_libc.allocs[i].addr == addr)
            return &g_libc.allocs[i];
    return 0;
}

uint32_t irix_libc_guest_allocation_size(IrixGuestAddr addr)
{
    IrixGuestAllocation *a = find_alloc(addr);
    return a ? a->size : 0u;
}

IrixGuestAddr irix_libc_guest_malloc(uint32_t size)
{
    return alloc_guest(size, 16u);
}

static IrixGuestAddr alloc_guest(uint32_t size, uint32_t align)
{
    IrixGuestAddr addr;
    uint32_t n = size ? size : 1u;
    size_t i;

    if (align < 1u)
        align = 1u;
    if (align & (align - 1u))
        return 0;

    /*
     * Reuse released blocks before extending the bump frontier.
     *
     * The original bring-up allocator intentionally made free() a no-op.
     * Xt/Motif performs enough short-lived allocation during resource and
     * translation initialization to exhaust either the 64 MB guest heap or
     * the 65536 allocation records.  Preserve stable guest addresses while
     * giving ordinary malloc/free semantics back to the application.
     *
     * We reuse an entire old block rather than split it.  That keeps this
     * allocator simple and deterministic while still eliminating the
     * pathological monotonic leak.
     */
    for (i = 0; i < g_libc.alloc_count; ++i) {
        IrixGuestAllocation *a = &g_libc.allocs[i];
        if (!a->in_use && a->capacity >= n &&
            (a->addr & (align - 1u)) == 0u) {
            a->size = n;
            a->in_use = 1;
            return a->addr;
        }
    }

    addr = align_addr(g_libc.heap_cur, align);
    if (!addr || addr < g_libc.heap_cur ||
        addr + n < addr || addr + n > g_libc.heap_end) {
        fprintf(stderr,
                "IRIX guest malloc exhausted: request=%u align=%u "
                "heap_cur=%08x heap_end=%08x allocs=%zu\n",
                n, align, g_libc.heap_cur, g_libc.heap_end,
                g_libc.alloc_count);
        return 0;
    }
    if (g_libc.alloc_count >= IRIX_LIBC_MAX_ALLOCS) {
        fprintf(stderr,
                "IRIX guest malloc allocation-table exhausted: "
                "request=%u align=%u heap_cur=%08x allocs=%zu max=%u\n",
                n, align, g_libc.heap_cur, g_libc.alloc_count,
                IRIX_LIBC_MAX_ALLOCS);
        return 0;
    }

    g_libc.heap_cur = align_addr(addr + n, 16u);
    g_libc.allocs[g_libc.alloc_count].addr = addr;
    g_libc.allocs[g_libc.alloc_count].size = n;
    g_libc.allocs[g_libc.alloc_count].capacity = n;
    g_libc.allocs[g_libc.alloc_count].in_use = 1;
    ++g_libc.alloc_count;
    return addr;
}

static const char *guest_cstr(IrixGuestRuntime *rt, IrixGuestAddr addr)
{
    return irix_guest_cstr(rt, addr, 65536u);
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
    /*
     * Host qsort/bsearch is standing in for an N32 indirect callback call.
     * A real SGI caller would place the function pointer in $t9 before
     * jalr $t9 so a PIC callback can establish its own $gp.
     */
    cb.gpr[25] = (uint64_t)(int64_t)(int32_t)fn;
    if (irix_guest_call(rt, &cb, fn) != 0) {
        rt->current_pc = saved_pc;
        return 0;
    }
    rt->current_pc = saved_pc;
    return (int)(int32_t)cb.gpr[2];
}

static void bridge_malloc(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    uint32_t size = (uint32_t)cpu->gpr[4];

    /*
     * SGI Motif _XmBuildResources ABI repair.
     *
     * The translated routine at 0x0b2a95xx reaches the original XtMalloc
     * wrapper with s2=wc_num_resources but s1 polluted with sc_resources
     * itself. XtMalloc then calls this native malloc bridge:
     *
     *   ra             = 0x0f6144b8
     *   saved Xt caller= 0x0b2a9590 at sp+0x10
     *
     * The original code computes both its malloc and following memcpy sizes
     * from s1, so repair the register here as well as the immediate malloc
     * request. The superclass synthetic-resource array is a guest allocation;
     * each IRIX N32 XmSyntheticResource is 20 bytes.
     */
    if (size > 0x04000000u &&
        (uint32_t)cpu->gpr[31] == 0x0f6144b8u) {
        IrixGuestAddr sp = (IrixGuestAddr)(uint32_t)cpu->gpr[29];
        IrixGuestAddr xt_caller_ra = 0u;

        if (sp && irix_guest_ptr(rt, sp + 0x10u, 8u))
            xt_caller_ra = (IrixGuestAddr)(uint32_t)
                irix_guest_read64be(rt, sp + 0x10u);

        if (xt_caller_ra == 0x0b2a9590u) {
            IrixGuestAddr sc_resources =
                (IrixGuestAddr)(uint32_t)cpu->gpr[17];
            uint32_t wc_num = (uint32_t)cpu->gpr[18];
            IrixGuestAllocation *a = find_alloc(sc_resources);

            if (a && a->size >= 20u && (a->size % 20u) == 0u &&
                wc_num < 4096u) {
                uint32_t sc_num = a->size / 20u;
                uint64_t total = (uint64_t)(wc_num + sc_num) * 20u;

                if (sc_num < 4096u && total <= 0x00100000u) {
                    (void)0;
                    cpu->gpr[17] =
                        (uint64_t)(int64_t)(int32_t)sc_num;
                    cpu->gpr[4] =
                        (uint64_t)(int64_t)(int32_t)(uint32_t)total;
                    size = (uint32_t)total;
                }
            } else {
                fprintf(stderr,
                        "IRIX XmBuildResources malloc repair unresolved: "
                        "sc_resources=%08x alloc_size=%u wc_num=%u\n",
                        sc_resources, a ? a->size : 0u, wc_num);
            }
        }
    }

    /*
     * A normal desktop startup allocation should never be remotely close to
     * the guest address-space limit.  When an absurd request appears, dump
     * the architectural callsite before returning NULL so we can distinguish
     * a corrupt size from a bad import/GOT binding.
     */
    if (size > 0x04000000u) {
        IrixGuestAddr ra = (IrixGuestAddr)(uint32_t)cpu->gpr[31];
        IrixGuestAddr callsite = ra >= 8u ? ra - 8u : 0u;
        unsigned i;

        {
            IrixGuestAddr sp = (IrixGuestAddr)(uint32_t)cpu->gpr[29];
            IrixGuestAddr outer_ra = 0u;
            IrixGuestAddr outer_callsite = 0u;
            IrixGuestAddr imd_caller_ra = 0u;
            IrixGuestAddr imd_caller2_ra = 0u;
            IrixGuestFunctionEntry *imd_caller = NULL;
            IrixGuestFunctionEntry *imd_caller2 = NULL;

            /*
             * SGI XtMalloc at 0x0f614498 saves its caller RA with
             *     sd $ra,0x10($sp)
             * before jalr'ing libc malloc at 0x0f6144b0.  Recover that
             * saved RA here so an absurd allocation report names the code
             * that CALCULATED the bad size, not merely the XtMalloc wrapper.
             */
            if (sp && irix_guest_ptr(rt, sp + 0x10u, 8u)) {
                uint64_t saved_ra64 = irix_guest_read64be(rt, sp + 0x10u);
                outer_ra = (IrixGuestAddr)(uint32_t)saved_ra64;
                if (outer_ra >= 8u)
                    outer_callsite = outer_ra - 8u;
            }

            /* ImdString::_allocString (0x09838194) has a 0x40-byte
             * frame and saves its caller RA at sp+0x20.  If it is the
             * producer of an impossible request, also unwind one level
             * beyond its frame so the corrupt list-view input is visible. */
            if (sp && irix_guest_ptr(rt, sp + 0x20u, 8u))
                imd_caller_ra = (IrixGuestAddr)(uint32_t)
                    irix_guest_read64be(rt, sp + 0x20u);
            if (sp && irix_guest_ptr(rt, sp + 0x60u, 8u))
                imd_caller2_ra = (IrixGuestAddr)(uint32_t)
                    irix_guest_read64be(rt, sp + 0x60u);
            imd_caller = imd_caller_ra ?
                irix_guest_find_function(rt, imd_caller_ra) : NULL;
            imd_caller2 = imd_caller2_ra ?
                irix_guest_find_function(rt, imd_caller2_ra) : NULL;

            fprintf(stderr,
                    "IRIX malloc BAD REQUEST: size=%08x pc=%08x ra=%08x "
                    "callsite=%08x sp=%08x outer_ra=%08x outer_callsite=%08x "
                    "imd_ra=%08x:%s imd2_ra=%08x:%s "
                    "gp=%08x t9=%08x "
                    "a0-a7=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x "
                    "s0-s7=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x\n",
                    size, (uint32_t)cpu->pc, ra, callsite, sp,
                    outer_ra, outer_callsite,
                    imd_caller_ra,
                    imd_caller && imd_caller->name ? imd_caller->name : "?",
                    imd_caller2_ra,
                    imd_caller2 && imd_caller2->name ? imd_caller2->name : "?",
                    (uint32_t)cpu->gpr[28], (uint32_t)cpu->gpr[25],
                    (uint32_t)cpu->gpr[4], (uint32_t)cpu->gpr[5],
                    (uint32_t)cpu->gpr[6], (uint32_t)cpu->gpr[7],
                    (uint32_t)cpu->gpr[8], (uint32_t)cpu->gpr[9],
                    (uint32_t)cpu->gpr[10], (uint32_t)cpu->gpr[11],
                    (uint32_t)cpu->gpr[16], (uint32_t)cpu->gpr[17],
                    (uint32_t)cpu->gpr[18], (uint32_t)cpu->gpr[19],
                    (uint32_t)cpu->gpr[20], (uint32_t)cpu->gpr[21],
                    (uint32_t)cpu->gpr[22], (uint32_t)cpu->gpr[23]);

            if (callsite >= 24u &&
                irix_guest_ptr(rt, callsite - 24u, 56u)) {
                (void)0;
                for (i = 0; i < 14u; ++i) {
                    IrixGuestAddr a = callsite - 24u + i * 4u;
                    fprintf(stderr, " [%08x]=%08x",
                            a, irix_guest_read32be(rt, a));
                }
                fputc('\n', stderr);
            }

            if (outer_callsite >= 32u &&
                irix_guest_ptr(rt, outer_callsite - 32u, 80u)) {
                (void)0;
                for (i = 0; i < 20u; ++i) {
                    IrixGuestAddr a = outer_callsite - 32u + i * 4u;
                    fprintf(stderr, " [%08x]=%08x",
                            a, irix_guest_read32be(rt, a));
                }
                fputc('\n', stderr);
            }

            if (sp && irix_guest_ptr(rt, sp, 64u)) {
                (void)0;
                for (i = 0; i < 8u; ++i) {
                    IrixGuestAddr a = sp + i * 8u;
                    fprintf(stderr, " [%08x]=%016llx",
                            a,
                            (unsigned long long)irix_guest_read64be(rt, a));
                }
                fputc('\n', stderr);
            }
        }
    }

    cpu->gpr[2] = alloc_guest(size, 16u);
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
        if (old)
            old->in_use = 0;
        cpu->gpr[2] = 0;
        return;
    }

    /* A real realloc may keep the same address. Do that whenever the
       existing guest block has enough capacity. */
    if (old && old->capacity >= new_size) {
        old->size = new_size;
        cpu->gpr[2] = old_addr;
        return;
    }

    addr = alloc_guest(new_size, 16u);
    if (!addr) {
        cpu->gpr[2] = 0;
        return;
    }

    copy_size = old && old->size < new_size ? old->size : new_size;
    dst = irix_guest_ptr(rt, addr, copy_size);
    src = irix_guest_ptr(rt, old_addr, copy_size);
    if (dst && src)
        memcpy(dst, src, copy_size);
    if (old)
        old->in_use = 0;
    cpu->gpr[2] = addr;
}

static void bridge_memalign(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    uint32_t size = (uint32_t)cpu->gpr[5];
    uint32_t align = (uint32_t)cpu->gpr[4];

    
    cpu->gpr[2] = alloc_guest(size, align);
}

static void bridge_free(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr addr = (IrixGuestAddr)(uint32_t)cpu->gpr[4];
    IrixGuestAllocation *a;
    (void)rt;

    if (!addr)
        return;

    a = find_alloc(addr);
    if (a) {
        a->in_use = 0;
        a->size = 0u;
    }
}

static void bridge_strdup(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr src = (IrixGuestAddr)(uint32_t)cpu->gpr[4];
    const char *s = guest_cstr(rt, src);
    IrixGuestAddr out = copy_host_string_to_guest(rt, s);
    
    cpu->gpr[2] = out;
}

typedef struct IrixSptyHostId {
    uint32_t index;
    uint32_t side;
} IrixSptyHostId;

#define IRIX_SPTY_HOST_GET_ID _IOR('I', 0xf0, IrixSptyHostId)
#define IRIX_SPTY_SIDE_MASTER 0u
#define IRIX_UNLKPT            (('P' << 8) | 2)

static void bridge__getpty(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr fildes_addr = (uint32_t)cpu->gpr[4];
    mode_t mode = (mode_t)((uint32_t)cpu->gpr[6] & 0666u);
    IrixSptyHostId id;
    char name[32];
    IrixGuestAddr result;
    int fd;
    unsigned family;
    unsigned minor_no;

    /*
     * IRIX _getpty() is a libc wrapper around the PTC clone device.  Keep the
     * naming rule from getpty.c, but let the Linux spty module provide the
     * controller identity instead of depending on translated libc's writable
     * DSO data aliases (__slave0/__slave1).
     */
    fd = open("/dev/ptc", O_RDWR | O_NOCTTY);
    if (fd < 0) {
        /*
         * No irix_spty module: fall back to the host's standard Unix98 pty.
         * The controller fd is the guest's master; the slave is the plain
         * /dev/pts/N node, which the guest opens by the returned name.
         */
        char *slave;
        fd = posix_openpt(O_RDWR | O_NOCTTY);
        if (fd < 0)
            goto fail;
        if (grantpt(fd) < 0 || unlockpt(fd) < 0 ||
            !(slave = ptsname(fd)))
            goto fail_close;
        (void)chmod(slave, mode);
        result = copy_host_string_to_guest(rt, slave);
        if (!result)
            goto fail_close;
        if (fildes_addr)
            irix_guest_write32be(rt, fildes_addr, (uint32_t)fd);
        else
            close(fd);
        cpu->gpr[2] = result;
        return;
    }
    if (ioctl(fd, IRIX_SPTY_HOST_GET_ID, &id) < 0 ||
        id.side != IRIX_SPTY_SIDE_MASTER || id.index >= 1000u)
        goto fail_close;

    family = id.index / 100u;
    minor_no = id.index % 100u;
    if (family >= 10u)
        goto fail_close;
    snprintf(name, sizeof(name), "/dev/tty%c%u",
             "qrstuvwxyz"[family], minor_no);

    /* The module exposes the historical IRIX slave node directly. */
    (void)chmod(name, mode);
    if (ioctl(fd, IRIX_UNLKPT) < 0)
        goto fail_close;

    result = copy_host_string_to_guest(rt, name);
    if (!result)
        goto fail_close;
    if (fildes_addr)
        irix_guest_write32be(rt, fildes_addr, (uint32_t)fd);
    else
        close(fd);

    cpu->gpr[2] = result;
    return;

fail_close:
    close(fd);
fail:
    cpu->gpr[2] = 0;
}

static void bridge_sprintf(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr dst_addr = (uint32_t)cpu->gpr[4];
    const char *fmt = guest_cstr(rt, (uint32_t)cpu->gpr[5]);
    char *dst = (char *)irix_guest_ptr(rt, dst_addr, 1u);
    const char *p;
    char *out;
    unsigned arg = 6u;

    if (!dst || !fmt) {
        cpu->gpr[2] = (uint64_t)(int64_t)-1;
        return;
    }

    /*
     * N32 passes the integer/pointer varargs following dst/fmt in a2-a7.
     * Keep this bridge deliberately to the conversion set used by the IRIX
     * libc paths we currently execute; unlike host sprintf, guest pointers
     * must be translated before %s can be consumed.
     */
    out = dst;
    for (p = fmt; *p; ++p) {
        char tmp[128], spec[32];
        int n = 0, si = 0, longs = 0;
        uint64_t v;

        if (*p != '%') {
            *out++ = *p;
            continue;
        }
        if (p[1] == '%') {
            *out++ = '%';
            ++p;
            continue;
        }
        spec[si++] = *p++;
        while (*p && strchr("-+ #0123456789.", *p) && si < 20)
            spec[si++] = *p++;
        while (*p && strchr("hlLqzjt", *p)) {
            if (*p == 'l' || *p == 'q' || *p == 'L') ++longs;
            ++p;
        }
        if (!*p) break;
        /* a2-a7 are gpr 6..11; further N32 varargs sit in 8-byte stack slots */
        if (arg <= 11u)
            v = cpu->gpr[arg++];
        else {
            v = irix_guest_read64be(rt, (uint32_t)cpu->gpr[29] + 8u * (arg - 12u));
            ++arg;
        }
        switch (*p) {
        case 'c':
            spec[si++] = 'c'; spec[si] = 0;
            n = snprintf(tmp, sizeof(tmp), spec, (int)(uint32_t)v);
            break;
        case 'd': case 'i':
            spec[si++] = 'l'; spec[si++] = 'l'; spec[si++] = 'd'; spec[si] = 0;
            n = snprintf(tmp, sizeof(tmp), spec,
                         longs ? (long long)(int64_t)v : (long long)(int32_t)v);
            break;
        case 'u': case 'x': case 'X': case 'o':
            spec[si++] = 'l'; spec[si++] = 'l'; spec[si++] = *p; spec[si] = 0;
            n = snprintf(tmp, sizeof(tmp), spec,
                         longs ? (unsigned long long)v
                               : (unsigned long long)(uint32_t)v);
            break;
        case 'p':
            n = snprintf(tmp, sizeof(tmp), "0x%x", (uint32_t)v);
            break;
        case 's': {
            const char *str = guest_cstr(rt, (uint32_t)v);
            if (!str) str = "(null)";
            spec[si++] = 's'; spec[si] = 0;
            n = snprintf(tmp, sizeof(tmp), spec, str);
            if (n >= (int)sizeof(tmp)) {   /* long string: copy verbatim */
                n = (int)strlen(str);
                memcpy(out, str, (size_t)n);
                out += n;
                n = 0;
            }
            break;
        }
        default:
            *out = '\0';
            cpu->gpr[2] = (uint64_t)(int64_t)-1;
            return;
        }
        if (n > 0) {
            if (n >= (int)sizeof(tmp)) n = (int)sizeof(tmp) - 1;
            memcpy(out, tmp, (size_t)n);
            out += n;
        }
    }
    *out = '\0';
    cpu->gpr[2] = (uint64_t)(uint32_t)(out - dst);
}

static void bridge_strlen(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr addr = (uint32_t)cpu->gpr[4];
    const char *s = guest_cstr(rt, addr);
    size_t length = s ? strlen(s) : 0u;
    /* IRIX's private commize() helper underflows when an optional extra-label
       string is empty.  Treat its one-byte NUL input as length one so the
       helper copies an empty result instead of requesting ~1.4 GB. */
    if (s && length == 0u && (uint32_t)cpu->gpr[31] == 0x09691a10u)
        length = 1u;
    cpu->gpr[2] = length;
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
    IrixGuestAddr caller = (IrixGuestAddr)((uint32_t)cpu->gpr[31] - 8u);
    if ((caller >= 0x0fa88c00u && caller < 0x0fa89300u) ||
        (caller >= 0x0fa45c00u && caller < 0x0fa45e00u)) {
        (void)0;
        if (src_addr && irix_guest_ptr(rt, src_addr, n ? n : 1u)) {
            unsigned i, lim = n < 48u ? n : 48u;
            fputs("  src:", stderr);
            for (i = 0; i < lim; ++i)
                fprintf(stderr, " %02x",
                        (unsigned)*(const unsigned char *)irix_guest_ptr(rt, src_addr + i, 1u));
            fputc('\n', stderr);
        }
    }
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

static void bridge_getoptreset(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    irix_guest_write32be(rt, g_libc.optind_addr, 1u);
    irix_guest_write32be(rt, g_libc.optarg_addr, 0u);
    irix_guest_write32be(rt, g_libc.optopt_addr, 0u);
    irix_guest_write32be(rt, g_libc.getopt_sp_addr, 1u);
    cpu->gpr[2] = 0;
}

static void bridge_getopt(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    int argc = (int)(int32_t)cpu->gpr[4];
    IrixGuestAddr argv_addr = (uint32_t)cpu->gpr[5];
    IrixGuestAddr opts_addr = (uint32_t)cpu->gpr[6];
    const char *opts = guest_cstr(rt, opts_addr);
    uint32_t optind = irix_guest_read32be(rt, g_libc.optind_addr);
    uint32_t opterr = irix_guest_read32be(rt, g_libc.opterr_addr);
    uint32_t sp = irix_guest_read32be(rt, g_libc.getopt_sp_addr);
    uint32_t start_optind = optind;
    uint32_t start_sp = sp;
    IrixGuestAddr arg_addr = 0;
    IrixGuestAddr optarg = 0;
    const char *arg = 0;
    const char *cp;
    int result = -1;
    unsigned char ch = 0;

    if (!opts || argc < 0)
        goto done;

    if (sp == 1u) {
        if (optind >= (uint32_t)argc)
            goto done;
        arg_addr = irix_guest_read32be(rt, argv_addr + optind * 4u);
        arg = guest_cstr(rt, arg_addr);
        if (!arg || arg[0] != '-' || arg[1] == '\0')
            goto done;
        if (strcmp(arg, "--") == 0) {
            ++optind;
            goto done;
        }
    } else {
        if (optind >= (uint32_t)argc)
            goto done;
        arg_addr = irix_guest_read32be(rt, argv_addr + optind * 4u);
        arg = guest_cstr(rt, arg_addr);
        if (!arg)
            goto done;
    }

    ch = (unsigned char)arg[sp];
    irix_guest_write32be(rt, g_libc.optopt_addr, (uint32_t)ch);
    cp = strchr(opts, ch);

    if (ch == ':' || !cp) {
        if (opterr && opts[0] != ':') {
            IrixGuestAddr prog_addr = irix_guest_read32be(rt, argv_addr);
            const char *prog = guest_cstr(rt, prog_addr);
            fprintf(stderr, "%s: Illegal option -- %c\n", prog ? prog : "getopt", ch);
        }
        ++sp;
        if (arg[sp] == '\0') {
            ++optind;
            sp = 1u;
        }
        result = '?';
        goto done;
    }

    if (cp[1] == ':') {
        if (arg[sp + 1u] != '\0') {
            optarg = arg_addr + sp + 1u;
            ++optind;
        } else {
            ++optind;
            if (optind >= (uint32_t)argc) {
                sp = 1u;
                if (opts[0] == ':') {
                    result = ':';
                    goto done;
                }
                if (opterr) {
                    IrixGuestAddr prog_addr = irix_guest_read32be(rt, argv_addr);
                    const char *prog = guest_cstr(rt, prog_addr);
                    fprintf(stderr, "%s: Option requires an argument -- %c\n",
                            prog ? prog : "getopt", ch);
                }
                result = '?';
                goto done;
            }
            optarg = irix_guest_read32be(rt, argv_addr + optind * 4u);
            ++optind;
        }
        sp = 1u;
    } else {
        ++sp;
        if (arg[sp] == '\0') {
            sp = 1u;
            ++optind;
        }
        optarg = 0;
    }

    result = (int)ch;

done:
    irix_guest_write32be(rt, g_libc.optind_addr, optind);
    irix_guest_write32be(rt, g_libc.optarg_addr, optarg);
    irix_guest_write32be(rt, g_libc.getopt_sp_addr, sp);
    
    cpu->gpr[2] = (uint64_t)(int64_t)(int32_t)result;
}

static void bridge_getenv(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *name = guest_cstr(rt, (uint32_t)cpu->gpr[4]);
    const char *value;
    size_t i;
    if (!name) { cpu->gpr[2] = 0; return; }
    value = getenv(name);
    /* The host login shell (e.g. /usr/local/bin/ksh) does not exist in the
       guest tree; f.exec/system()-style callers would exec it and fail. */
    if (!strcmp(name, "SHELL")) value = "/bin/sh";
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

static void bridge_sqrt(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    double value;
    (void)rt;
    value = irix_guest_fpr_get_d(cpu, 12);
    irix_guest_fpr_set_d(cpu, 0, sqrt(value));
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
    g_libc.errno_addr = alloc_guest(4u, 4u);
    g_libc.optarg_addr = alloc_guest(4u, 4u);
    g_libc.opterr_addr = alloc_guest(4u, 4u);
    g_libc.optind_addr = alloc_guest(4u, 4u);
    g_libc.optopt_addr = alloc_guest(4u, 4u);
    g_libc.getopt_sp_addr = alloc_guest(4u, 4u);
    if (!g_libc.errno_addr || !g_libc.optarg_addr || !g_libc.opterr_addr ||
        !g_libc.optind_addr || !g_libc.optopt_addr || !g_libc.getopt_sp_addr)
        return -1;
    irix_guest_set_errno_proxy(rt, g_libc.errno_addr);
    irix_guest_write32be(rt, g_libc.optarg_addr, 0u);
    irix_guest_write32be(rt, g_libc.opterr_addr, 1u);
    irix_guest_write32be(rt, g_libc.optind_addr, 1u);
    irix_guest_write32be(rt, g_libc.optopt_addr, 0u);
    irix_guest_write32be(rt, g_libc.getopt_sp_addr, 1u);
    return 0;
}

IrixGuestAddr irix_libc_bridge_data_lookup(const char *name)
{
    if (!name)
        return 0;
    if (!strcmp(name, "errno"))
        return g_libc.errno_addr;
    if (!strcmp(name, "optarg") || !strcmp(name, "_optarg"))
        return g_libc.optarg_addr;
    if (!strcmp(name, "opterr") || !strcmp(name, "_opterr"))
        return g_libc.opterr_addr;
    if (!strcmp(name, "optind") || !strcmp(name, "_optind"))
        return g_libc.optind_addr;
    if (!strcmp(name, "optopt") || !strcmp(name, "_optopt"))
        return g_libc.optopt_addr;
    if (!strcmp(name, "_sp"))
        return g_libc.getopt_sp_addr;
    return 0;
}

IrixGuestFunction irix_libc_bridge_lookup(const char *name)
{
    if (!name) return 0;
    if (!strcmp(name, "malloc") || !strcmp(name, "_malloc") || !strcmp(name, "__nw__GUi") || !strcmp(name, "__nwa__GUi")) return bridge_malloc;
    if (!strcmp(name, "calloc") || !strcmp(name, "_calloc")) return bridge_calloc;
    if (!strcmp(name, "realloc") || !strcmp(name, "_realloc")) return bridge_realloc;
    if (!strcmp(name, "memalign")) return bridge_memalign;
    if (!strcmp(name, "free") || !strcmp(name, "_free") || !strcmp(name, "__dl__GPv") || !strcmp(name, "__dla__GPv")) return bridge_free;
    if (!strcmp(name, "strdup")) return bridge_strdup;
    if (!strcmp(name, "sprintf")) return bridge_sprintf;
    if (!strcmp(name, "_getpty")) return bridge__getpty;
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
    if (!strcmp(name, "getopt") || !strcmp(name, "_getopt")) return bridge_getopt;
    if (!strcmp(name, "getoptreset") || !strcmp(name, "_getoptreset")) return bridge_getoptreset;
    if (!strcmp(name, "getenv")) return bridge_getenv;
    if (!strcmp(name, "getpid")) return bridge_getpid;
    if (!strcmp(name, "getppid")) return bridge_getppid;
    if (!strcmp(name, "getuid")) return bridge_getuid;
    if (!strcmp(name, "geteuid")) return bridge_geteuid;
    if (!strcmp(name, "getgid")) return bridge_getgid;
    if (!strcmp(name, "getegid")) return bridge_getegid;
    if (!strcmp(name, "setlocale")) return bridge_setlocale;
    if (!strcmp(name, "sqrt")) return bridge_sqrt;
    return 0;
}
