#include "IrixGuest.h"
#include "IrixMipsInterp.h"
#include "IrixMipsJit.h"
#include "IrixCompat.h"
#include "IrixSyscallNumbers.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>

/* Runtime-private guest-libc helpers used by narrow native compatibility
   implementations. Keep them out of IrixGuest.h to avoid rebuilding every
   generated translation unit. */
extern IrixGuestAddr irix_libc_guest_malloc(uint32_t size);

#define IRIX_NSIG 128u
#define IRIX_SIGACTION_N32_SIZE 32u

typedef struct IrixKernelSigAction {
    uint32_t flags;
    uint32_t handler;
    uint32_t mask[4];
    uint32_t sigtramp;
} IrixKernelSigAction;

static IrixKernelSigAction g_kernel_sigactions[IRIX_NSIG];

#define IRIX_GUEST_GP_RANGE_MAX 512u

typedef struct IrixGuestGpRange {
    IrixGuestAddr base;
    uint32_t size;
    IrixGuestAddr gp;
} IrixGuestGpRange;

static IrixGuestGpRange g_guest_gp_ranges[IRIX_GUEST_GP_RANGE_MAX];
static size_t g_guest_gp_range_count;

void irix_guest_register_gp_range(IrixGuestAddr base, uint32_t size,
                                  IrixGuestAddr gp)
{
    IrixGuestGpRange *r;
    if (!size || !gp || g_guest_gp_range_count >= IRIX_GUEST_GP_RANGE_MAX)
        return;
    r = &g_guest_gp_ranges[g_guest_gp_range_count++];
    r->base = base;
    r->size = size;
    r->gp = gp;
}

IrixGuestAddr irix_guest_gp_for_address(IrixGuestAddr address)
{
    size_t i;
    static int warned_vk_fallback;

    for (i = g_guest_gp_range_count; i > 0; --i) {
        const IrixGuestGpRange *r = &g_guest_gp_ranges[i - 1u];
        uint64_t end = (uint64_t)r->base + (uint64_t)r->size;
        if ((uint64_t)address >= r->base && (uint64_t)address < end)
            return r->gp;
    }

    /*
     * Direct-main bring-up fallback for SGI ViewKit/libvk.
     *
     * The original libvk text executes in the 0x0d6xxxxx region and its
     * verified N32 GP is 0x0d6cf058. Some extracted libvk images do not expose
     * usable GP metadata through the loader path yet, leaving this range
     * unowned and allowing callers to inherit libXt's 0x0f69eef0 GP.
     *
     * That produces the reproducible failure at 0x0d6352e0:
     *
     *   lw t9,-0x7480(gp)
     *   -> with Xt GP reads ASCII "mous" (0x6d6f7573)
     *   -> jalr t9
     *
     * Keep this fallback address-based and subordinate to all discovered GP
     * ranges so a proper ELF-derived registration automatically wins.
     */
    if (address >= 0x0d600000u && address < 0x0d700000u) {
        if (!warned_vk_fallback) {
            fprintf(stderr,
                    "IRIX GP ownership fallback: ViewKit/libvk 0x0d6xxxxx -> gp=0x0d6cf058\n");
            warned_vk_fallback = 1;
        }
        return 0x0d6cf058u;
    }

    return 0;
}


#define IRIX_PROFILE_FN_SLOTS   262144u
#define IRIX_PROFILE_EDGE_SLOTS 524288u

typedef struct IrixGuestProfileFunction {
    IrixGuestAddr address;
    uint32_t flags;
    uint64_t count;
    const char *name;
    int used;
} IrixGuestProfileFunction;

typedef struct IrixGuestProfileEdge {
    IrixGuestAddr caller;
    IrixGuestAddr callee;
    uint64_t count;
    int used;
} IrixGuestProfileEdge;

typedef struct IrixGuestProfileState {
    char *path;
    char *process_name;
    IrixGuestProfileFunction *functions;
    IrixGuestProfileEdge *edges;
    size_t function_used;
    size_t edge_used;
} IrixGuestProfileState;

static uint32_t irix_profile_hash32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

static char *irix_profile_strdup(const char *s)
{
    size_t n;
    char *out;
    if (!s) return 0;
    n = strlen(s) + 1u;
    out = (char *)malloc(n);
    if (out) memcpy(out, s, n);
    return out;
}

static void irix_guest_profile_init(IrixGuestRuntime *rt)
{
    const char *dir = getenv("IRIX_PROFILE_DIR");
    const char *name = getenv("IRIX_PROFILE_NAME");
    IrixGuestProfileState *p;
    size_t path_n;
    char *path;

    if (!dir || !*dir)
        return;
    if (!name || !*name)
        name = "irix-process";

    (void)mkdir(dir, 0777);
    p = (IrixGuestProfileState *)calloc(1, sizeof(*p));
    if (!p)
        return;
    p->functions = (IrixGuestProfileFunction *)calloc(
        IRIX_PROFILE_FN_SLOTS, sizeof(*p->functions));
    p->edges = (IrixGuestProfileEdge *)calloc(
        IRIX_PROFILE_EDGE_SLOTS, sizeof(*p->edges));
    p->process_name = irix_profile_strdup(name);
    if (!p->functions || !p->edges || !p->process_name)
        goto fail;

    path_n = strlen(dir) + strlen(name) + 64u;
    path = (char *)malloc(path_n);
    if (!path)
        goto fail;
    snprintf(path, path_n, "%s/%s-%ld.tsv", dir, name, (long)getpid());
    p->path = path;
    rt->profile_state = p;
    fprintf(stderr, "IRIX profile: process=%s output=%s\n", name, path);
    return;

fail:
    free(p->functions);
    free(p->edges);
    free(p->process_name);
    free(p->path);
    free(p);
}

static void irix_guest_profile_hit(IrixGuestRuntime *rt,
                                   IrixGuestAddr caller,
                                   IrixGuestAddr callee,
                                   IrixGuestFunctionEntry *entry)
{
    IrixGuestProfileState *p = (IrixGuestProfileState *)rt->profile_state;
    uint32_t slot, start;

    if (!p || !callee)
        return;

    slot = irix_profile_hash32(callee) & (IRIX_PROFILE_FN_SLOTS - 1u);
    start = slot;
    while (p->functions[slot].used &&
           p->functions[slot].address != callee) {
        slot = (slot + 1u) & (IRIX_PROFILE_FN_SLOTS - 1u);
        if (slot == start)
            break;
    }
    if (!p->functions[slot].used) {
        p->functions[slot].used = 1;
        p->functions[slot].address = callee;
        p->functions[slot].flags = entry ? entry->flags : 0u;
        p->functions[slot].name = entry ? entry->name : 0;
        ++p->function_used;
    }
    ++p->functions[slot].count;

    if (caller) {
        uint32_t h = irix_profile_hash32(caller ^ (callee * 0x9e3779b9u));
        slot = h & (IRIX_PROFILE_EDGE_SLOTS - 1u);
        start = slot;
        while (p->edges[slot].used &&
               (p->edges[slot].caller != caller ||
                p->edges[slot].callee != callee)) {
            slot = (slot + 1u) & (IRIX_PROFILE_EDGE_SLOTS - 1u);
            if (slot == start)
                break;
        }
        if (!p->edges[slot].used) {
            p->edges[slot].used = 1;
            p->edges[slot].caller = caller;
            p->edges[slot].callee = callee;
            ++p->edge_used;
        }
        ++p->edges[slot].count;
    }
}

static void irix_profile_write_name(FILE *out, const char *name)
{
    const unsigned char *s = (const unsigned char *)(name ? name : "?");
    while (*s) {
        unsigned char ch = *s++;
        fputc((ch == '\t' || ch == '\r' || ch == '\n') ? ' ' : ch, out);
    }
}

static void irix_guest_profile_flush(IrixGuestRuntime *rt)
{
    IrixGuestProfileState *p = (IrixGuestProfileState *)rt->profile_state;
    FILE *out;
    size_t i;

    if (!p || !p->path)
        return;
    out = fopen(p->path, "w");
    if (!out) {
        fprintf(stderr, "IRIX profile: cannot write %s: %s\n",
                p->path, strerror(errno));
        return;
    }

    fprintf(out, "# irix-call-profile-v1\n");
    fprintf(out, "# process\t%s\n", p->process_name ? p->process_name : "?");
    fprintf(out, "# functions\t%zu\n", p->function_used);
    fprintf(out, "# edges\t%zu\n", p->edge_used);
    for (i = 0; i < IRIX_PROFILE_FN_SLOTS; ++i) {
        IrixGuestProfileFunction *fn = &p->functions[i];
        if (!fn->used)
            continue;
        fprintf(out, "F\t%08x\t%llu\t%08x\t",
                fn->address, (unsigned long long)fn->count, fn->flags);
        irix_profile_write_name(out, fn->name);
        fputc('\n', out);
    }
    for (i = 0; i < IRIX_PROFILE_EDGE_SLOTS; ++i) {
        IrixGuestProfileEdge *edge = &p->edges[i];
        if (!edge->used)
            continue;
        fprintf(out, "E\t%08x\t%08x\t%llu\n",
                edge->caller, edge->callee,
                (unsigned long long)edge->count);
    }
    fclose(out);
    fprintf(stderr,
            "IRIX profile: wrote %zu functions and %zu call edges to %s\n",
            p->function_used, p->edge_used, p->path);
}

static void irix_guest_profile_destroy(IrixGuestRuntime *rt)
{
    IrixGuestProfileState *p = (IrixGuestProfileState *)rt->profile_state;
    if (!p)
        return;
    irix_guest_profile_flush(rt);
    free(p->functions);
    free(p->edges);
    free(p->path);
    free(p->process_name);
    free(p);
    rt->profile_state = 0;
}

static int read_irix_sigaction_n32(IrixGuestRuntime *rt, IrixGuestAddr addr,
                                   IrixKernelSigAction *out)
{
    unsigned i;
    if (!addr || !out || !irix_guest_ptr(rt, addr, IRIX_SIGACTION_N32_SIZE))
        return -1;
    out->flags = irix_guest_read32be(rt, addr + 0u);
    out->handler = irix_guest_read32be(rt, addr + 4u);
    for (i = 0; i < 4u; ++i)
        out->mask[i] = irix_guest_read32be(rt, addr + 8u + i * 4u);
    return rt->trapped ? -1 : 0;
}

static int write_irix_sigaction_n32(IrixGuestRuntime *rt, IrixGuestAddr addr,
                                    const IrixKernelSigAction *in)
{
    unsigned i;
    if (!addr || !in || !irix_guest_ptr(rt, addr, IRIX_SIGACTION_N32_SIZE))
        return -1;
    irix_guest_write32be(rt, addr + 0u, in->flags);
    irix_guest_write32be(rt, addr + 4u, in->handler);
    for (i = 0; i < 4u; ++i)
        irix_guest_write32be(rt, addr + 8u + i * 4u, in->mask[i]);
    irix_guest_write32be(rt, addr + 24u, 0u);
    irix_guest_write32be(rt, addr + 28u, 0u);
    return rt->trapped ? -1 : 0;
}

static IrixGuestSegment *find_segment(IrixGuestRuntime *rt, IrixGuestAddr addr, size_t size)
{
    size_t i;
    uint64_t end = (uint64_t)addr + (uint64_t)size;
    for (i = 0; i < rt->memory.count; ++i) {
        IrixGuestSegment *seg = &rt->memory.segments[i];
        uint64_t seg_end = (uint64_t)seg->guest_base + (uint64_t)seg->size;
        if ((uint64_t)addr >= seg->guest_base && end <= seg_end)
            return seg;
    }
    return 0;
}

static void memory_fault(IrixGuestRuntime *rt, IrixGuestAddr addr)
{
    if (!rt->trapped) {
        rt->trapped = 1;
        rt->trap_pc = rt->current_pc;
        rt->fault_addr = addr;
        rt->trap_code = 0xeeee0004u;
    }
}

void irix_guest_runtime_init(IrixGuestRuntime *rt)
{
    memset(rt, 0, sizeof(*rt));
    g_guest_gp_range_count = 0u;
    irix_guest_profile_init(rt);
}

void irix_guest_runtime_destroy(IrixGuestRuntime *rt)
{
    size_t i;
    irix_guest_profile_destroy(rt);
    irix_mips_jit_shutdown(rt);
    for (i = 0; i < rt->memory.count; ++i)
        free(rt->memory.segments[i].host_base);
    memset(rt, 0, sizeof(*rt));
}

int irix_guest_map(IrixGuestRuntime *rt, IrixGuestAddr base, uint32_t size, unsigned flags)
{
    IrixGuestSegment *seg;
    if (!size || rt->memory.count >= IRIX_GUEST_MAX_SEGMENTS)
        return -1;
    if (find_segment(rt, base, size))
        return -1;
    seg = &rt->memory.segments[rt->memory.count];
    {
        size_t alloc_size = ((size_t)size + 0xfffu) & ~(size_t)0xfffu;
        if (alloc_size < size)
            return -1;
        seg->host_base = (uint8_t *)calloc(1, alloc_size);
    }
    if (!seg->host_base)
        return -1;
    seg->guest_base = base;
    seg->size = size;
    seg->flags = flags;
    ++rt->memory.count;
    return 0;
}

void *irix_guest_ptr(IrixGuestRuntime *rt, IrixGuestAddr addr, size_t size)
{
    IrixGuestSegment *seg = find_segment(rt, addr, size);
    if (!seg)
        return 0;
    return seg->host_base + (uint32_t)(addr - seg->guest_base);
}

#define IRIX_XT_COMPAT_BASE 0x74400000u
#define IRIX_XT_COMPAT_SIZE 0x00100000u
#define IRIX_XT_RESOURCE_SIZE 28u

/*
 * Current Xt shell/widget allocation class hint.
 *
 * SGI Motif's allocation proc is recovered as an interior entry.  By the time
 * that interior range is dispatched, its incoming a0 is not always the
 * original WidgetClass argument anymore.  XtAppCreateShell still gives us the
 * authoritative class in a2, so retain it for the duration of the shell
 * construction and use it only as a narrowly-scoped allocation fallback.
 */
static IrixGuestAddr g_xt_alloc_widget_class_hint;

static IrixGuestAddr irix_guest_xt_compat_alloc(IrixGuestRuntime *rt, uint32_t size)
{
    IrixGuestAddr out;
    uint32_t n = (size + 7u) & ~7u;

    if (!n)
        return 0;
    if (!rt->compat_alloc_cur) {
        if (irix_guest_map(rt, IRIX_XT_COMPAT_BASE, IRIX_XT_COMPAT_SIZE, 3u) != 0 &&
            !irix_guest_ptr(rt, IRIX_XT_COMPAT_BASE, 1u))
            return 0;
        rt->compat_alloc_cur = IRIX_XT_COMPAT_BASE;
    }
    if (rt->compat_alloc_cur + n < rt->compat_alloc_cur ||
        rt->compat_alloc_cur + n > IRIX_XT_COMPAT_BASE + IRIX_XT_COMPAT_SIZE)
        return 0;
    out = rt->compat_alloc_cur;
    rt->compat_alloc_cur += n;
    return out;
}

void irix_guest_repair_xm_screen_private(IrixGuestRuntime *rt,
                                         IrixGuestAddr screen)
{
    IrixGuestAddr priv;

    /*
     * SGI Motif XmScreen carries a private pointer at +0x104. The original
     * instance-initialize path allocates an 8-byte zeroed block there.
     * Recovered/interpreted continuation entries can bypass the dispatcher
     * hook that used to repair only the public XmGetXmScreen entry.
     */
    if (!screen || !irix_guest_ptr(rt, screen, 0x108u) ||
        irix_guest_read32be(rt, screen + 0x104u) != 0u)
        return;

    priv = irix_guest_xt_compat_alloc(rt, 8u);
    if (!priv || !irix_guest_ptr(rt, priv, 8u))
        return;

    irix_guest_write32be(rt, priv + 0u, 0u);
    irix_guest_write32be(rt, priv + 4u, 0u);
    irix_guest_write32be(rt, screen + 0x104u, priv);
    fprintf(stderr,
            "IRIX XmScreen repair: screen=%08x private=%08x\n",
            screen, priv);
}

static int irix_guest_xt_resources_are_indirect(IrixGuestRuntime *rt,
                                                 IrixGuestAddr resources,
                                                 uint32_t num_resources)
{
    IrixGuestAddr first;
    uint32_t size;

    if (!resources || !num_resources)
        return 0;
    if (!irix_guest_ptr(rt, resources, 4u))
        return 0;
    first = irix_guest_read32be(rt, resources);
    if (!first || !irix_guest_ptr(rt, first, IRIX_XT_RESOURCE_SIZE))
        return 0;
    size = irix_guest_read32be(rt, first + 12u);
    return size > 0u && size <= 0x10000u;
}

int irix_guest_xt_normalize_resource(IrixGuestRuntime *rt,
                                     IrixGuestAddr res)
{
    uint32_t size;
    uint32_t off;
    uint32_t q;
    static const uint32_t fields[] = {0u, 4u, 8u, 20u};
    unsigned k;

    if (!res || !irix_guest_ptr(rt, res, IRIX_XT_RESOURCE_SIZE))
        return 0;

    size = irix_guest_read32be(rt, res + 12u);
    if (!size || size > 0x10000u)
        return 0;

    /*
     * _XtCompileResourceList interns resource_name, resource_class,
     * resource_type and default_type into the Xrm quark namespace.  This is
     * deliberately idempotent: positive values that are already Xrm quarks
     * do not map to guest strings, so quarkify_encoded returns zero and we
     * leave them untouched.  SGI FAST_QUARK values and real guest String
     * pointers are converted.
     */
    if (rt->xrm_quarkify_encoded) {
        for (k = 0; k < sizeof(fields) / sizeof(fields[0]); ++k) {
            uint32_t value = irix_guest_read32be(rt, res + fields[k]);
            q = rt->xrm_quarkify_encoded(rt, value);
            if (q)
                irix_guest_write32be(rt, res + fields[k], q);
        }
    }

    off = irix_guest_read32be(rt, res + 16u);
    if ((int32_t)off >= 0)
        irix_guest_write32be(rt, res + 16u, ~off);

    return 1;
}

IrixGuestAddr irix_guest_xt_ensure_resource_table(IrixGuestRuntime *rt,
                                                  IrixGuestAddr table,
                                                  uint32_t num_resources)
{
    IrixGuestAddr out;
    uint32_t i;

    if (!table || !num_resources || num_resources > 4096u)
        return table;

    /*
     * If this is already an XrmResourceList[], still normalize the pointed-to
     * records.  Earlier compatibility passes may have built the pointer table
     * while leaving the String/FAST_QUARK fields uncompiled.
     */
    if (irix_guest_ptr(rt, table, (size_t)num_resources * 4u)) {
        int all_indirect = 1;
        for (i = 0; i < num_resources; ++i) {
            IrixGuestAddr res =
                irix_guest_read32be(rt, table + i * 4u);
            if (!irix_guest_xt_normalize_resource(rt, res)) {
                all_indirect = 0;
                break;
            }
        }
        if (all_indirect)
            return table;
    }

    /*
     * Otherwise accept the input as a raw contiguous XtResource[] and perform
     * the same compilation before creating the XrmResourceList[] indirection
     * table expected by GetResources().
     */
    if (!irix_guest_ptr(rt, table,
                        (size_t)num_resources * IRIX_XT_RESOURCE_SIZE))
        return table;

    for (i = 0; i < num_resources; ++i) {
        IrixGuestAddr res = table + i * IRIX_XT_RESOURCE_SIZE;
        if (!irix_guest_xt_normalize_resource(rt, res))
            return table;
    }

    out = irix_guest_xt_compat_alloc(rt, num_resources * 4u);
    if (!out)
        return table;
    for (i = 0; i < num_resources; ++i)
        irix_guest_write32be(rt, out + i * 4u,
                             table + i * IRIX_XT_RESOURCE_SIZE);
    return out;
}

static void irix_guest_xt_repair_class_resources_inner(IrixGuestRuntime *rt,
                                                        IrixGuestAddr wc,
                                                        unsigned depth)
{
    /*
     * SGI/N32 CoreClassPart offsets used here:
     *   +0x00 superclass
     *   +0x08 widget_size
     *   +0x2c resources
     *   +0x30 num_resources
     *   +0x60 callback_private
     *
     * ConstructCallbackOffsets() always installs callback_private, even when
     * the class has zero callback resources.  The SGI FAST_QUARK encoding for
     * XtRCallback in this libXt build is 0xffffffe3 (verified from the sole
     * Object resource, destroyCallback).
     */
    enum { IRIX_XT_CALLBACK_PRIVATE = 0x60u };
    const uint32_t irix_xt_r_callback = 0xffffffe3u;
    uint32_t irix_xt_q_callback = 0u;
    IrixGuestAddr sc;
    IrixGuestAddr resources;
    IrixGuestAddr super_resources = 0;
    IrixGuestAddr super_callbacks = 0;
    IrixGuestAddr table = 0;
    IrixGuestAddr callback_table;
    uint32_t class_num;
    uint32_t super_num = 0;
    uint32_t super_callback_num = 0;
    uint32_t super_widget_size = 0;
    uint32_t new_num = 0;
    uint32_t next = 0;
    uint32_t own_callback_num = 0;
    uint32_t callback_next = 1;
    uint32_t i, j;
    int resources_indirect;

    if (!wc || depth > 64u || !irix_guest_ptr(rt, wc, 100u))
        return;

    sc = irix_guest_read32be(rt, wc + 0u);
    resources = irix_guest_read32be(rt, wc + 44u);
    class_num = irix_guest_read32be(rt, wc + 48u);
    resources_indirect =
        irix_guest_xt_resources_are_indirect(rt, resources, class_num);
    if (rt->xrm_quarkify_encoded)
        irix_xt_q_callback =
            rt->xrm_quarkify_encoded(rt, irix_xt_r_callback);

    if (sc) {
        irix_guest_xt_repair_class_resources_inner(rt, sc, depth + 1u);
        if (irix_guest_ptr(rt, sc, 100u)) {
            super_widget_size = irix_guest_read32be(rt, sc + 8u);
            super_resources = irix_guest_read32be(rt, sc + 44u);
            super_num = irix_guest_read32be(rt, sc + 48u);
            super_callbacks =
                irix_guest_read32be(rt, sc + IRIX_XT_CALLBACK_PRIVATE);
            if (super_callbacks && irix_guest_ptr(rt, super_callbacks, 4u))
                super_callback_num =
                    irix_guest_read32be(rt, super_callbacks);
        }
    }

    /*
     * Repair the resource indirection table only when this class still owns
     * its raw XtResource array.  Keep 'resources' pointing at that raw array
     * below so callback_private can be constructed from the same class-local
     * resource list, just like Xt's ConstructCallbackOffsets().
     */
    if (!resources_indirect && class_num) {
        if (!resources || class_num > 4096u ||
            !irix_guest_ptr(rt, resources,
                            (size_t)class_num * IRIX_XT_RESOURCE_SIZE))
            return;

        for (i = 0; i < class_num; ++i) {
            IrixGuestAddr res = resources + i * IRIX_XT_RESOURCE_SIZE;
            uint32_t off = irix_guest_read32be(rt, res + 16u);
            if ((int32_t)off >= 0)
                irix_guest_write32be(rt, res + 16u, ~off);
        }

        table = irix_guest_xt_compat_alloc(rt,
                                           (super_num + class_num) * 4u);
        if (!table)
            return;

        for (i = 0; i < super_num; ++i)
            irix_guest_write32be(rt, table + i * 4u,
                                 irix_guest_read32be(rt,
                                                    super_resources + i * 4u));

        new_num = super_num + class_num;
        next = super_num;
        for (i = 0; i < class_num; ++i) {
            IrixGuestAddr res = resources + i * IRIX_XT_RESOURCE_SIZE;
            uint32_t off = irix_guest_read32be(rt, res + 16u);
            uint32_t real_off = ~off;
            int replaced = 0;

            if (real_off < super_widget_size) {
                for (j = 0; j < super_num; ++j) {
                    IrixGuestAddr super_res =
                        irix_guest_read32be(rt, table + j * 4u);
                    if (super_res &&
                        irix_guest_read32be(rt, super_res + 16u) == off) {
                        irix_guest_write32be(rt, table + j * 4u, res);
                        --new_num;
                        replaced = 1;
                        break;
                    }
                }
            }
            if (!replaced)
                irix_guest_write32be(rt, table + next++ * 4u, res);
        }

        irix_guest_write32be(rt, wc + 44u, table);
        irix_guest_write32be(rt, wc + 48u, new_num);
    } else if (!class_num && !resources_indirect) {
        irix_guest_write32be(rt, wc + 44u, super_resources);
        irix_guest_write32be(rt, wc + 48u, super_num);
    }

    /*
     * Xt's ConstructCallbackOffsets() is a separate class-init step from
     * _XtResourceDependencies().  Our original repair supplied the latter's
     * resource table but forgot callback_private entirely, leaving +0x60 NULL
     * and causing the SGI callback walker to dereference address zero.
     *
     * Build the same table shape: [count, subclass callbacks..., inherited
     * callbacks...].  If the resource list was already repaired by an earlier
     * invocation, leave an existing callback table alone.
     */
    if (irix_guest_read32be(rt, wc + IRIX_XT_CALLBACK_PRIVATE))
        return;

    /*
     * If resources is already an XrmResourceList[], it is the merged class
     * resource table produced by Xt.  Scan that table directly: it already
     * contains inherited resources in the correct override order, so adding
     * superclass callbacks again would duplicate them.
     */
    if (resources_indirect && resources && class_num &&
        irix_guest_ptr(rt, resources, (size_t)class_num * 4u)) {
        for (i = 0; i < class_num; ++i) {
            IrixGuestAddr res =
                irix_guest_read32be(rt, resources + i * 4u);
            if (res && irix_guest_ptr(rt, res, IRIX_XT_RESOURCE_SIZE) &&
                (irix_guest_read32be(rt, res + 8u) == irix_xt_r_callback ||
                 (irix_xt_q_callback &&
                  irix_guest_read32be(rt, res + 8u) == irix_xt_q_callback)))
                ++own_callback_num;
        }

        callback_table =
            irix_guest_xt_compat_alloc(rt, (1u + own_callback_num) * 4u);
        if (!callback_table)
            return;

        irix_guest_write32be(rt, callback_table, own_callback_num);
        for (i = 0; i < class_num; ++i) {
            IrixGuestAddr res =
                irix_guest_read32be(rt, resources + i * 4u);
            if (res && irix_guest_ptr(rt, res, IRIX_XT_RESOURCE_SIZE) &&
                (irix_guest_read32be(rt, res + 8u) == irix_xt_r_callback ||
                 (irix_xt_q_callback &&
                  irix_guest_read32be(rt, res + 8u) == irix_xt_q_callback)))
                irix_guest_write32be(rt,
                    callback_table + callback_next++ * 4u, res);
        }

        irix_guest_write32be(rt, wc + IRIX_XT_CALLBACK_PRIVATE,
                             callback_table);
        return;
    }

    if (resources && class_num &&
        irix_guest_ptr(rt, resources,
                       (size_t)class_num * IRIX_XT_RESOURCE_SIZE)) {
        for (i = 0; i < class_num; ++i) {
            IrixGuestAddr res = resources + i * IRIX_XT_RESOURCE_SIZE;
            if ((irix_guest_read32be(rt, res + 8u) == irix_xt_r_callback ||
                 (irix_xt_q_callback &&
                  irix_guest_read32be(rt, res + 8u) == irix_xt_q_callback)))
                ++own_callback_num;
        }
    }

    callback_table =
        irix_guest_xt_compat_alloc(rt,
            (1u + own_callback_num + super_callback_num) * 4u);
    if (!callback_table)
        return;

    irix_guest_write32be(rt, callback_table,
                         own_callback_num + super_callback_num);

    if (resources && own_callback_num) {
        for (i = 0; i < class_num; ++i) {
            IrixGuestAddr res = resources + i * IRIX_XT_RESOURCE_SIZE;
            if ((irix_guest_read32be(rt, res + 8u) == irix_xt_r_callback ||
                 (irix_xt_q_callback &&
                  irix_guest_read32be(rt, res + 8u) == irix_xt_q_callback)))
                irix_guest_write32be(rt,
                    callback_table + callback_next++ * 4u, res);
        }
    }

    if (super_callbacks) {
        for (i = 0; i < super_callback_num; ++i)
            irix_guest_write32be(rt,
                callback_table + callback_next++ * 4u,
                irix_guest_read32be(rt,
                    super_callbacks + (i + 1u) * 4u));
    }

    irix_guest_write32be(rt, wc + IRIX_XT_CALLBACK_PRIVATE, callback_table);
}

void irix_guest_xt_repair_class_resources(IrixGuestRuntime *rt, IrixGuestAddr wc)
{
    irix_guest_xt_repair_class_resources_inner(rt, wc, 0u);
}

uint8_t irix_guest_read8(IrixGuestRuntime *rt, IrixGuestAddr addr)
{
    uint8_t *p = (uint8_t *)irix_guest_ptr(rt, addr, 1);
    if (!p) { memory_fault(rt, addr); return 0; }
    return *p;
}

uint16_t irix_guest_read16be(IrixGuestRuntime *rt, IrixGuestAddr addr)
{
    uint8_t *p = (uint8_t *)irix_guest_ptr(rt, addr, 2);
    if (!p) { memory_fault(rt, addr); return 0; }
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

uint32_t irix_guest_read32be(IrixGuestRuntime *rt, IrixGuestAddr addr)
{
    uint8_t *p;
    if (rt->errno_proxy_addr && addr == rt->errno_proxy_addr)
        return (uint32_t)(int32_t)errno;
    p = (uint8_t *)irix_guest_ptr(rt, addr, 4);
    if (!p) { memory_fault(rt, addr); return 0; }
    {
        uint32_t value = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
                         ((uint32_t)p[2] << 8) | (uint32_t)p[3];
        return value;
    }
}

uint64_t irix_guest_read64be(IrixGuestRuntime *rt, IrixGuestAddr addr)
{
    uint32_t hi = irix_guest_read32be(rt, addr);
    uint32_t lo = irix_guest_read32be(rt, addr + 4);
    return ((uint64_t)hi << 32) | lo;
}

static void rld_watch_write(IrixGuestRuntime *rt, IrixGuestAddr addr, size_t size)
{
    unsigned i;
    uint64_t end = (uint64_t)addr + size;
    for (i = 0; i < rt->rld_watch_depth; ++i) {
        IrixGuestRldWatchFrame *f = &rt->rld_watch[i];
        uint64_t slot_end = (uint64_t)f->slot + 8u;
        if (!f->warned && rt->current_pc != 0x0fb69b8cu &&
            (uint64_t)addr < slot_end && end > (uint64_t)f->slot) {
            uint64_t before = irix_guest_read64be(rt, f->slot);
            fprintf(stderr,
                    "IRIX rld saved-s0 overwrite: pc=%08x frame_sp=%08x slot=%08x expected=%016llx before=%016llx write_addr=%08x size=%zu\n",
                    (uint32_t)rt->current_pc, (uint32_t)f->sp,
                    (uint32_t)f->slot, (unsigned long long)f->expected_s0,
                    (unsigned long long)before, (uint32_t)addr, size);
            f->warned = 1;
        }
    }
}

void irix_guest_rld_watch_enter(IrixGuestRuntime *rt, const IrixGuestCpu *cpu)
{
    IrixGuestRldWatchFrame *f;
    if (rt->rld_watch_depth >= IRIX_GUEST_RLD_WATCH_MAX) {
        fprintf(stderr, "IRIX rld watch overflow: pc=%08x sp=%08x\n",
                (uint32_t)rt->current_pc, (uint32_t)cpu->gpr[29]);
        return;
    }
    f = &rt->rld_watch[rt->rld_watch_depth++];
    f->sp = (IrixGuestAddr)((uint32_t)cpu->gpr[29] - 0x30u);
    f->slot = f->sp + 0x10u;
    f->expected_s0 = cpu->gpr[16];
    f->warned = 0;
}

void irix_guest_rld_watch_restore(IrixGuestRuntime *rt, const IrixGuestCpu *cpu)
{
    IrixGuestAddr sp = (IrixGuestAddr)cpu->gpr[29];
    unsigned i;
    for (i = rt->rld_watch_depth; i > 0; --i) {
        IrixGuestRldWatchFrame *f = &rt->rld_watch[i - 1u];
        uint64_t actual;
        if (f->sp != sp)
            continue;
        actual = irix_guest_read64be(rt, f->slot);
        if (actual != f->expected_s0)
            fprintf(stderr,
                    "IRIX rld saved-s0 CORRUPT: pc=%08x frame_sp=%08x slot=%08x expected=%016llx actual=%016llx\n",
                    (uint32_t)rt->current_pc, (uint32_t)f->sp,
                    (uint32_t)f->slot, (unsigned long long)f->expected_s0,
                    (unsigned long long)actual);
        return;
    }
}

void irix_guest_rld_watch_leave(IrixGuestRuntime *rt, const IrixGuestCpu *cpu)
{
    IrixGuestAddr sp = (IrixGuestAddr)cpu->gpr[29];
    unsigned i;
    for (i = rt->rld_watch_depth; i > 0; --i) {
        unsigned index = i - 1u;
        IrixGuestRldWatchFrame *f = &rt->rld_watch[index];
        if (f->sp != sp)
            continue;
        if (cpu->gpr[16] != f->expected_s0)
            fprintf(stderr,
                    "IRIX rld s0 restore mismatch: pc=%08x frame_sp=%08x expected=%016llx restored=%016llx\n",
                    (uint32_t)rt->current_pc, (uint32_t)f->sp,
                    (unsigned long long)f->expected_s0,
                    (unsigned long long)cpu->gpr[16]);
        if (index + 1u < rt->rld_watch_depth)
            memmove(&rt->rld_watch[index], &rt->rld_watch[index + 1u],
                    (rt->rld_watch_depth - index - 1u) * sizeof(rt->rld_watch[0]));
        --rt->rld_watch_depth;
        return;
    }

    fprintf(stderr, "IRIX rld unmatched epilogue: pc=%08x actual_sp=%08x active=",
            (uint32_t)rt->current_pc, (uint32_t)sp);
    if (!rt->rld_watch_depth) {
        fprintf(stderr, "none");
    } else {
        for (i = 0; i < rt->rld_watch_depth; ++i)
            fprintf(stderr, "%s%08x", i ? "," : "", (uint32_t)rt->rld_watch[i].sp);
    }
    fputc('\n', stderr);
}

void irix_guest_write8(IrixGuestRuntime *rt, IrixGuestAddr addr, uint8_t value)
{
    uint8_t *p;
    rld_watch_write(rt, addr, 1u);
    if (addr >= 0x0fb5291cu && addr < 0x0fb52920u)
        fprintf(stderr,
                "IRIX libc GOT byte watch: pc=0x%08x addr=0x%08x value=%02x\n",
                rt->current_pc, addr, value);
    p = (uint8_t *)irix_guest_ptr(rt, addr, 1);
    if (!p) { memory_fault(rt, addr); return; }
    *p = value;
}

void irix_guest_write16be(IrixGuestRuntime *rt, IrixGuestAddr addr, uint16_t value)
{
    uint8_t *p;
    rld_watch_write(rt, addr, 2u);
    if ((uint64_t)addr < UINT64_C(0x0fb52920) &&
        (uint64_t)addr + 2u > UINT64_C(0x0fb5291c))
        fprintf(stderr,
                "IRIX libc GOT half watch: pc=0x%08x addr=0x%08x value=%04x\n",
                rt->current_pc, addr, value);
    p = (uint8_t *)irix_guest_ptr(rt, addr, 2);
    if (!p) { memory_fault(rt, addr); return; }
    p[0] = (uint8_t)(value >> 8); p[1] = (uint8_t)value;
}

void irix_guest_write32be(IrixGuestRuntime *rt, IrixGuestAddr addr, uint32_t value)
{
    uint8_t *p;
    rld_watch_write(rt, addr, 4u);
    if (addr == 0x0fb5291cu) {
        uint32_t old_value = 0;
        uint8_t *oldp = (uint8_t *)irix_guest_ptr(rt, addr, 4u);
        if (oldp)
            old_value = ((uint32_t)oldp[0] << 24) |
                        ((uint32_t)oldp[1] << 16) |
                        ((uint32_t)oldp[2] << 8) |
                        (uint32_t)oldp[3];
        fprintf(stderr,
                "IRIX libc GOT watch: pc=0x%08x slot=0x%08x %08x -> %08x\n",
                rt->current_pc, addr, old_value, value);
    }
    if (rt->errno_proxy_addr && addr == rt->errno_proxy_addr) {
        errno = (int)(int32_t)value;
        return;
    }
    p = (uint8_t *)irix_guest_ptr(rt, addr, 4);
    if (!p) { memory_fault(rt, addr); return; }
    p[0] = (uint8_t)(value >> 24); p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8); p[3] = (uint8_t)value;
}

void irix_guest_write64be(IrixGuestRuntime *rt, IrixGuestAddr addr, uint64_t value)
{
    irix_guest_write32be(rt, addr, (uint32_t)(value >> 32));
    irix_guest_write32be(rt, addr + 4, (uint32_t)value);
}

void irix_guest_set_errno_proxy(IrixGuestRuntime *rt, IrixGuestAddr addr)
{
    rt->errno_proxy_addr = addr;
}

void irix_guest_set_rootfs(IrixGuestRuntime *rt, const char *rootfs)
{
    rt->rootfs = rootfs;
}

float irix_guest_fpr_get_s(const IrixGuestCpu *cpu, unsigned reg)
{
    union { uint32_t u; float f; } v;
    v.u = (uint32_t)cpu->fpr[reg & 31u];
    return v.f;
}

double irix_guest_fpr_get_d(const IrixGuestCpu *cpu, unsigned reg)
{
    union { uint64_t u; double d; } v;
    v.u = cpu->fpr[reg & 31u];
    return v.d;
}

void irix_guest_fpr_set_s(IrixGuestCpu *cpu, unsigned reg, float value)
{
    union { uint32_t u; float f; } v;
    v.f = value;
    cpu->fpr[reg & 31u] = (cpu->fpr[reg & 31u] & UINT64_C(0xffffffff00000000)) | v.u;
}

void irix_guest_fpr_set_d(IrixGuestCpu *cpu, unsigned reg, double value)
{
    union { uint64_t u; double d; } v;
    v.d = value;
    cpu->fpr[reg & 31u] = v.u;
}

static int irix_guest_register_function_range_flags(
    IrixGuestRuntime *rt, IrixGuestAddr address, uint32_t size,
    IrixGuestFunction function, const char *name, uint32_t flags)
{
    IrixGuestFunctionEntry *entry;
    if (!function || rt->function_count >= IRIX_GUEST_MAX_FUNCTIONS)
        return -1;
    entry = &rt->functions[rt->function_count++];
    entry->address = address;
    entry->size = size;
    entry->function = function;
    entry->name = name;
    entry->flags = flags;
    {
        uint32_t slot = (address >> 2) & (IRIX_GUEST_FUNCTION_CACHE_SIZE - 1u);
        uint32_t start = slot;
        while (rt->function_cache[slot] &&
               rt->function_cache[slot]->address != address) {
            slot = (slot + 1u) & (IRIX_GUEST_FUNCTION_CACHE_SIZE - 1u);
            if (slot == start)
                break;
        }
        /*
         * Preserve the registry's historical first-registration-wins
         * semantics for exact duplicate addresses. Native overrides are
         * intentionally registered before guest translations.
         */
        if (!rt->function_cache[slot])
            rt->function_cache[slot] = entry;
    }
    return 0;
}

int irix_guest_register_function_range(IrixGuestRuntime *rt, IrixGuestAddr address,
                                       uint32_t size, IrixGuestFunction function,
                                       const char *name)
{
    return irix_guest_register_function_range_flags(
        rt, address, size, function, name, IRIX_GUEST_FUNCTION_NATIVE);
}

int irix_guest_register_function(IrixGuestRuntime *rt, IrixGuestAddr address,
                                 IrixGuestFunction function, const char *name)
{
    return irix_guest_register_function_range(rt, address, 0u, function, name);
}

int irix_guest_register_recompiled_function_range(
    IrixGuestRuntime *rt, IrixGuestAddr address, uint32_t size,
    IrixGuestFunction function, const char *name)
{
    return irix_guest_register_function_range_flags(
        rt, address, size, function, name, IRIX_GUEST_FUNCTION_RECOMPILED);
}

int irix_guest_register_recompiled_function(
    IrixGuestRuntime *rt, IrixGuestAddr address,
    IrixGuestFunction function, const char *name)
{
    return irix_guest_register_recompiled_function_range(
        rt, address, 0u, function, name);
}

IrixGuestFunctionEntry *irix_guest_find_function(IrixGuestRuntime *rt, IrixGuestAddr address)
{
    IrixGuestFunctionEntry *containing = 0;
    uint32_t slot = (address >> 2) & (IRIX_GUEST_FUNCTION_CACHE_SIZE - 1u);
    uint32_t start = slot;
    size_t i;

    /* Exact callable entries dominate normal guest-call traffic.  Resolve
       those through the registration-time address hash instead of linearly
       scanning the entire translated ecosystem on every JAL/JALR. */
    while (rt->function_cache[slot]) {
        if (rt->function_cache[slot]->address == address)
            return rt->function_cache[slot];
        slot = (slot + 1u) & (IRIX_GUEST_FUNCTION_CACHE_SIZE - 1u);
        if (slot == start)
            break;
    }

    /* Interior-PC dispatch is recovery/fallback behavior.  Preserve the
       smallest-containing-range rule for those uncommon transfers. */
    for (i = 0; i < rt->function_count; ++i) {
        IrixGuestFunctionEntry *entry = &rt->functions[i];
        if (entry->size && address > entry->address &&
            (uint64_t)address < (uint64_t)entry->address + entry->size) {
            if (!containing || entry->size < containing->size)
                containing = entry;
        }
    }
    return containing;
}

void irix_guest_trap(IrixGuestRuntime *rt, IrixGuestCpu *cpu, uint32_t code)
{
    rt->trapped = 1;
    rt->trap_pc = cpu->pc;
    rt->trap_code = code;
}

static uint32_t irix_syscall_dev(dev_t d)
{
    return ((uint32_t)major(d) << 18) | ((uint32_t)minor(d) & 0x3ffffu);
}

/*
 * The Linux spty module uses dynamic host device numbers.  IRIX libc getpty
 * identifies PTC/PTS endpoints from st_rdev, so fstat must expose the
 * historical IRIX device families instead of the Linux substrate numbers.
 */
typedef struct IrixSptyHostId {
    uint32_t index;
    uint32_t side;
} IrixSptyHostId;

#define IRIX_SPTY_HOST_GET_ID _IOR('I', 0xf0, IrixSptyHostId)
#define IRIX_SPTY_SIDE_MASTER 0u
#define IRIX_SPTY_SIDE_SLAVE  1u

static int irix_spty_path_index(const char *path, uint32_t *index)
{
    const char *p;
    char *end;
    unsigned long n;

    if (!path || !index)
        return 0;

    if (strncmp(path, "/dev/pts/", 9u) == 0) {
        p = path + 9u;
        if (!*p)
            return 0;
        errno = 0;
        n = strtoul(p, &end, 10);
        if (errno || *end || n >= 1000u)
            return 0;
        *index = (uint32_t)n;
        return 1;
    }

    if (strncmp(path, "/dev/tty", 8u) == 0 &&
        path[8] >= 'q' && path[8] <= 'z') {
        unsigned family = (unsigned)(path[8] - 'q');
        p = path + 9u;
        if (!*p)
            return 0;
        errno = 0;
        n = strtoul(p, &end, 10);
        if (errno || *end || n >= 100u)
            return 0;
        *index = family * 100u + (uint32_t)n;
        return *index < 1000u;
    }

    return 0;
}

static uint32_t irix_spty_rdev(uint32_t index, uint32_t side)
{
    static const uint32_t ptc_major[5] = { 14u, 104u, 106u, 108u, 110u };
    static const uint32_t pts_major[5] = { 15u, 105u, 107u, 109u, 111u };
    uint32_t family = index / 200u;
    uint32_t minor_no = index % 200u;
    uint32_t major_no = side == IRIX_SPTY_SIDE_MASTER ?
                        ptc_major[family] : pts_major[family];
    return (major_no << 18) | minor_no;
}

static uint32_t irix_syscall_rdev(const struct stat *st, int fd,
                                  const char *guest_path)
{
    IrixSptyHostId id;
    uint32_t path_index;

    if (fd >= 0 && ioctl(fd, IRIX_SPTY_HOST_GET_ID, &id) == 0 &&
        id.index < 1000u &&
        (id.side == IRIX_SPTY_SIDE_MASTER ||
         id.side == IRIX_SPTY_SIDE_SLAVE)) {
        return irix_spty_rdev(id.index, id.side);
    }

    if (irix_spty_path_index(guest_path, &path_index))
        return irix_spty_rdev(path_index, IRIX_SPTY_SIDE_SLAVE);

    return irix_syscall_dev(st->st_rdev);
}

static int write_irix_xstat32(IrixGuestRuntime *rt, IrixGuestAddr out,
                              const struct stat *st, int fd, const char *guest_path)
{
    unsigned char *p;
    if (!out || !st) {
        errno = EFAULT;
        return -1;
    }
    p = (unsigned char *)irix_guest_ptr(rt, out, 136u);
    if (!p) {
        errno = EFAULT;
        return -1;
    }
    memset(p, 0, 136u);
    irix_guest_write32be(rt, out + 0u, irix_syscall_dev(st->st_dev));
    irix_guest_write32be(rt, out + 16u, (uint32_t)st->st_ino);
    irix_guest_write32be(rt, out + 20u, (uint32_t)st->st_mode);
    irix_guest_write32be(rt, out + 24u, (uint32_t)st->st_nlink);
    irix_guest_write32be(rt, out + 28u, (uint32_t)st->st_uid);
    irix_guest_write32be(rt, out + 32u, (uint32_t)st->st_gid);
    irix_guest_write32be(rt, out + 36u, irix_syscall_rdev(st, fd, guest_path));
    irix_guest_write32be(rt, out + 48u, (uint32_t)st->st_size);
#if defined(__APPLE__)
    irix_guest_write32be(rt, out + 56u, (uint32_t)st->st_atimespec.tv_sec);
    irix_guest_write32be(rt, out + 60u, (uint32_t)st->st_atimespec.tv_nsec);
    irix_guest_write32be(rt, out + 64u, (uint32_t)st->st_mtimespec.tv_sec);
    irix_guest_write32be(rt, out + 68u, (uint32_t)st->st_mtimespec.tv_nsec);
    irix_guest_write32be(rt, out + 72u, (uint32_t)st->st_ctimespec.tv_sec);
    irix_guest_write32be(rt, out + 76u, (uint32_t)st->st_ctimespec.tv_nsec);
#else
    irix_guest_write32be(rt, out + 56u, (uint32_t)st->st_atim.tv_sec);
    irix_guest_write32be(rt, out + 60u, (uint32_t)st->st_atim.tv_nsec);
    irix_guest_write32be(rt, out + 64u, (uint32_t)st->st_mtim.tv_sec);
    irix_guest_write32be(rt, out + 68u, (uint32_t)st->st_mtim.tv_nsec);
    irix_guest_write32be(rt, out + 72u, (uint32_t)st->st_ctim.tv_sec);
    irix_guest_write32be(rt, out + 76u, (uint32_t)st->st_ctim.tv_nsec);
#endif
    irix_guest_write32be(rt, out + 80u, (uint32_t)st->st_blksize);
    irix_guest_write32be(rt, out + 84u, (uint32_t)st->st_blocks);
    memcpy(p + 88u, "efs", 4u);
    return rt->trapped ? -1 : 0;
}

static int write_irix_xstat64(IrixGuestRuntime *rt, IrixGuestAddr out,
                              const struct stat *st, int fd,
                              const char *guest_path)
{
    unsigned char *p;
    if (!out || !st) {
        errno = EFAULT;
        return -1;
    }
    p = (unsigned char *)irix_guest_ptr(rt, out, 152u);
    if (!p) {
        errno = EFAULT;
        return -1;
    }
    memset(p, 0, 152u);
    irix_guest_write32be(rt, out + 0u, irix_syscall_dev(st->st_dev));
    irix_guest_write64be(rt, out + 16u, (uint64_t)st->st_ino);
    irix_guest_write32be(rt, out + 24u, (uint32_t)st->st_mode);
    irix_guest_write32be(rt, out + 28u, (uint32_t)st->st_nlink);
    irix_guest_write32be(rt, out + 32u, (uint32_t)st->st_uid);
    irix_guest_write32be(rt, out + 36u, (uint32_t)st->st_gid);
    irix_guest_write32be(rt, out + 40u, irix_syscall_rdev(st, fd, guest_path));
    irix_guest_write64be(rt, out + 56u, (uint64_t)(int64_t)st->st_size);
#if defined(__APPLE__)
    irix_guest_write32be(rt, out + 68u, (uint32_t)st->st_atimespec.tv_sec);
    irix_guest_write32be(rt, out + 72u, (uint32_t)st->st_atimespec.tv_nsec);
    irix_guest_write32be(rt, out + 76u, (uint32_t)st->st_mtimespec.tv_sec);
    irix_guest_write32be(rt, out + 80u, (uint32_t)st->st_mtimespec.tv_nsec);
    irix_guest_write32be(rt, out + 84u, (uint32_t)st->st_ctimespec.tv_sec);
    irix_guest_write32be(rt, out + 88u, (uint32_t)st->st_ctimespec.tv_nsec);
#else
    irix_guest_write32be(rt, out + 68u, (uint32_t)st->st_atim.tv_sec);
    irix_guest_write32be(rt, out + 72u, (uint32_t)st->st_atim.tv_nsec);
    irix_guest_write32be(rt, out + 76u, (uint32_t)st->st_mtim.tv_sec);
    irix_guest_write32be(rt, out + 80u, (uint32_t)st->st_mtim.tv_nsec);
    irix_guest_write32be(rt, out + 84u, (uint32_t)st->st_ctim.tv_sec);
    irix_guest_write32be(rt, out + 88u, (uint32_t)st->st_ctim.tv_nsec);
#endif
    irix_guest_write32be(rt, out + 92u, (uint32_t)st->st_blksize);
    irix_guest_write64be(rt, out + 96u, (uint64_t)(int64_t)st->st_blocks);
    return rt->trapped ? -1 : 0;
}

static int write_irix_xstat(IrixGuestRuntime *rt, uint32_t version,
                            IrixGuestAddr out, const struct stat *st, int fd,
                            const char *guest_path)
{
    if (version == 2u)
        return write_irix_xstat32(rt, out, st, fd, guest_path);
    if (version == 3u)
        return write_irix_xstat64(rt, out, st, fd, guest_path);
    errno = EINVAL;
    return -1;
}

static int irix_is_guest_system_path(const char *guest)
{
    return strncmp(guest, "/usr/lib32/", 11u) == 0 ||
           strncmp(guest, "/lib32/", 7u) == 0 ||
           strncmp(guest, "/usr/lib/", 9u) == 0 ||
           strncmp(guest, "/lib/", 5u) == 0 ||
           strncmp(guest, "/usr/gfx/", 9u) == 0;
}

static const char *irix_host_path(IrixGuestRuntime *rt, const char *guest,
                                  char *buf, size_t bufsz)
{
    struct stat st;
    int n;

    if (!guest)
        return 0;

    /*
     * IRIX exposes each slave in both historical /dev/tty[q-z]N and SVR4
     * /dev/pts/N form.  Linux devpts owns the latter namespace, so point the
     * guest SVR4 spelling at our IRIX slave character node.
     */
    if (strncmp(guest, "/dev/pts/", 9u) == 0) {
        uint32_t index;
        if (irix_spty_path_index(guest, &index)) {
            n = snprintf(buf, bufsz, "/dev/tty%c%u",
                         "qrstuvwxyz"[index / 100u], index % 100u);
            if (n <= 0 || (size_t)n >= bufsz) {
                errno = ENAMETOOLONG;
                return 0;
            }
            return buf;
        }
    }

    if (guest[0] != '/' || !rt->rootfs || !rt->rootfs[0])
        return guest;

    n = snprintf(buf, bufsz, "%s%s", rt->rootfs, guest);
    if (n <= 0 || (size_t)n >= bufsz) {
        errno = ENAMETOOLONG;
        return 0;
    }

    if (stat(buf, &st) == 0)
        return buf;

    /*
     * The extracted install tree contains the ISA-specific N32 runtime
     * libraries but not all of the canonical top-level aliases that exist on
     * an installed IRIX system.  Our emulated machine is an R10000/MIPS-IV
     * profile, so resolve a missing direct /usr/lib32/<soname> through the
     * mips4 directory first, with mips3 as the compatible fallback.
     *
     * Keep this deliberately narrow: do not recursively search arbitrary
     * subdirectories, and do not apply it to internal/ libraries.
     */
    if (strncmp(guest, "/usr/lib32/", 11u) == 0) {
        const char *name = guest + 11u;
        if (*name && !strchr(name, '/')) {
            n = snprintf(buf, bufsz, "%s/usr/lib32/mips4/%s",
                         rt->rootfs, name);
            if (n > 0 && (size_t)n < bufsz && stat(buf, &st) == 0) {
                fprintf(stderr,
                        "IRIX path alias: %s -> /usr/lib32/mips4/%s\n",
                        guest, name);
                return buf;
            }

            n = snprintf(buf, bufsz, "%s/usr/lib32/mips3/%s",
                         rt->rootfs, name);
            if (n > 0 && (size_t)n < bufsz && stat(buf, &st) == 0) {
                fprintf(stderr,
                        "IRIX path alias: %s -> /usr/lib32/mips3/%s\n",
                        guest, name);
                return buf;
            }
        }
    }

    /*
     * Never let an unresolved IRIX system-library path escape into the host
     * filesystem.  A host /usr/lib32/libm.so may be x86 and can be opened
     * successfully, only for the guest rld to reject it later.
     *
     * Ordinary device/temp paths still intentionally fall back to the host.
     */
    if (irix_is_guest_system_path(guest)) {
        n = snprintf(buf, bufsz, "%s%s", rt->rootfs, guest);
        if (n <= 0 || (size_t)n >= bufsz) {
            errno = ENAMETOOLONG;
            return 0;
        }
        return buf;
    }

    return guest;
}

static int irix_open_flags(uint32_t f)
{
    int out = 0;
    switch (f & 3u) {
    case 1u: out |= O_WRONLY; break;
    case 2u: out |= O_RDWR; break;
    default: out |= O_RDONLY; break;
    }
    if (f & 0x0004u) out |= O_NONBLOCK; /* O_NDELAY */
    if (f & 0x0008u) out |= O_APPEND;
#ifdef O_SYNC
    if (f & 0x0010u) out |= O_SYNC;
#endif
#ifdef O_ASYNC
    if (f & 0x0040u) out |= O_ASYNC;
#endif
    if (f & 0x0080u) out |= O_NONBLOCK;
    if (f & 0x0100u) out |= O_CREAT;
    if (f & 0x0200u) out |= O_TRUNC;
    if (f & 0x0400u) out |= O_EXCL;
#ifdef O_NOCTTY
    if (f & 0x0800u) out |= O_NOCTTY;
#endif
#ifdef O_DIRECT
    if (f & 0x8000u) out |= O_DIRECT;
#endif
    return out;
}

static void syscall_result(IrixGuestCpu *cpu, int64_t value)
{
    if (value < 0) {
        cpu->gpr[2] = (uint64_t)(int64_t)errno;
        cpu->gpr[7] = 1;
    } else {
        cpu->gpr[2] = (uint64_t)value;
        cpu->gpr[7] = 0;
    }
}

void irix_guest_syscall(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    uint32_t number = (uint32_t)cpu->gpr[2];
    int64_t result;

    errno = 0;
    switch (number) {
    case SYS_syscall: { /* syscall(indirect_number, ...) */
        uint32_t indirect = (uint32_t)cpu->gpr[4];
        unsigned i;
        if (indirect == 1000u) {
            errno = EINVAL;
            syscall_result(cpu, -1);
            return;
        }
        /*
         * IRIX libc uses syscall(2) as an indirect entry for calls such as
         * sysmp().  The real syscall number is in a0 and its arguments begin
         * in a1, so shift the N32 argument registers down one slot and reuse
         * the normal dispatcher.
         */
        for (i = 4u; i < 11u; ++i)
            cpu->gpr[i] = cpu->gpr[i + 1u];
        cpu->gpr[2] = indirect;
        irix_guest_syscall(rt, cpu);
        return;
    }
    case SYS_exit: { /* exit */
        /*
         * IRIX _exit(2) never returns.  Use a distinguished runtime trap to
         * unwind translated call frames; AppRunner converts it back into the
         * guest process exit status instead of reporting it as a fault.
         */
        uint32_t status = (uint32_t)cpu->gpr[4] & 0xffu;
        cpu->gpr[2] = status;
        cpu->gpr[7] = 0;
        rt->fault_addr = status;
        irix_guest_trap(rt, cpu, 0xeeee23ffu);
        return;
    }
    case SYS_read: { /* read */
        IrixGuestAddr addr = (IrixGuestAddr)(uint32_t)cpu->gpr[5];
        size_t count = (size_t)(uint32_t)cpu->gpr[6];
        void *p = irix_guest_ptr(rt, addr, count);
        if (!p && count) {
            errno = EFAULT;
            syscall_result(cpu, -1);
            return;
        }
        result = (int64_t)read((int)(int32_t)cpu->gpr[4], p, count);
        syscall_result(cpu, result);
        return;
    }
    case SYS_write: { /* write */
        IrixGuestAddr addr = (IrixGuestAddr)(uint32_t)cpu->gpr[5];
        size_t count = (size_t)(uint32_t)cpu->gpr[6];
        const void *p = irix_guest_ptr(rt, addr, count);
        if (!p && count) {
            errno = EFAULT;
            syscall_result(cpu, -1);
            return;
        }
        result = (int64_t)write((int)(int32_t)cpu->gpr[4], p, count);
        syscall_result(cpu, result);
        return;
    }
    case SYS_open: { /* open */
        IrixGuestAddr path_addr = (IrixGuestAddr)(uint32_t)cpu->gpr[4];
        const char *guest_path = (const char *)irix_guest_ptr(rt, path_addr, 1u);
        char host_path[PATH_MAX];
        const char *path;
        uint32_t flags = (uint32_t)cpu->gpr[5];
        mode_t mode = (mode_t)(uint32_t)cpu->gpr[6];
        if (!guest_path) {
            errno = EFAULT;
            syscall_result(cpu, -1);
            return;
        }
        path = irix_host_path(rt, guest_path, host_path, sizeof(host_path));
        if (!path) {
            syscall_result(cpu, -1);
            return;
        }
        result = (int64_t)open(path, irix_open_flags(flags), mode);
        fprintf(stderr, "IRIX open: %s -> %s flags=0x%x = %lld%s%s\n",
                guest_path, path, flags, (long long)result,
                result < 0 ? " errno=" : "",
                result < 0 ? strerror(errno) : "");
        syscall_result(cpu, result);
        return;
    }
    case SYS_close: /* close */
        result = (int64_t)close((int)(int32_t)cpu->gpr[4]);
        syscall_result(cpu, result);
        return;
    case SYS_ioctl: { /* ioctl(fd, cmd, arg) */
        int fd = (int)(int32_t)cpu->gpr[4];
        uint32_t cmd = (uint32_t)cpu->gpr[5];
        IrixGuestAddr arg_addr = (IrixGuestAddr)(uint32_t)cpu->gpr[6];

        /*
         * IRIX STREAMS I_STR is 0x5308.  Its N32 strioctl contains a
         * 32-bit guest pointer, so marshal the wrapper and pointed-to data
         * instead of handing the guest address to the Linux kernel.
         */
        if (cmd == 0x5308u) {
            unsigned char host_si[16];
            unsigned char *payload = NULL;
            uint32_t ic_cmd;
            int32_t ic_timout;
            int32_t ic_len;
            uint32_t ic_dp;

            if (!arg_addr || !irix_guest_ptr(rt, arg_addr, 16u)) {
                errno = EFAULT;
                syscall_result(cpu, -1);
                return;
            }
            ic_cmd = irix_guest_read32be(rt, arg_addr + 0u);
            ic_timout = (int32_t)irix_guest_read32be(rt, arg_addr + 4u);
            ic_len = (int32_t)irix_guest_read32be(rt, arg_addr + 8u);
            ic_dp = irix_guest_read32be(rt, arg_addr + 12u);
            if (ic_len < 0 || ic_len > 65536) {
                errno = EINVAL;
                syscall_result(cpu, -1);
                return;
            }
            if (ic_len && ic_dp) {
                payload = (unsigned char *)irix_guest_ptr(
                    rt, (IrixGuestAddr)ic_dp, (size_t)ic_len);
                if (!payload) {
                    errno = EFAULT;
                    syscall_result(cpu, -1);
                    return;
                }
            }

            /*
             * The module deliberately keeps the 16-byte N32 strioctl wire
             * shape.  ic_dp is host-private at this boundary, so require it
             * to fit in 32 bits rather than silently truncating a pointer.
             * The current getpty controls (UNLKPT/SVR4SOPEN) carry no data
             * and therefore use a zero pointer.
             */
            if ((uintptr_t)payload > UINT32_MAX) {
                errno = EOVERFLOW;
                syscall_result(cpu, -1);
                return;
            }
            memcpy(host_si + 0, &ic_cmd, 4u);
            memcpy(host_si + 4, &ic_timout, 4u);
            memcpy(host_si + 8, &ic_len, 4u);
            {
                uint32_t hp = (uint32_t)(uintptr_t)payload;
                memcpy(host_si + 12, &hp, 4u);
            }
            result = (int64_t)ioctl(fd, cmd, host_si);
            syscall_result(cpu, result);
            return;
        }

        /*
         * Scalar/no-argument IRIX ioctls can be forwarded unchanged to the
         * IRIX spty module.  Pointer-bearing commands get explicit ABI
         * marshalling as they are added.
         */
        result = (int64_t)ioctl(fd, cmd, (unsigned long)(uint32_t)arg_addr);
        syscall_result(cpu, result);
        return;
    }
    case SYS_brk: { /* break */
        IrixGuestAddr requested = (IrixGuestAddr)(uint32_t)cpu->gpr[4];
        const uint32_t page_size = 16384u;
        IrixGuestAddr old_break = rt->program_break;
        IrixGuestAddr old_page;
        IrixGuestAddr new_page;

        /*
         * IRIX syscall 17 is the kernel break(2) primitive used by libc's
         * brk/sbrk implementation.  Unlike Linux's raw brk syscall, IRIX
         * returns 0 on success and -1/ENOMEM on failure.
         *
         * Keep the logical break byte-exact, but back newly exposed heap with
         * zero-filled guest pages.  Shrinking only moves the logical break for
         * now: retaining the backing avoids fragmenting the simple guest
         * segment table and is harmless until that address range is reused.
         */
        if (!old_break) {
            /*
             * The first break request is not merely initialization: libc may
             * immediately use memory below that requested break.  Back the
             * complete heap extent from the end of the main executable's
             * existing data mapping up through the requested break.
             *
             * Find the highest mapped address below the requested break.  In
             * the normal desktop image this is the executable's data/bss end;
             * page-align it down so a partially mapped final page is covered.
             */
            IrixGuestAddr heap_base = 0;
            IrixGuestAddr heap_page;
            IrixGuestAddr request_page;
            size_t i;

            for (i = 0; i < rt->memory.count; ++i) {
                IrixGuestSegment *seg = &rt->memory.segments[i];
                uint64_t end = (uint64_t)seg->guest_base + seg->size;
                if (end <= requested && end > heap_base)
                    heap_base = (IrixGuestAddr)end;
            }
            if (!heap_base) {
                errno = ENOMEM;
                syscall_result(cpu, -1);
                return;
            }

            heap_page = heap_base & ~(page_size - 1u);
            request_page = (requested + page_size - 1u) & ~(page_size - 1u);
            if (request_page > heap_page) {
                uint32_t grow = (uint32_t)(request_page - heap_page);
                if (!irix_guest_ptr(rt, heap_page, grow) &&
                    irix_guest_map(rt, heap_page, grow, 3u) != 0) {
                    /*
                     * A partial overlap is common at the executable's final
                     * data page.  Start immediately after that mapping and
                     * back only the still-unmapped tail.
                     */
                    IrixGuestAddr tail = heap_base;
                    if (tail < request_page &&
                        !irix_guest_ptr(rt, tail,
                                        (uint32_t)(request_page - tail)) &&
                        irix_guest_map(rt, tail,
                                       (uint32_t)(request_page - tail),
                                       3u) != 0) {
                        errno = ENOMEM;
                        syscall_result(cpu, -1);
                        return;
                    }
                }
            }

            rt->program_break = requested;
            fprintf(stderr, "IRIX break: init heap 0x%08x -> 0x%08x\n",
                    heap_base, requested);
            syscall_result(cpu, 0);
            return;
        }
        if (requested < 0x10000u) {
            errno = ENOMEM;
            syscall_result(cpu, -1);
            return;
        }

        old_page = (old_break + page_size - 1u) & ~(page_size - 1u);
        new_page = (requested + page_size - 1u) & ~(page_size - 1u);
        if (new_page > old_page) {
            uint32_t grow = (uint32_t)(new_page - old_page);
            if (irix_guest_map(rt, old_page, grow, 3u) != 0) {
                /*
                 * The requested range may already be backed by a loader
                 * segment.  Accept it only when the complete growth range is
                 * already guest-addressable.
                 */
                if (!irix_guest_ptr(rt, old_page, grow)) {
                    errno = ENOMEM;
                    syscall_result(cpu, -1);
                    return;
                }
            }
        }
        rt->program_break = requested;
        fprintf(stderr, "IRIX break: 0x%08x -> 0x%08x\n",
                old_break, requested);
        syscall_result(cpu, 0);
        return;
    }
    case SYS_ksigaction: { /* sigaction(sig, act, oact, sigtramp) */
        int sig = (int)(int32_t)cpu->gpr[4];
        IrixGuestAddr act = (IrixGuestAddr)(uint32_t)cpu->gpr[5];
        IrixGuestAddr oact = (IrixGuestAddr)(uint32_t)cpu->gpr[6];
        uint32_t sigtramp = (uint32_t)cpu->gpr[7];
        IrixKernelSigAction next;

        /*
         * SGI scalls.c: IRIX5/N32 sigaction copies irix5_sigaction_t in/out,
         * validates 0 < sig < NSIG, and stores the fourth syscall argument
         * separately as the signal trampoline. The user ABI structure is:
         * flags, 32-bit handler, four-word sigset, two reserved words.
         */
        if (sig <= 0 || (unsigned)sig >= IRIX_NSIG) {
            errno = EINVAL;
            syscall_result(cpu, -1);
            return;
        }
        if (oact && write_irix_sigaction_n32(rt, oact,
                                             &g_kernel_sigactions[sig]) != 0) {
            errno = EFAULT;
            syscall_result(cpu, -1);
            return;
        }
        if (act) {
            if (read_irix_sigaction_n32(rt, act, &next) != 0) {
                errno = EFAULT;
                syscall_result(cpu, -1);
                return;
            }
            next.sigtramp = sigtramp;
            g_kernel_sigactions[sig] = next;
        }
        syscall_result(cpu, 0);
        return;
    }
    case SYS_gethostname: { /* gethostname(char *name, size_t len) */
        IrixGuestAddr name_addr = (IrixGuestAddr)(uint32_t)cpu->gpr[4];
        size_t len = (size_t)(uint32_t)cpu->gpr[5];
        char *name;

        if (!len) {
            errno = EINVAL;
            syscall_result(cpu, -1);
            return;
        }
        name = (char *)irix_guest_ptr(rt, name_addr, len);
        if (!name) {
            errno = EFAULT;
            syscall_result(cpu, -1);
            return;
        }
        syscall_result(cpu, (int64_t)gethostname(name, len));
        return;
    }
    case SYS_chdir: {
        const char *path = (const char *)irix_guest_ptr(rt, (IrixGuestAddr)(uint32_t)cpu->gpr[4], 1u);
        if (!path) { errno = EFAULT; syscall_result(cpu, -1); return; }
        syscall_result(cpu, (int64_t)chdir(path));
        return;
    }
    case SYS_chmod: {
        const char *guest_path = (const char *)irix_guest_ptr(rt, (IrixGuestAddr)(uint32_t)cpu->gpr[4], 1u);
        char host_path[PATH_MAX];
        const char *path;
        if (!guest_path) { errno = EFAULT; syscall_result(cpu, -1); return; }
        path = irix_host_path(rt, guest_path, host_path, sizeof(host_path));
        if (!path) { syscall_result(cpu, -1); return; }
        syscall_result(cpu, (int64_t)chmod(path, (mode_t)(uint32_t)cpu->gpr[5]));
        return;
    }
    case SYS_chown: {
        const char *guest_path = (const char *)irix_guest_ptr(rt, (IrixGuestAddr)(uint32_t)cpu->gpr[4], 1u);
        char host_path[PATH_MAX];
        const char *path;
        if (!guest_path) { errno = EFAULT; syscall_result(cpu, -1); return; }
        path = irix_host_path(rt, guest_path, host_path, sizeof(host_path));
        if (!path) { syscall_result(cpu, -1); return; }
        syscall_result(cpu, (int64_t)chown(path, (uid_t)(uint32_t)cpu->gpr[5],
                                           (gid_t)(uint32_t)cpu->gpr[6]));
        return;
    }
    case SYS_lseek:
        syscall_result(cpu, (int64_t)lseek((int)(int32_t)cpu->gpr[4],
                                           (off_t)(int32_t)cpu->gpr[5],
                                           (int)(int32_t)cpu->gpr[6]));
        return;
    case SYS_setuid:
        syscall_result(cpu, (int64_t)setuid((uid_t)(uint32_t)cpu->gpr[4]));
        return;
    case SYS_setgid:
        syscall_result(cpu, (int64_t)setgid((gid_t)(uint32_t)cpu->gpr[4]));
        return;
    case SYS_umask:
        syscall_result(cpu, (int64_t)umask((mode_t)(uint32_t)cpu->gpr[4]));
        return;
    case SYS_rmdir: {
        const char *path = (const char *)irix_guest_ptr(rt, (IrixGuestAddr)(uint32_t)cpu->gpr[4], 1u);
        if (!path) { errno = EFAULT; syscall_result(cpu, -1); return; }
        syscall_result(cpu, (int64_t)rmdir(path));
        return;
    }
    case SYS_mkdir: {
        const char *path = (const char *)irix_guest_ptr(rt, (IrixGuestAddr)(uint32_t)cpu->gpr[4], 1u);
        if (!path) { errno = EFAULT; syscall_result(cpu, -1); return; }
        syscall_result(cpu, (int64_t)mkdir(path, (mode_t)(uint32_t)cpu->gpr[5]));
        return;
    }
    case SYS_rename: {
        const char *oldp = (const char *)irix_guest_ptr(rt, (IrixGuestAddr)(uint32_t)cpu->gpr[4], 1u);
        const char *newp = (const char *)irix_guest_ptr(rt, (IrixGuestAddr)(uint32_t)cpu->gpr[5], 1u);
        if (!oldp || !newp) { errno = EFAULT; syscall_result(cpu, -1); return; }
        syscall_result(cpu, (int64_t)rename(oldp, newp));
        return;
    }
    case SYS_symlink: {
        const char *target = (const char *)irix_guest_ptr(rt, (IrixGuestAddr)(uint32_t)cpu->gpr[4], 1u);
        const char *linkpath = (const char *)irix_guest_ptr(rt, (IrixGuestAddr)(uint32_t)cpu->gpr[5], 1u);
        if (!target || !linkpath) { errno = EFAULT; syscall_result(cpu, -1); return; }
        syscall_result(cpu, (int64_t)symlink(target, linkpath));
        return;
    }
    case SYS_readlink: {
        const char *path = (const char *)irix_guest_ptr(rt, (IrixGuestAddr)(uint32_t)cpu->gpr[4], 1u);
        IrixGuestAddr buf_addr = (IrixGuestAddr)(uint32_t)cpu->gpr[5];
        size_t count = (size_t)(uint32_t)cpu->gpr[6];
        void *buf = irix_guest_ptr(rt, buf_addr, count);
        if (!path || (count && !buf)) { errno = EFAULT; syscall_result(cpu, -1); return; }
        syscall_result(cpu, (int64_t)readlink(path, (char *)buf, count));
        return;
    }
    case SYS_truncate: {
        const char *path = (const char *)irix_guest_ptr(rt, (IrixGuestAddr)(uint32_t)cpu->gpr[4], 1u);
        if (!path) { errno = EFAULT; syscall_result(cpu, -1); return; }
        syscall_result(cpu, (int64_t)truncate(path, (off_t)(int32_t)cpu->gpr[5]));
        return;
    }
    case SYS_ftruncate:
        syscall_result(cpu, (int64_t)ftruncate((int)(int32_t)cpu->gpr[4],
                                               (off_t)(int32_t)cpu->gpr[5]));
        return;
    case SYS_fsync:
        syscall_result(cpu, (int64_t)fsync((int)(int32_t)cpu->gpr[4]));
        return;
    case SYS_fchdir:
        syscall_result(cpu, (int64_t)fchdir((int)(int32_t)cpu->gpr[4]));
        return;
    case SYS_fchown:
        syscall_result(cpu, (int64_t)fchown((int)(int32_t)cpu->gpr[4],
                                            (uid_t)(uint32_t)cpu->gpr[5],
                                            (gid_t)(uint32_t)cpu->gpr[6]));
        return;
    case SYS_fchmod:
        syscall_result(cpu, (int64_t)fchmod((int)(int32_t)cpu->gpr[4],
                                            (mode_t)(uint32_t)cpu->gpr[5]));
        return;
    case SYS_lchown: {
        const char *path = (const char *)irix_guest_ptr(rt, (IrixGuestAddr)(uint32_t)cpu->gpr[4], 1u);
        if (!path) { errno = EFAULT; syscall_result(cpu, -1); return; }
        syscall_result(cpu, (int64_t)lchown(path, (uid_t)(uint32_t)cpu->gpr[5],
                                            (gid_t)(uint32_t)cpu->gpr[6]));
        return;
    }
    case SYS_getpid: /* getpid */
        syscall_result(cpu, (int64_t)getpid());
        return;
    case SYS_getuid: /* getuid */
        syscall_result(cpu, (int64_t)getuid());
        return;
    case SYS_access: { /* access */
        IrixGuestAddr path_addr = (IrixGuestAddr)(uint32_t)cpu->gpr[4];
        int mode = (int)(int32_t)cpu->gpr[5];
        const char *path = (const char *)irix_guest_ptr(rt, path_addr, 1u);
        if (!path) {
            errno = EFAULT;
            syscall_result(cpu, -1);
            return;
        }
        syscall_result(cpu, (int64_t)access(path, mode));
        return;
    }
    case SYS_syssgi: /* syssgi */
        irix_compat_syssgi(rt, cpu);
        return;
    case SYS_getgid: /* getgid */
        syscall_result(cpu, (int64_t)getgid());
        return;
    case SYS_sysmp: /* sysmp(cmd, ...) */
        irix_compat_sysmp(rt, cpu);
        return;
    case SYS_setregid: { /* setregid */
        gid_t rgid = (gid_t)(uint32_t)cpu->gpr[4];
        gid_t egid = (gid_t)(uint32_t)cpu->gpr[5];
        syscall_result(cpu, (int64_t)setregid(rgid, egid));
        return;
    }
    case SYS_setreuid: { /* setreuid */
        uid_t ruid = (uid_t)(uint32_t)cpu->gpr[4];
        uid_t euid = (uid_t)(uint32_t)cpu->gpr[5];
        syscall_result(cpu, (int64_t)setreuid(ruid, euid));
        return;
    }
    case SYS_prctl: /* prctl */
        irix_compat_prctl(rt, cpu);
        return;
    case SYS_mmap: /* mmap */
        irix_compat_mmap(rt, cpu);
        return;
    case SYS_munmap: /* munmap */
        irix_compat_munmap(rt, cpu);
        return;
    case SYS_mprotect: /* mprotect */
        irix_compat_mprotect(rt, cpu);
        return;
    case SYS_getpagesize: /* getpagesize */
        syscall_result(cpu, 16384);
        return;
    case SYS_sysinfo: /* sysinfo(command, buf, count) */
        irix_compat_systeminfo(rt, cpu);
        return;
    case SYS_xstat: { /* xstat(version, path, statbuf) */
        uint32_t version = (uint32_t)cpu->gpr[4];
        IrixGuestAddr path_addr = (IrixGuestAddr)(uint32_t)cpu->gpr[5];
        IrixGuestAddr out = (IrixGuestAddr)(uint32_t)cpu->gpr[6];
        const char *guest_path = (const char *)irix_guest_ptr(rt, path_addr, 1u);
        char host_path[PATH_MAX];
        const char *path;
        struct stat st;
        if (!guest_path) {
            errno = EFAULT;
            syscall_result(cpu, -1);
            return;
        }
        path = irix_host_path(rt, guest_path, host_path, sizeof(host_path));
        if (!path) {
            syscall_result(cpu, -1);
            return;
        }
        result = (int64_t)stat(path, &st);
        if (result == 0 && write_irix_xstat(rt, version, out, &st, -1, guest_path) != 0)
            result = -1;
        syscall_result(cpu, result);
        return;
    }
    case SYS_lxstat: { /* lxstat(version, path, statbuf) */
        uint32_t version = (uint32_t)cpu->gpr[4];
        IrixGuestAddr path_addr = (IrixGuestAddr)(uint32_t)cpu->gpr[5];
        IrixGuestAddr out = (IrixGuestAddr)(uint32_t)cpu->gpr[6];
        const char *guest_path = (const char *)irix_guest_ptr(rt, path_addr, 1u);
        char host_path[PATH_MAX];
        const char *path;
        struct stat st;
        if (!guest_path) {
            errno = EFAULT;
            syscall_result(cpu, -1);
            return;
        }
        path = irix_host_path(rt, guest_path, host_path, sizeof(host_path));
        if (!path) {
            syscall_result(cpu, -1);
            return;
        }
        result = (int64_t)lstat(path, &st);
        if (result == 0 && write_irix_xstat(rt, version, out, &st, -1, guest_path) != 0)
            result = -1;
        syscall_result(cpu, result);
        return;
    }
    case SYS_fxstat: { /* fxstat(version, fd, statbuf) */
        uint32_t version = (uint32_t)cpu->gpr[4];
        int fd = (int)(int32_t)cpu->gpr[5];
        IrixGuestAddr out = (IrixGuestAddr)(uint32_t)cpu->gpr[6];
        struct stat st;
        result = (int64_t)fstat(fd, &st);
        if (result == 0 && write_irix_xstat(rt, version, out, &st, fd, NULL) != 0)
            result = -1;
        syscall_result(cpu, result);
        return;
    }
    default:
        fprintf(stderr, "UNIMPLEMENTED IRIX SYSCALL %u pc=0x%08x a0-a3=%08x,%08x,%08x,%08x\n",
                number, (uint32_t)cpu->pc,
                (uint32_t)cpu->gpr[4], (uint32_t)cpu->gpr[5],
                (uint32_t)cpu->gpr[6], (uint32_t)cpu->gpr[7]);
        rt->fault_addr = number;
        irix_guest_trap(rt, cpu, 0xeeee2303u);
        return;
    }
}

static int try_trivial_stripped_ctor(IrixGuestRuntime *rt, IrixGuestCpu *cpu,
                                     IrixGuestAddr address)
{
    IrixGuestAddr object;
    uint32_t w0, w1, w2, w3, w4;

    if (!address || !irix_guest_ptr(rt, address, 20u))
        return 0;

    w0 = irix_guest_read32be(rt, address + 0u);
    w1 = irix_guest_read32be(rt, address + 4u);
    w2 = irix_guest_read32be(rt, address + 8u);
    w3 = irix_guest_read32be(rt, address + 12u);
    w4 = irix_guest_read32be(rt, address + 16u);

    if (w0 != 0x00801025u || w1 != 0xac800008u ||
        w2 != 0xac800004u || w3 != 0x03e00008u ||
        w4 != 0xac800000u)
        return 0;

    object = (IrixGuestAddr)(uint32_t)cpu->gpr[4];
    if (!irix_guest_ptr(rt, object, 12u))
        return 0;

    irix_guest_write32be(rt, object + 8u, 0u);
    irix_guest_write32be(rt, object + 4u, 0u);
    irix_guest_write32be(rt, object + 0u, 0u);
    cpu->gpr[2] = (uint64_t)(int64_t)(int32_t)object;
    cpu->gpr[0] = 0;
    return 1;
}

static int try_shared_return_epilogue(IrixGuestRuntime *rt, IrixGuestCpu *cpu,
                                      IrixGuestAddr address)
{
    uint32_t w0, w1, w2;
    int32_t frame;
    uint32_t sp;

    if (!address || !irix_guest_ptr(rt, address, 12u))
        return 0;

    w0 = irix_guest_read32be(rt, address + 0u);
    w1 = irix_guest_read32be(rt, address + 4u);
    w2 = irix_guest_read32be(rt, address + 8u);

    /*
     * Some stripped IRIX DSOs expose tiny shared failure epilogues as
     * independently callable targets even though no ELF function symbol starts
     * here.  Keep this recovery deliberately exact: li v0,-1; jr ra; then a
     * positive addiu/daddiu sp,sp,N in the return delay slot.
     */
    if (w0 != 0x2402ffffu || w1 != 0x03e00008u)
        return 0;
    if (((w2 >> 26) & 0x3fu) != 0x09u && ((w2 >> 26) & 0x3fu) != 0x19u)
        return 0;
    if (((w2 >> 21) & 0x1fu) != 29u || ((w2 >> 16) & 0x1fu) != 29u)
        return 0;

    frame = (int16_t)(w2 & 0xffffu);
    if (frame <= 0 || (frame & 7) != 0)
        return 0;

    cpu->gpr[2] = UINT64_C(0xffffffffffffffff);
    sp = (uint32_t)cpu->gpr[29] + (uint32_t)frame;
    cpu->gpr[29] = (uint64_t)(int64_t)(int32_t)sp;
    cpu->gpr[0] = 0;
    return 1;
}

static void report_nearest_functions(IrixGuestRuntime *rt, IrixGuestAddr address)
{
    IrixGuestFunctionEntry *below = 0;
    IrixGuestFunctionEntry *above = 0;
    size_t i;

    for (i = 0; i < rt->function_count; ++i) {
        IrixGuestFunctionEntry *e = &rt->functions[i];
        if (e->address < address && (!below || e->address > below->address))
            below = e;
        if (e->address > address && (!above || e->address < above->address))
            above = e;
    }

    if (below)
        fprintf(stderr, " below=0x%08x:%s(+0x%x)",
                below->address, below->name ? below->name : "?",
                (uint32_t)(address - below->address));
    if (above)
        fprintf(stderr, " above=0x%08x:%s(-0x%x)",
                above->address, above->name ? above->name : "?",
                (uint32_t)(above->address - address));
}

static int trace_dialog_call(const char *name)
{
    if (!name) return 0;
    return strstr(name, "postBlocked__15VkDialogManager") != 0 ||
           strstr(name, "waitForDialog__15VkDialogManager") != 0;
}

static int irix_native_xm_build_resources(IrixGuestRuntime *rt,
                                          IrixGuestCpu *cpu)
{
    const uint32_t rec_size = 20u;
    IrixGuestAddr wc_ptrp = (IrixGuestAddr)(uint32_t)cpu->gpr[4];
    IrixGuestAddr wc_nump = (IrixGuestAddr)(uint32_t)cpu->gpr[5];
    IrixGuestAddr sc = (IrixGuestAddr)(uint32_t)cpu->gpr[6];
    uint32_t sc_num = (uint32_t)cpu->gpr[7];
    IrixGuestAddr wc;
    uint32_t wc_num;
    IrixGuestAddr merged;
    uint32_t new_num;
    uint32_t i, j;
    uint64_t total_bytes;

    if (!wc_ptrp || !wc_nump ||
        !irix_guest_ptr(rt, wc_ptrp, 4u) ||
        !irix_guest_ptr(rt, wc_nump, 4u)) {
        fprintf(stderr,
                "IRIX native _XmBuildResources: invalid output pointers "
                "wc_ptrp=%08x wc_nump=%08x\n",
                wc_ptrp, wc_nump);
        return -1;
    }

    wc = irix_guest_read32be(rt, wc_ptrp);
    wc_num = irix_guest_read32be(rt, wc_nump);

    fprintf(stderr,
            "IRIX native _XmBuildResources: wc=%08x wc_num=%u "
            "sc=%08x sc_num=%u\n",
            wc, wc_num, sc, sc_num);

    if (wc_num == 0u) {
        irix_guest_write32be(rt, wc_ptrp, sc);
        irix_guest_write32be(rt, wc_nump, sc_num);
        cpu->gpr[2] = 0u;
        cpu->gpr[0] = 0u;
        return rt->trapped ? -1 : 0;
    }

    if (wc_num > 4096u || sc_num > 4096u) {
        fprintf(stderr,
                "IRIX native _XmBuildResources: absurd counts "
                "wc_num=%u sc_num=%u\n",
                wc_num, sc_num);
        return -1;
    }

    if ((wc_num && (!wc || !irix_guest_ptr(rt, wc, wc_num * rec_size))) ||
        (sc_num && (!sc || !irix_guest_ptr(rt, sc, sc_num * rec_size)))) {
        fprintf(stderr,
                "IRIX native _XmBuildResources: invalid resource arrays "
                "wc=%08x/%u sc=%08x/%u\n",
                wc, wc_num, sc, sc_num);
        return -1;
    }

    total_bytes = (uint64_t)(wc_num + sc_num) * rec_size;
    if (!total_bytes || total_bytes > UINT32_MAX)
        return -1;

    merged = irix_libc_guest_malloc((uint32_t)total_bytes);
    if (!merged || !irix_guest_ptr(rt, merged, (uint32_t)total_bytes)) {
        fprintf(stderr,
                "IRIX native _XmBuildResources: allocation failed bytes=%u\n",
                (uint32_t)total_bytes);
        return -1;
    }

    if (sc_num) {
        void *dst = irix_guest_ptr(rt, merged, sc_num * rec_size);
        void *src = irix_guest_ptr(rt, sc, sc_num * rec_size);
        if (!dst || !src)
            return -1;
        memcpy(dst, src, sc_num * rec_size);
    }

    new_num = sc_num;
    for (i = 0; i < wc_num; ++i) {
        IrixGuestAddr wr = wc + i * rec_size;
        uint32_t name = irix_guest_read32be(rt, wr + 0u);
        int override = 0;

        for (j = 0; j < sc_num; ++j) {
            IrixGuestAddr nr = merged + j * rec_size;
            if (irix_guest_read32be(rt, nr + 0u) == name) {
                /*
                 * Motif override semantics preserve the superclass record
                 * except for export_proc and import_proc.
                 */
                irix_guest_write32be(
                    rt, nr + 12u, irix_guest_read32be(rt, wr + 12u));
                irix_guest_write32be(
                    rt, nr + 16u, irix_guest_read32be(rt, wr + 16u));
                override = 1;
                break;
            }
        }

        if (!override) {
            IrixGuestAddr nr = merged + new_num * rec_size;
            void *dst = irix_guest_ptr(rt, nr, rec_size);
            void *src = irix_guest_ptr(rt, wr, rec_size);
            if (!dst || !src)
                return -1;
            memcpy(dst, src, rec_size);
            ++new_num;
        }
    }

    irix_guest_write32be(rt, wc_ptrp, merged);
    irix_guest_write32be(rt, wc_nump, new_num);

    fprintf(stderr,
            "IRIX native _XmBuildResources: merged=%08x count=%u "
            "capacity=%u\n",
            merged, new_num, wc_num + sc_num);

    cpu->gpr[2] = 0u;
    cpu->gpr[0] = 0u;
    return rt->trapped ? -1 : 0;
}

static int irix_guest_looks_like_widget(IrixGuestRuntime *rt,
                                        IrixGuestAddr widget)
{
    IrixGuestAddr cls;

    if (!widget || !irix_guest_ptr(rt, widget, 12u))
        return 0;
    cls = irix_guest_read32be(rt, widget + 4u);
    if (!cls || !irix_guest_ptr(rt, cls, 0x74u))
        return 0;
    return 1;
}

static int irix_guest_looks_like_widget_class(IrixGuestRuntime *rt,
                                              IrixGuestAddr widget_class)
{
    uint32_t wsize;

    if (!widget_class || !irix_guest_ptr(rt, widget_class, 12u))
        return 0;
    wsize = irix_guest_read32be(rt, widget_class + 0x08u);
    return wsize != 0u && wsize <= 0x01000000u;
}

static int irix_guest_repair_xt_widget_alloc(IrixGuestRuntime *rt,
                                             IrixGuestCpu *cpu,
                                             IrixGuestAddr caller_sp,
                                             IrixGuestAddr widget_class)
{
    IrixGuestAddr out_slot = caller_sp + 0x20u;
    IrixGuestAddr widget;
    IrixGuestAddr constraints = 0u;
    uint32_t csize;
    uint32_t wsize;
    uint32_t aligned_wsize;
    uint64_t total;
    void *p;

    /*
     * SGI Xt xtWidgetAlloc extension-allocation fallback.
     *
     * ObjectClassExtension.allocate receives &widget as its eighth N32
     * argument (sp+0x20). Some recovered Motif allocation callbacks return
     * without filling that output. The next original Xt instructions
     * unconditionally execute widget->core.self = widget and fault at NULL.
     *
     * Mirror xtWidgetAlloc's ordinary no-extension path: allocate a zeroed
     * widget plus constraint tail, install CorePart.constraints, then let the
     * original Xt code fill self/parent/class/xrm_name/being_destroyed.
     */
    if (!caller_sp || !widget_class ||
        !irix_guest_ptr(rt, out_slot, 4u) ||
        !irix_guest_ptr(rt, widget_class, 12u))
        return 0;
    if (irix_guest_read32be(rt, out_slot) != 0u)
        return 0;

    csize = irix_guest_read32be(rt, caller_sp + 0x10u);
    wsize = irix_guest_read32be(rt, widget_class + 0x08u);
    if (!wsize || wsize > 0x01000000u || csize > 0x01000000u)
        return 0;

    aligned_wsize = wsize;
    if (csize && (csize & 7u) == 0u)
        aligned_wsize = (wsize + 7u) & ~7u;

    total = (uint64_t)aligned_wsize + (uint64_t)csize;
    if (!total || total > UINT32_MAX)
        return 0;

    widget = irix_libc_guest_malloc((uint32_t)total);
    p = widget ? irix_guest_ptr(rt, widget, (size_t)total) : NULL;
    if (!widget || !p)
        return 0;
    memset(p, 0, (size_t)total);

    if (csize)
        constraints = widget + aligned_wsize;
    irix_guest_write32be(rt, widget + 0x18u, constraints);
    irix_guest_write32be(rt, out_slot, widget);

    fprintf(stderr,
            "IRIX xtWidgetAlloc fallback: class=%08x wsize=%u csize=%u "
            "widget=%08x constraints=%08x\\n",
            widget_class, wsize, csize, widget, constraints);
    cpu->gpr[2] = widget;
    return rt->trapped ? -1 : 1;
}

static int irix_guest_call_entry_impl(IrixGuestRuntime *rt, IrixGuestCpu *cpu,
                                      IrixGuestAddr address,
                                      IrixGuestFunctionEntry *entry)
{
    static unsigned xm_display_shell_depth;
    static IrixGuestAddr xm_display_bootstrap_widget;
    static IrixGuestAddr sg_drop_class[16];
    static IrixGuestAddr sg_drop_state[16];
    static IrixGuestFunctionEntry *xm_build_resources_entry;
    static IrixGuestFunctionEntry *xm_save_core_translations_entry;

    /*
     * Motif InitializePrehook ABI repair.
     *
     * The recovered prehook at 0x0b2e54f0 has the normal XtInitProc
     * signature (req, new_w, args, num_args). Its delay slot at 0x0b2e5518
     * copies a1 into s0 and the following instructions immediately use
     * s0 as new_w. In the failing hybrid path a1 is a stack scratch address
     * while the caller-preserved s1 still holds the real new_w.
     *
     * Repair only this demonstrated malformed case, and only when s1 has the
     * shape of a live Xt widget while a1 does not.
     */
    if (address == 0x0b2e54f0u) {
        IrixGuestAddr new_w = (IrixGuestAddr)(uint32_t)cpu->gpr[5];
        IrixGuestAddr saved_new_w =
            (IrixGuestAddr)(uint32_t)cpu->gpr[17];

        if (!irix_guest_looks_like_widget(rt, new_w) &&
            irix_guest_looks_like_widget(rt, saved_new_w)) {
            fprintf(stderr,
                    "IRIX Motif InitializePrehook ABI repair: "
                    "a1=%08x -> s1=%08x\n",
                    new_w, saved_new_w);
            cpu->gpr[5] =
                (uint64_t)(int64_t)(int32_t)saved_new_w;
        }
    }

    /*
     * Replace SGI Motif _XmBuildResources wholesale for bring-up.
     *
     * We know 0x0b2a9588 is its XtMalloc callsite from the original MIPS
     * instruction stream. Resolve whichever recovered function contains that
     * PC instead of hard-coding a guessed symbol boundary; then execute the
     * source-equivalent native implementation above whenever that recovered
     * entry is called.
     */
    if (!xm_build_resources_entry)
        xm_build_resources_entry =
            irix_guest_find_function(rt, 0x0b2a9588u);

    if (!xm_save_core_translations_entry)
        xm_save_core_translations_entry =
            irix_guest_find_function(rt, 0x0b2c1adcu);

    if (entry && xm_build_resources_entry &&
        entry == xm_build_resources_entry &&
        address == entry->address) {
        fprintf(stderr,
                "IRIX native override: _XmBuildResources "
                "entry=%08x interior=0b2a9588\n",
                entry->address);
        return irix_native_xm_build_resources(rt, cpu);
    }

    /*
     * _XmSaveCoreClassTranslations(widget) ultimately reads
     * widget->core.widget_class->core_class.tm_table. During one recovered
     * InitializePrehook path the widget exists but widget_class is still NULL,
     * so the original helper dereferences class+0x64 and faults at 0x64.
     *
     * Keep the original Motif helper for valid widgets. For only the malformed
     * transient case, treat the save as empty; there is no class translation
     * table to preserve yet, so this is equivalent to saving nothing.
     */
    if (entry && xm_save_core_translations_entry &&
        entry == xm_save_core_translations_entry &&
        address == entry->address) {
        IrixGuestAddr widget = (IrixGuestAddr)(uint32_t)cpu->gpr[4];
        IrixGuestAddr widget_class = 0u;

        if (widget && irix_guest_ptr(rt, widget, 8u))
            widget_class = irix_guest_read32be(rt, widget + 4u);

        if (!widget_class || !irix_guest_ptr(rt, widget_class, 0x68u)) {
            fprintf(stderr,
                    "IRIX _XmSaveCoreClassTranslations repair: "
                    "widget=%08x class=%08x -> skip empty save\n",
                    widget, widget_class);
            cpu->gpr[2] = 0u;
            cpu->gpr[0] = 0u;
            return 0;
        }
    }

    /*
     * Motif _XmSortResourceList always expects XrmResourceList[]: an array
     * of pointers to compiled resource records.  Several SGI Motif class
     * paths reach it with their raw contiguous XtResource[] instead.
     *
     * At function entry a0 is the resource table and a1 is the count.  Reuse
     * the generic Xt compatibility normalizer here for every caller.  It is
     * idempotent for an already-indirect table and allocates a writable
     * pointer table for a raw resource array, which is important because
     * _XmSortResourceList reorders the pointer array in place.
     */
    if (address == 0x0b2ab1a0u) {
        IrixGuestAddr table = (IrixGuestAddr)(uint32_t)cpu->gpr[4];
        uint32_t count = (uint32_t)cpu->gpr[5];
        IrixGuestAddr repaired =
            irix_guest_xt_ensure_resource_table(rt, table, count);
        if (!rt->trapped && repaired != table) {
            fprintf(stderr,
                    "IRIX XmSortResourceList repair: raw=%08x count=%u -> table=%08x\n",
                    table, count, repaired);
            cpu->gpr[4] = repaired;
        }
    }

    /*
     * Xt translation/accelerator parser scratch repair.
     *
     * The helper at 0x0f627994 immediately stores through a2.  In the
     * failing XtCvtStringToTranslationTable path, the caller at 0x0f628448
     * loads a2 from its local output slot at sp+0xea8, but that slot is still
     * NULL.  Reuse the local slot itself as writable scratch so the parser can
     * continue instead of dereferencing address zero.
     */
    if (address == 0x0f627994u &&
        (uint32_t)cpu->gpr[6] == 0u) {
        IrixGuestAddr scratch =
            (IrixGuestAddr)(uint32_t)cpu->gpr[29] + 0x0ea8u;
        if (irix_guest_ptr(rt, scratch, 4u)) {
            fprintf(stderr,
                    "IRIX Xt translation parser repair: a2=NULL -> %08x\n",
                    scratch);
            cpu->gpr[6] = scratch;
        }
    }

    /*
     * Resolve the SGI Motif XmDialogShell core-class extension inheritance
     * that Xt class initialization failed to fold.
     *
     * XmDialogShell (0x0b3e6d18) has core_class.extension at +0x70.  Its
     * first extension hook at +0x10 is _XtInherit, while its superclass
     * TransientShell has no corresponding core extension.  The inherited
     * value is therefore NULL.  Leaving _XtInherit in place causes the
     * internal Xt initializer at 0x0f62b47c to call the fatal sentinel.
     *
     * Patch the slot before that initializer tests it so Xt follows its
     * normal no-extension fallback path.
     */
    if (address == 0x0f62b47cu &&
        (uint32_t)cpu->gpr[4] == 0x0b3e6d18u) {
        IrixGuestAddr cls = 0x0b3e6d18u;
        IrixGuestAddr ext = irix_guest_read32be(rt, cls + 0x70u);
        if (!rt->trapped && ext &&
            irix_guest_ptr(rt, ext + 0x10u, 4u) &&
            irix_guest_read32be(rt, ext + 0x10u) == 0x0f6588b8u) {
            fprintf(stderr,
                    "IRIX Xt inheritance repair: XmDialogShell core extension hook _XtInherit -> NULL\n");
            irix_guest_write32be(rt, ext + 0x10u, 0u);
        }
    }

    if (irix_mips_interp_should_force(address)) {
        fprintf(stderr,
                "IRIX dispatcher: forced interpreter target=%08x\n",
                address);
        return irix_mips_interp_call(rt, cpu, address);
    }

    /*
     * SGI Xt translation-manager internal entries reached via BAL.
     *
     * The public-name fallback below is not sufficient because several TM
     * paths enter stripped/static helpers directly. Keep those internal
     * helper flows in original MIPS so their private frame/register contracts
     * are not split across generated C and recovered function boundaries.
     *
     * 0x0f623fcc: parser helper reached from 0x0f625074.
     * 0x0f622e2c: merge/override helper reached from XtOverrideTranslations;
     *             translated execution was observed entering it with s7
     *             becoming ASCII string bytes and faulting at 0x0f622e8c.
     */
    if (address == 0x0f623fccu || address == 0x0f622e2cu) {
        uint64_t saved_gp = cpu->gpr[28];
        IrixGuestAddr target_gp = irix_guest_gp_for_address(address);
        int rc;
        if (target_gp)
            cpu->gpr[28] = (uint64_t)(int64_t)(int32_t)target_gp;
        cpu->gpr[25] = (uint64_t)(int64_t)(int32_t)address;
        fprintf(stderr,
                "IRIX Xt TMparse internal fallback: %08x -> original MIPS\n",
                address);
        rc = irix_mips_interp_call(rt, cpu, address);
        if (!rt->trapped)
            cpu->gpr[28] = saved_gp;
        return rc;
    }

    /*
     * Bring-up escape hatch for Xt's translation manager.
     *
     * The translated converter path has repeatedly exposed malformed XrmValue
     * state (including {size=0,addr=1} for the cursor converter and now a
     * translation parser source becoming the scalar size 0x1a). Run the
     * public translation/accelerator conversion and merge entry points as
     * original MIPS for now. This keeps the rest of Xt statically translated
     * while making parsing, override/augment, and their hidden helpers use
     * SGI's exact structure/register conventions end-to-end.
     */
    if (entry && entry->name &&
        (!strcmp(entry->name, "XtCvtStringToTranslationTable") ||
         !strcmp(entry->name, "XtCvtStringToAcceleratorTable") ||
         !strcmp(entry->name, "XtParseTranslationTable") ||
         !strcmp(entry->name, "XtParseAcceleratorTable") ||
         !strcmp(entry->name, "XtOverrideTranslations") ||
         !strcmp(entry->name, "XtAugmentTranslations"))) {
        uint64_t saved_gp = cpu->gpr[28];
        IrixGuestAddr target_gp = irix_guest_gp_for_address(address);
        int rc;
        if (target_gp)
            cpu->gpr[28] = (uint64_t)(int64_t)(int32_t)target_gp;
        cpu->gpr[25] = (uint64_t)(int64_t)(int32_t)address;
        fprintf(stderr,
                "IRIX Xt TMparse fallback: %s @ %08x -> original MIPS\n",
                entry->name, address);
        rc = irix_mips_interp_call(rt, cpu, address);
        if (!rt->trapped)
            cpu->gpr[28] = saved_gp;
        return rc;
    }

    if (address == 0x0f6588b8u) {
        fprintf(stderr,
                "IRIX _XtInherit called: caller_pc=%08x ra=%08x sp=%08x gp=%08x t9=%08x a0-a7=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x\n",
                (uint32_t)cpu->pc,
                (uint32_t)cpu->gpr[31],
                (uint32_t)cpu->gpr[29],
                (uint32_t)cpu->gpr[28],
                (uint32_t)cpu->gpr[25],
                (uint32_t)cpu->gpr[4], (uint32_t)cpu->gpr[5],
                (uint32_t)cpu->gpr[6], (uint32_t)cpu->gpr[7],
                (uint32_t)cpu->gpr[8], (uint32_t)cpu->gpr[9],
                (uint32_t)cpu->gpr[10], (uint32_t)cpu->gpr[11]);
    }

    if (!entry) {
        IrixGuestAddr s4 = (IrixGuestAddr)(uint32_t)cpu->gpr[20];

        if (try_trivial_stripped_ctor(rt, cpu, address))
            return rt->trapped ? -1 : 0;
        if (try_shared_return_epilogue(rt, cpu, address))
            return rt->trapped ? -1 : 0;

        /*
         * Universal IID escape hatch: if original executable code is mapped
         * at an otherwise-unregistered target, execute the guest MIPS
         * directly instead of dying on a missing recovered/translated range.
         *
         * This is still an N32 function call boundary. Preserve the caller's
         * $gp exactly like the translated dispatcher does. Without this, an
         * unregistered Xt helper can establish libXt's GP in its PIC prologue,
         * return successfully, and leave ViewKit executing with Xt's GOT.
         * That exact leak produced t9=0x6d6f7573 ("mous") at 0x0d6352e8.
         */
        {
            uint64_t saved_gp = cpu->gpr[28];
            IrixGuestAddr target_gp = irix_guest_gp_for_address(address);
            int interp_rc;

            if (target_gp)
                cpu->gpr[28] = (uint64_t)(int64_t)(int32_t)target_gp;
            cpu->gpr[25] = (uint64_t)(int64_t)(int32_t)address;

            interp_rc = irix_mips_interp_call(rt, cpu, address);
            if (interp_rc == 0) {
                if (!rt->trapped)
                    cpu->gpr[28] = saved_gp;
                return 0;
            }
            if (rt->trapped)
                return -1;

            /* No executable mapping / not a real guest function. Restore the
               caller before producing the INVALID GUEST CALL diagnostic. */
            cpu->gpr[28] = saved_gp;
        }

        fprintf(stderr,
                "INVALID GUEST CALL target=0x%08x caller=0x%08x s4=0x%08x gp=%08x t9=%08x sp=%08x ra=%08x",
                address, (uint32_t)cpu->pc, s4,
                (uint32_t)cpu->gpr[28], (uint32_t)cpu->gpr[25],
                (uint32_t)cpu->gpr[29], (uint32_t)cpu->gpr[31]);
        fprintf(stderr,
                " a0-a7=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x",
                (uint32_t)cpu->gpr[4], (uint32_t)cpu->gpr[5],
                (uint32_t)cpu->gpr[6], (uint32_t)cpu->gpr[7],
                (uint32_t)cpu->gpr[8], (uint32_t)cpu->gpr[9],
                (uint32_t)cpu->gpr[10], (uint32_t)cpu->gpr[11]);
        if (address && irix_guest_ptr(rt, address, 20u)) {
            fprintf(stderr,
                    " target_words=%08x,%08x,%08x,%08x,%08x",
                    irix_guest_read32be(rt, address + 0u),
                    irix_guest_read32be(rt, address + 4u),
                    irix_guest_read32be(rt, address + 8u),
                    irix_guest_read32be(rt, address + 12u),
                    irix_guest_read32be(rt, address + 16u));
        }
        report_nearest_functions(rt, address);
        if (s4 && irix_guest_ptr(rt, s4, 28u)) {
            fprintf(stderr,
                    " words=%08x,%08x,%08x,%08x,%08x,%08x,%08x",
                    irix_guest_read32be(rt, s4 + 0u),
                    irix_guest_read32be(rt, s4 + 4u),
                    irix_guest_read32be(rt, s4 + 8u),
                    irix_guest_read32be(rt, s4 + 12u),
                    irix_guest_read32be(rt, s4 + 16u),
                    irix_guest_read32be(rt, s4 + 20u),
                    irix_guest_read32be(rt, s4 + 24u));
        }
        fputc('\n', stderr);
        rt->trapped = 1;
        rt->trap_pc = address;
        rt->trap_code = 0xeeee0003u;
        return -1;
    }
    /*
     * SGI Motif XmDisplay singleton bootstrap.
     *
     * While the first XmDisplay shell is being constructed, Motif may ask for
     * XmGetXmDisplay() recursively before its widget-extension context has
     * been pushed/saved.  The recursive call must see the in-progress
     * XmDisplay rather than create another shell.
     */
    if (xm_display_shell_depth &&
        address == 0x0b2957dcu &&
        !xm_display_bootstrap_widget) {
        IrixGuestAddr w = (IrixGuestAddr)(uint32_t)cpu->gpr[4];
        if (w >= 0x30000000u && w < 0x40000000u) {
            xm_display_bootstrap_widget = w;
            fprintf(stderr,
                    "IRIX XmDisplay bootstrap widget: %08x\n",
                    xm_display_bootstrap_widget);
        }
    }

    if (xm_display_shell_depth &&
        address == 0x0b297c70u &&
        xm_display_bootstrap_widget) {
        cpu->gpr[2] = xm_display_bootstrap_widget;
        cpu->gpr[0] = 0;
        fprintf(stderr,
                "IRIX XmGetXmDisplay bootstrap return: %08x\n",
                xm_display_bootstrap_widget);
        return 0;
    }

    {
        uint64_t caller_saved[9];
        uint64_t caller_gp = cpu->gpr[28];
        uint64_t caller_sp = cpu->gpr[29];
        IrixGuestAddr callee_gp = 0u;
        IrixGuestAddr caller_pc = cpu->pc;
        IrixGuestAddr sg_saved_private = 0;
        IrixGuestAddr sg_class = 0;
        IrixGuestAddr sg_state = 0;
        int sg_drop_swap = 0;
        int xm_display_shell_call =
            (address == 0x0f62caa8u &&
             (uint32_t)cpu->gpr[6] == 0x0b3f0890u);
        unsigned saved_i;

        /*
         * Emergency XtGetValues/_XtCopyToArg guard.
         *
         * Some translated XtGetValues paths currently produce a bogus
         * args[index].value destination (observed 0xb00e1da4).  The original
         * _XtCopyToArg immediately dereferences that pointer.  If the
         * destination is not guest-mapped, skip that one output copy rather
         * than killing the process; callers will retain their existing value.
         */
        if (address == 0x0f61caecu) {
            IrixGuestAddr dst = (IrixGuestAddr)(uint32_t)cpu->gpr[5];
            uint32_t n = (uint32_t)cpu->gpr[6];
            if (!dst || !irix_guest_ptr(rt, dst, n ? n : 1u)) {
                fprintf(stderr,
                        "IRIX _XtCopyToArg guard: skip invalid dst=%08x size=%u src=%08x\n",
                        dst, n, (uint32_t)cpu->gpr[4]);
                cpu->gpr[2] = 0;
                cpu->gpr[0] = 0;
                return 0;
            }
        }

        /*
         * Emergency Motif font-list repair.
         *
         * A failed String -> XmFontList conversion can leave the literal
         * bytes "fixe" (from "fixed") in a 4-byte resource field.  When that
         * value reaches _XmFontListSearch as a0, treating it as XmFontList*
         * faults immediately.  _XmFontListSearch has a clean NULL path, so
         * force only this known malformed sentinel to NULL and let Motif take
         * its normal fallback/default handling.
         */
        if (address == 0x0b29c904u &&
            (uint32_t)cpu->gpr[4] == 0x66697865u) {
            fprintf(stderr,
                    "IRIX XmFontList repair: raw \"fixed\" sentinel -> NULL\n");
            cpu->gpr[4] = 0;
        }

        if (address == 0x0f636998u) {
            IrixGuestAddr args = (IrixGuestAddr)(uint32_t)cpu->gpr[5];

            /*
             * Emergency Xt cursor-converter ABI repair.
             *
             * SGI Xt's XtCvtStringToCursor expects args[0] to be an N32
             * XrmValue describing storage that contains the Display pointer.
             * The translated _XtConvert path currently builds a malformed
             * { size=0, addr=1 } record.  The converter already receives the
             * correct Display in a0, so synthesize the one argument it needs
             * directly in its stack argument block.
             *
             * num_args for this converter is one; args+8 is therefore safe
             * scratch immediately after args[0].
             */
            if (args && irix_guest_ptr(rt, args, 12u)) {
                IrixGuestAddr scratch = args + 8u;
                irix_guest_write32be(rt, scratch, (uint32_t)cpu->gpr[4]);
                irix_guest_write32be(rt, args + 0u, 4u);
                irix_guest_write32be(rt, args + 4u, scratch);
                fprintf(stderr,
                        "IRIX XtCvtStringToCursor repair: args=%08x size=4 addr=%08x dpy=%08x\n",
                        args, scratch, (uint32_t)cpu->gpr[4]);
            }
            uint32_t arg_size = 0, arg_addr = 0, arg_value = 0;
            if (args && irix_guest_ptr(rt, args, 8u)) {
                arg_size = irix_guest_read32be(rt, args + 0u);
                arg_addr = irix_guest_read32be(rt, args + 4u);
                if (arg_addr && irix_guest_ptr(rt, arg_addr, 4u))
                    arg_value = irix_guest_read32be(rt, arg_addr);
            }
            fprintf(stderr,
                    "IRIX XtCvtStringToCursor entry: caller=%08x dpy=%08x args=%08x num_args=%08x from=%08x to=%08x closure=%08x arg0.size=%08x arg0.addr=%08x arg0.value=%08x",
                    (uint32_t)cpu->pc, (uint32_t)cpu->gpr[4], args,
                    (uint32_t)cpu->gpr[6], (uint32_t)cpu->gpr[7],
                    (uint32_t)cpu->gpr[8], (uint32_t)cpu->gpr[9],
                    arg_size, arg_addr, arg_value);
            if (args && irix_guest_ptr(rt, args, 32u)) {
                fprintf(stderr,
                        " words=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x",
                        irix_guest_read32be(rt,args+0u),
                        irix_guest_read32be(rt,args+4u),
                        irix_guest_read32be(rt,args+8u),
                        irix_guest_read32be(rt,args+12u),
                        irix_guest_read32be(rt,args+16u),
                        irix_guest_read32be(rt,args+20u),
                        irix_guest_read32be(rt,args+24u),
                        irix_guest_read32be(rt,args+28u));
            }
            {
                IrixGuestAddr cvt = (IrixGuestAddr)(uint32_t)cpu->gpr[22];
                if (cvt && irix_guest_ptr(rt, cvt, 48u)) {
                    fprintf(stderr,
                            "IRIX XtCvtStringToCursor converter: s6=%08x words=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x\n",
                            cvt,
                            irix_guest_read32be(rt,cvt+0u),
                            irix_guest_read32be(rt,cvt+4u),
                            irix_guest_read32be(rt,cvt+8u),
                            irix_guest_read32be(rt,cvt+12u),
                            irix_guest_read32be(rt,cvt+16u),
                            irix_guest_read32be(rt,cvt+20u),
                            irix_guest_read32be(rt,cvt+24u),
                            irix_guest_read32be(rt,cvt+28u),
                            irix_guest_read32be(rt,cvt+32u),
                            irix_guest_read32be(rt,cvt+36u),
                            irix_guest_read32be(rt,cvt+40u),
                            irix_guest_read32be(rt,cvt+44u));
                }
            }
            fputc('\n', stderr);
        }

        /* N32 callee-saved integer state. s0-s7 and s8/fp must survive a
           completed guest call just as $gp already does below. Keeping this
           contract at the dispatcher boundary is especially important when
           a translated callee crosses recovered ranges before returning. */
        for (saved_i = 0; saved_i < 8u; ++saved_i)
            caller_saved[saved_i] = cpu->gpr[16u + saved_i];
        caller_saved[8] = cpu->gpr[30];
        if (address == 0x10044150u) {
            IrixGuestFunctionEntry *caller_entry = irix_guest_find_function(rt, caller_pc);
            fprintf(stderr,
                    "IRIX tuTopLevel::resize entry: caller_pc=0x%08x %s this=0x%08x width=%d height=%d ra=0x%08x\\n",
                    caller_pc,
                    (caller_entry && caller_entry->name) ? caller_entry->name : "?",
                    (uint32_t)cpu->gpr[4], (int32_t)cpu->gpr[5],
                    (int32_t)cpu->gpr[6], (uint32_t)cpu->gpr[31]);
        }
        if (address == 0x10043a60u) {
            IrixGuestFunctionEntry *caller_entry = irix_guest_find_function(rt, caller_pc);
            fprintf(stderr,
                    "IRIX tuTopLevel::setInitialSize entry: caller_pc=0x%08x %s this=0x%08x width=%d height=%d ra=0x%08x\\n",
                    caller_pc,
                    (caller_entry && caller_entry->name) ? caller_entry->name : "?",
                    (uint32_t)cpu->gpr[4], (int32_t)cpu->gpr[5],
                    (int32_t)cpu->gpr[6], (uint32_t)cpu->gpr[31]);
        }
        if (address == 0x10024fdcu) {
            IrixGuestFunctionEntry *caller_entry = irix_guest_find_function(rt, caller_pc);
            fprintf(stderr,
                    "IRIX tuGadget::resize entry: caller_pc=0x%08x %s this=0x%08x width=%d height=%d ra=0x%08x\\n",
                    caller_pc,
                    (caller_entry && caller_entry->name) ? caller_entry->name : "?",
                    (uint32_t)cpu->gpr[4], (int32_t)cpu->gpr[5],
                    (int32_t)cpu->gpr[6], (uint32_t)cpu->gpr[31]);
        }
        /*
         * Targeted call-boundary tracer for stalls that occur immediately
         * after a known guest call site.  Set IRIX_TRACE_FROM_CALLER to a
         * guest PC (hex accepted).  Once that caller is observed, dump the
         * next 64 dispatcher crossings.  This stays dormant in normal runs.
         */
        {
            static int trace_init;
            static IrixGuestAddr trace_from;
            static unsigned trace_left;
            if (!trace_init) {
                const char *s = getenv("IRIX_TRACE_FROM_CALLER");
                if (s && *s)
                    trace_from = (IrixGuestAddr)strtoul(s, NULL, 0);
                trace_init = 1;
            }
            if (trace_from && caller_pc == trace_from)
                trace_left = 64u;
            if (trace_left) {
                fprintf(stderr,
                        "IRIX call trace: caller_pc=0x%08x callee=0x%08x:%s "
                        "ra=0x%08x sp=0x%08x gp=0x%08x at=0x%08x t9=0x%08x "
                        "a0-a3=%08x,%08x,%08x,%08x "
                        "s0-s3=%08x,%08x,%08x,%08x\n",
                        caller_pc, address, entry->name ? entry->name : "?",
                        (uint32_t)cpu->gpr[31], (uint32_t)cpu->gpr[29],
                        (uint32_t)cpu->gpr[28], (uint32_t)cpu->gpr[1],
                        (uint32_t)cpu->gpr[25],
                        (uint32_t)cpu->gpr[4], (uint32_t)cpu->gpr[5],
                        (uint32_t)cpu->gpr[6], (uint32_t)cpu->gpr[7],
                        (uint32_t)cpu->gpr[16], (uint32_t)cpu->gpr[17],
                        (uint32_t)cpu->gpr[18], (uint32_t)cpu->gpr[19]);
                if (trace_from && caller_pc == trace_from) {
                    IrixGuestAddr s0 = (IrixGuestAddr)(uint32_t)cpu->gpr[16];
                    if (s0 && irix_guest_ptr(rt, s0, 32u)) {
                        fprintf(stderr,
                                "IRIX call trace s0@%08x: "
                                "%08x %08x %08x %08x %08x %08x %08x %08x\n",
                                s0,
                                irix_guest_read32be(rt, s0 + 0u),
                                irix_guest_read32be(rt, s0 + 4u),
                                irix_guest_read32be(rt, s0 + 8u),
                                irix_guest_read32be(rt, s0 + 12u),
                                irix_guest_read32be(rt, s0 + 16u),
                                irix_guest_read32be(rt, s0 + 20u),
                                irix_guest_read32be(rt, s0 + 24u),
                                irix_guest_read32be(rt, s0 + 28u));
                    }
                }
                --trace_left;
            }
        }
        if (trace_dialog_call(entry->name)) {
            fprintf(stderr,
                    "IRIX dialog trace: callee=0x%08x:%s caller_pc=0x%08x ra=0x%08x this=0x%08x a1-a7=%08x,%08x,%08x,%08x,%08x,%08x,%08x\n",
                    address, entry->name ? entry->name : "?", caller_pc,
                    (uint32_t)cpu->gpr[31], (uint32_t)cpu->gpr[4],
                    (uint32_t)cpu->gpr[5], (uint32_t)cpu->gpr[6],
                    (uint32_t)cpu->gpr[7], (uint32_t)cpu->gpr[8],
                    (uint32_t)cpu->gpr[9], (uint32_t)cpu->gpr[10],
                    (uint32_t)cpu->gpr[11]);
        }

        /*
         * Any callable guest address must execute with the $gp of the ELF
         * image that owns that address.
         *
         * Do NOT key this on registration flags. The same guest entry can be
         * reached through translated registries, native bridge overrides,
         * import forwarders, recovered wrappers, or first-registration aliases.
         * Registration is an implementation detail; MIPS PIC ownership is not.
         *
         * We previously switched GP only for RECOMPILED entries. That allowed
         * a ViewKit/libvk entry around 0x0d635xxx to execute with libXt's
         * 0x0f69eef0 GP, causing a GOT load to return ASCII "mous"
         * (0x6d6f7573) and jalr into it.
         */
        callee_gp = irix_guest_gp_for_address(address);
        if (callee_gp)
            cpu->gpr[28] = (uint64_t)(int64_t)(int32_t)callee_gp;

        cpu->pc = address;
        rt->current_pc = address;
        /*
         * Only real named guest functions get N32 call-entry treatment.
         * Anonymous recovered ranges are translation coverage: entering one
         * at an interior PC is a control-flow continuation, not a new ABI
         * function call.  In particular, do not clobber t9 when dispatching
         * into an FDE/PDR/link/pointer-derived range.
         */
        if (entry->name)
            cpu->gpr[25] = (uint64_t)(int64_t)(int32_t)address;

        /*
         * SGI Motif gadget class-extension suspend/continue hooks use the
         * class slot at +0x60 as temporary SG state.  Keep that state separate
         * from Xt's own class-private/callback data: temporarily swap in a
         * persistent per-class scratch block only while the SG hook runs, then
         * restore the original pointer.
         */
        if (address == 0x0b2f6680u || address == 0x0b2f6690u) {
            unsigned i;
            sg_class = (IrixGuestAddr)(uint32_t)cpu->gpr[4];
            if (sg_class && irix_guest_ptr(rt, sg_class, 0x64u)) {
                sg_saved_private = irix_guest_read32be(rt, sg_class + 0x60u);
                for (i = 0; i < 16u; ++i) {
                    if (sg_drop_class[i] == sg_class) {
                        sg_state = sg_drop_state[i];
                        break;
                    }
                    if (!sg_drop_class[i]) {
                        sg_state = irix_guest_xt_compat_alloc(rt, 64u);
                        if (sg_state && irix_guest_ptr(rt, sg_state, 64u)) {
                            memset(irix_guest_ptr(rt, sg_state, 64u), 0, 64u);
                            sg_drop_class[i] = sg_class;
                            sg_drop_state[i] = sg_state;
                        }
                        break;
                    }
                }
                if (sg_state) {
                    irix_guest_write32be(rt, sg_class + 0x60u, sg_state);
                    sg_drop_swap = 1;
                    fprintf(stderr,
                            "IRIX SG drop-transfer state swap: class=%08x saved=%08x scratch=%08x\n",
                            sg_class, sg_saved_private, sg_state);
                }
            }
        }

        if (xm_display_shell_call) {
            IrixGuestAddr trace_sp = (IrixGuestAddr)(uint32_t)cpu->gpr[29];
            if (xm_display_shell_depth++ == 0u)
                xm_display_bootstrap_widget = 0u;
            g_xt_alloc_widget_class_hint =
                (IrixGuestAddr)(uint32_t)cpu->gpr[6];
            fprintf(stderr,
                    "IRIX XmDisplay shell bootstrap begin depth=%u "
                    "pc=%08x sp=%08x a0-a7=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x "
                    "s0-s3=%08x,%08x,%08x,%08x "
                    "stk10-28=%08x,%08x,%08x,%08x,%08x,%08x,%08x\n",
                    xm_display_shell_depth, caller_pc, trace_sp,
                    (uint32_t)cpu->gpr[4], (uint32_t)cpu->gpr[5],
                    (uint32_t)cpu->gpr[6], (uint32_t)cpu->gpr[7],
                    (uint32_t)cpu->gpr[8], (uint32_t)cpu->gpr[9],
                    (uint32_t)cpu->gpr[10], (uint32_t)cpu->gpr[11],
                    (uint32_t)cpu->gpr[16], (uint32_t)cpu->gpr[17],
                    (uint32_t)cpu->gpr[18], (uint32_t)cpu->gpr[19],
                    irix_guest_read32be(rt, trace_sp + 0x10u),
                    irix_guest_read32be(rt, trace_sp + 0x14u),
                    irix_guest_read32be(rt, trace_sp + 0x18u),
                    irix_guest_read32be(rt, trace_sp + 0x1cu),
                    irix_guest_read32be(rt, trace_sp + 0x20u),
                    irix_guest_read32be(rt, trace_sp + 0x24u),
                    irix_guest_read32be(rt, trace_sp + 0x28u));
        }

        entry->function(rt, cpu);

        if (sg_drop_swap && sg_class &&
            irix_guest_ptr(rt, sg_class + 0x60u, 4u)) {
            irix_guest_write32be(rt, sg_class + 0x60u, sg_saved_private);
            fprintf(stderr,
                    "IRIX SG drop-transfer state restore: class=%08x private=%08x\n",
                    sg_class, sg_saved_private);
        }

        /*
         * Repair the public XmGetXmScreen return as before. Interior entries
         * in the same machine function are handled in irix_guest_call().
         */
        if (!rt->trapped && address == 0x0b2a80acu)
            irix_guest_repair_xm_screen_private(
                rt, (IrixGuestAddr)(uint32_t)cpu->gpr[2]);

        if (xm_display_shell_call) {
            IrixGuestAddr trace_sp = (IrixGuestAddr)(uint32_t)cpu->gpr[29];
            if (xm_display_shell_depth)
                --xm_display_shell_depth;
            fprintf(stderr,
                    "IRIX XmDisplay shell bootstrap end depth=%u widget=%08x "
                    "v0=%08x pc=%08x sp=%08x "
                    "a0-a3=%08x,%08x,%08x,%08x "
                    "s0-s3=%08x,%08x,%08x,%08x "
                    "stk10-28=%08x,%08x,%08x,%08x,%08x,%08x,%08x\n",
                    xm_display_shell_depth, xm_display_bootstrap_widget,
                    (uint32_t)cpu->gpr[2], (uint32_t)cpu->pc, trace_sp,
                    (uint32_t)cpu->gpr[4], (uint32_t)cpu->gpr[5],
                    (uint32_t)cpu->gpr[6], (uint32_t)cpu->gpr[7],
                    (uint32_t)cpu->gpr[16], (uint32_t)cpu->gpr[17],
                    (uint32_t)cpu->gpr[18], (uint32_t)cpu->gpr[19],
                    irix_guest_read32be(rt, trace_sp + 0x10u),
                    irix_guest_read32be(rt, trace_sp + 0x14u),
                    irix_guest_read32be(rt, trace_sp + 0x18u),
                    irix_guest_read32be(rt, trace_sp + 0x1cu),
                    irix_guest_read32be(rt, trace_sp + 0x20u),
                    irix_guest_read32be(rt, trace_sp + 0x24u),
                    irix_guest_read32be(rt, trace_sp + 0x28u));
            if (!xm_display_shell_depth) {
                xm_display_bootstrap_widget = 0u;
                g_xt_alloc_widget_class_hint = 0u;
            }
        }

        /*
         * Emergency SGI libXm compatibility path.
         *
         * The static XmDisplay GetDisplay() entry at 0x0b294240 is split by
         * SGI .debug_frame metadata at the interior address 0x0b2942b0.
         * Some generated bundles therefore return from the first recovered
         * range at 0x2b0 even though the machine function continues through
         * 0x0b29434f.  Continue directly into the adjacent recovered range
         * BEFORE restoring the caller's stack/callee-saved state so the live
         * GetDisplay frame is preserved.
         */
        if (!rt->trapped &&
            (caller_pc == 0x0b297ca0u || address == 0x0b294240u) &&
            (uint32_t)cpu->gpr[2] >= 0x74300000u &&
            (uint32_t)cpu->gpr[2] <  0x74400000u) {
            IrixGuestFunctionEntry *cont =
                irix_guest_find_function(rt, 0x0b2942b0u);
            if (cont && cont->function) {
                fprintf(stderr,
                        "IRIX Xm GetDisplay continuation: 0x0b294240 -> 0x0b2942b0\n");
                cpu->pc = 0x0b2942b0u;
                rt->current_pc = 0x0b2942b0u;
                cont->function(rt, cpu);
            } else {
                fprintf(stderr,
                        "IRIX Xm GetDisplay continuation missing at 0x0b2942b0\n");
            }
        }


        /* A completed N32 call must return with the caller's callee-saved
           register set and stack pointer intact. Do not rewrite state on a
           trap: the fault report needs the exact machine state at failure. */
        if (!rt->trapped) {
            for (saved_i = 0; saved_i < 8u; ++saved_i)
                cpu->gpr[16u + saved_i] = caller_saved[saved_i];
            cpu->gpr[28] = caller_gp;
            cpu->gpr[29] = caller_sp;
            cpu->gpr[30] = caller_saved[8];
        }
    }
    cpu->gpr[0] = 0;
    return rt->trapped ? -1 : 0;
}

int irix_guest_call_entry(IrixGuestRuntime *rt, IrixGuestCpu *cpu,
                          IrixGuestAddr address, IrixGuestFunctionEntry *entry)
{
    IrixGuestAddr caller;
    if (!entry || entry->address != address)
        return irix_guest_call(rt, cpu, address);
    caller = rt->current_pc;
    irix_guest_profile_hit(rt, caller, address, entry);
    if (irix_mips_jit_requested() &&
        !(entry->flags & IRIX_GUEST_FUNCTION_NATIVE))
        return irix_mips_jit_call(rt, cpu, address);
    return irix_guest_call_entry_impl(rt, cpu, address, entry);
}

int irix_guest_call(IrixGuestRuntime *rt, IrixGuestCpu *cpu, IrixGuestAddr address)
{
    IrixGuestAddr caller = rt->current_pc;
    IrixGuestFunctionEntry *entry = irix_guest_find_function(rt, address);
    irix_guest_profile_hit(rt, caller, address, entry);

    /*
     * An interior PC is not interchangeable with the containing translated
     * function's entry. Generated C functions begin at their own entry label
     * and do not reconstruct execution from cpu->pc. SGI libraries frequently
     * materialize hidden callback/leaf entry points inside a larger nominal
     * ELF symbol (Xt qsort comparators are one concrete case).
     *
     * When a function pointer names such an interior PC, execute the mapped
     * original MIPS at that exact address. This preserves the real callback
     * entry semantics instead of silently rewinding execution to the enclosing
     * translated function's start.
     */
    if (entry && entry->address != address &&
        !(entry->flags & IRIX_GUEST_FUNCTION_NATIVE)) {
        uint64_t saved_s[9];
        uint64_t saved_gp = cpu->gpr[28];
        uint64_t saved_sp = cpu->gpr[29];
        uint64_t saved_ra = cpu->gpr[31];
        IrixGuestAddr saved_a0 = (IrixGuestAddr)(uint32_t)cpu->gpr[4];
        unsigned saved_i;
        IrixGuestAddr target_gp = irix_guest_gp_for_address(address);
        int rc;

        /*
         * Interior callback/leaf entries are still entered through a real
         * N32 call boundary. The interpreter may use s0-s7 and s8/fp
         * internally, but those registers (plus sp/gp) belong to the caller
         * when control returns. Preserving only gp here leaked interpreter
         * state back into translated Motif callers; one observed consequence
         * was _XmBuildResources receiving stack addresses in s1/s2 and
         * turning them into a 0xc00d834c malloc.
         */
        for (saved_i = 0; saved_i < 8u; ++saved_i)
            saved_s[saved_i] = cpu->gpr[16u + saved_i];
        saved_s[8] = cpu->gpr[30];

        fprintf(stderr,
                "IRIX interior guest call: target=0x%08x containing=0x%08x:%s -> interpreter\n",
                address, entry->address, entry->name ? entry->name : "?");
        if (target_gp)
            cpu->gpr[28] = (uint64_t)(int64_t)(int32_t)target_gp;
        rc = irix_mips_interp_call(rt, cpu, address);
        if (!rt->trapped) {
            /*
             * xtWidgetAlloc's extension allocator returns to 0x0f62b578.
             * XtAllocateProc's first argument is the WidgetClass itself.
             * Preserve that incoming a0 across the interpreted callback and
             * use it for the fallback; s0 belongs to the Motif allocator and
             * was observed holding 0x0b3e4ad8 rather than the actual class
             * 0x0b3f0890, causing the fallback's widget-size guard to reject
             * the repair and leaving sp+0x20 NULL.
             */
            if ((uint32_t)saved_ra == 0x0f62b578u ||
                (uint32_t)cpu->gpr[31] == 0x0f62b578u) {
                IrixGuestAddr alloc_class = saved_a0;

                if (!irix_guest_looks_like_widget_class(rt, alloc_class) &&
                    irix_guest_looks_like_widget_class(
                        rt, g_xt_alloc_widget_class_hint)) {
                    fprintf(stderr,
                            "IRIX xtWidgetAlloc class repair: "
                            "a0=%08x -> shell-class=%08x\n",
                            alloc_class, g_xt_alloc_widget_class_hint);
                    alloc_class = g_xt_alloc_widget_class_hint;
                }

                (void)irix_guest_repair_xt_widget_alloc(
                    rt, cpu, (IrixGuestAddr)(uint32_t)saved_sp,
                    alloc_class);
            }

            /*
             * XmGetXmScreen spans 0x0b2a80ac..0x0b2a8388 in this recovered
             * SGI libXm image. Calls can legitimately target an interior
             * continuation (observed: 0x0b2a8300), so apply the same return
             * repair that the exact public entry receives.
             */
            if ((address >= 0x0b2a80acu && address < 0x0b2a8388u) ||
                entry->address == 0x0b2a80acu ||
                (entry->name && strcmp(entry->name, "XmGetXmScreen") == 0))
                irix_guest_repair_xm_screen_private(
                    rt, (IrixGuestAddr)(uint32_t)cpu->gpr[2]);

            for (saved_i = 0; saved_i < 8u; ++saved_i)
                cpu->gpr[16u + saved_i] = saved_s[saved_i];
            cpu->gpr[28] = saved_gp;
            cpu->gpr[29] = saved_sp;
            cpu->gpr[30] = saved_s[8];
        }
        return rc;
    }

    if (irix_mips_jit_requested() &&
        (!entry || !(entry->flags & IRIX_GUEST_FUNCTION_NATIVE)))
        return irix_mips_jit_call(rt, cpu, address);
    return irix_guest_call_entry_impl(rt, cpu, address, entry);
}
