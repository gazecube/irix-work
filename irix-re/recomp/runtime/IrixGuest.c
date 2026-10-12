#include "IrixGuest.h"
#include "IrixKernelInfo.h"
#include "IrixMipsInterp.h"
#include "IrixMipsJit.h"
#include "IrixCompat.h"
#include "IrixSyscallNumbers.h"
#include "IrixX11BridgeExtraAccess.h"
#include "IrixXrmQuarkBridge.h"

#include <errno.h>
#include <elf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sched.h>
#ifndef SIG_BLOCK
#define SIG_BLOCK 0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2
#endif
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/uio.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <asm/unistd.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <sys/times.h>
#include <fcntl.h>
#include <limits.h>
#include <time.h>

extern char **environ;

/* Runtime-private guest-libc helpers used by narrow native compatibility
   implementations. Keep them out of IrixGuest.h to avoid rebuilding every
   generated translation unit. */
extern IrixGuestAddr irix_libc_guest_malloc(uint32_t size);



#define IRIX_NSIG 128u
#define IRIX_SIGACTION_N32_SIZE 32u
#define IRIX_SIGALRM 14u

typedef struct IrixKernelSigAction {
    uint32_t flags;
    uint32_t handler;
    uint32_t mask[4];
    uint32_t sigtramp;
} IrixKernelSigAction;

static IrixKernelSigAction g_kernel_sigactions[IRIX_NSIG];

static uint64_t monotonic_time_ns(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

int irix_guest_poll_alarm(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    uint64_t now;
    uint32_t handler;
    IrixGuestCpu saved;
    IrixGuestAddr saved_pc;

    if (!rt || !cpu || !rt->alarm_deadline_ns)
        return 0;
    now = monotonic_time_ns();
    if (!now || now < rt->alarm_deadline_ns)
        return 0;

    /* An alarm is one-shot even when its guest handler rearms it. */
    rt->alarm_deadline_ns = 0;
    handler = g_kernel_sigactions[IRIX_SIGALRM].handler;
    if (handler == 1u) /* SIG_IGN */
        return 0;
    if (!handler) { /* SIG_DFL terminates with the signal status. */
        cpu->gpr[2] = 128u + IRIX_SIGALRM;
        cpu->gpr[7] = 0;
        rt->fault_addr = 128u + IRIX_SIGALRM;
        irix_guest_trap(rt, cpu, 0xeeee23ffu);
        return -1;
    }

    /*
     * The interpreter has no asynchronous host signal frame. Invoke the guest
     * handler synchronously at an instruction boundary, then restore the
     * interrupted register context. This keeps a MIPS handler address out of
     * host sigaction() and covers guest alarm-driven timeout paths.
     */
    saved = *cpu;
    saved_pc = rt->current_pc;
    cpu->gpr[4] = IRIX_SIGALRM;
    if (irix_guest_call(rt, cpu, handler) != 0 || rt->trapped)
        return -1;
    *cpu = saved;
    rt->current_pc = saved_pc;
    return 0;
}

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

static int read_irix_sockaddr(IrixGuestRuntime *rt, IrixGuestAddr addr,
                              socklen_t length,
                              struct sockaddr_storage *out)
{
    uint8_t *guest;
    uint16_t family;
    size_t copy_length;

    if (!addr || length < 2u || length > sizeof(*out)) {
        errno = EINVAL;
        return -1;
    }
    guest = (uint8_t *)irix_guest_ptr(rt, addr, length);
    if (!guest) {
        errno = EFAULT;
        return -1;
    }
    family = irix_guest_read16be(rt, addr);
    if (rt->trapped)
        return -1;
    memset(out, 0, sizeof(*out));
    copy_length = length;
    memcpy(out, guest, copy_length);

    /* IRIX sockaddr families use big-endian ABI values. Linux's local
       families match, while IRIX AF_INET6 (26) maps to Linux AF_INET6 (10). */
    if (family == 24u)
        family = AF_INET6;
    else if (family != AF_UNIX && family != AF_INET) {
        errno = EAFNOSUPPORT;
        return -1;
    }
    ((struct sockaddr *)out)->sa_family = family;
    return 0;
}

static int host_socket_domain_from_irix(int domain)
{
    switch (domain) {
    case 0: return AF_UNSPEC;
    case 1: return AF_UNIX;
    case 2: return AF_INET;
    case 24:
#ifdef AF_INET6
        return AF_INET6;
#else
        return -1;
#endif
    default: return -1;
    }
}

static int host_socket_type_from_irix(int type)
{
    switch (type) {
    case 1: return SOCK_DGRAM;
    case 2: return SOCK_STREAM;
    case 3: return SOCK_STREAM; /* SOCK_STREAM | orderly release */
    case 4: return SOCK_RAW;
#ifdef SOCK_RDM
    case 5: return SOCK_RDM;
#endif
#ifdef SOCK_SEQPACKET
    case 6: return SOCK_SEQPACKET;
#endif
    default: return -1;
    }
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

static int guest_range_mapped(IrixGuestRuntime *rt, IrixGuestAddr addr,
                              size_t size)
{
    uint64_t cursor = addr;
    uint64_t end = cursor + (uint64_t)size;

    if (end > UINT32_MAX + 1ull || end < cursor)
        return 0;
    while (cursor < end) {
        IrixGuestSegment *seg = find_segment(rt, (IrixGuestAddr)cursor, 1u);
        uint64_t seg_end;
        if (!seg)
            return 0;
        seg_end = (uint64_t)seg->guest_base + seg->size;
        cursor = seg_end < end ? seg_end : end;
    }
    return 1;
}

static int guest_copy_to_host(IrixGuestRuntime *rt, IrixGuestAddr addr,
                              void *host, size_t size)
{
    uint64_t cursor = addr;
    size_t copied = 0;

    if (!guest_range_mapped(rt, addr, size))
        return -1;
    while (copied < size) {
        IrixGuestSegment *seg = find_segment(rt, (IrixGuestAddr)cursor, 1u);
        uint64_t seg_end = (uint64_t)seg->guest_base + seg->size;
        size_t chunk = (size_t)(seg_end - cursor);
        if (chunk > size - copied)
            chunk = size - copied;
        memcpy((uint8_t *)host + copied,
               seg->host_base + (size_t)(cursor - seg->guest_base), chunk);
        copied += chunk;
        cursor += chunk;
    }
    return 0;
}

static int guest_copy_from_host(IrixGuestRuntime *rt, IrixGuestAddr addr,
                                const void *host, size_t size)
{
    uint64_t cursor = addr;
    size_t copied = 0;

    if (!guest_range_mapped(rt, addr, size))
        return -1;
    while (copied < size) {
        IrixGuestSegment *seg = find_segment(rt, (IrixGuestAddr)cursor, 1u);
        uint64_t seg_end = (uint64_t)seg->guest_base + seg->size;
        size_t chunk = (size_t)(seg_end - cursor);
        if (chunk > size - copied)
            chunk = size - copied;
        memcpy(seg->host_base + (size_t)(cursor - seg->guest_base),
               (const uint8_t *)host + copied, chunk);
        copied += chunk;
        cursor += chunk;
    }
    return 0;
}

#define IRIX_GUEST_IOV_MAX 1024u

static int irix_guest_iov(IrixGuestRuntime *rt, IrixGuestAddr vector_addr,
                          uint32_t vector_count, int guest_to_host,
                          struct iovec **out_iov, void ***out_storage)
{
    struct iovec *iov;
    void **storage;
    uint64_t total = 0;
    uint32_t i;

    *out_iov = NULL;
    *out_storage = NULL;
    if (vector_count > IRIX_GUEST_IOV_MAX ||
        (vector_count && !guest_range_mapped(
            rt, vector_addr, (size_t)vector_count * 8u))) {
        errno = EFAULT;
        return -1;
    }
    iov = calloc(vector_count ? vector_count : 1u, sizeof(*iov));
    storage = calloc(vector_count ? vector_count : 1u, sizeof(*storage));
    if (!iov || !storage) {
        free(iov);
        free(storage);
        errno = ENOMEM;
        return -1;
    }

    for (i = 0; i < vector_count; ++i) {
        IrixGuestAddr entry = vector_addr + (IrixGuestAddr)(i * 8u);
        IrixGuestAddr base = irix_guest_read32be(rt, entry);
        uint32_t length = irix_guest_read32be(rt, entry + 4u);
        void *buffer;

        if (rt->trapped || !guest_range_mapped(rt, base, length)) {
            errno = EFAULT;
            goto fail;
        }
        total += length;
        if (total > (uint64_t)SSIZE_MAX) {
            errno = EINVAL;
            goto fail;
        }
        buffer = malloc(length ? length : 1u);
        if (!buffer) {
            errno = ENOMEM;
            goto fail;
        }
        storage[i] = buffer;
        iov[i].iov_base = buffer;
        iov[i].iov_len = length;
        if (guest_to_host && length &&
            guest_copy_to_host(rt, base, buffer, length) != 0) {
            errno = EFAULT;
            goto fail;
        }
    }
    *out_iov = iov;
    *out_storage = storage;
    return 0;

fail:
    for (i = 0; i < vector_count; ++i)
        free(storage[i]);
    free(storage);
    free(iov);
    return -1;
}

static void irix_guest_iov_free(struct iovec *iov, void **storage,
                               uint32_t vector_count)
{
    uint32_t i;
    if (storage)
        for (i = 0; i < vector_count; ++i)
            free(storage[i]);
    free(storage);
    free(iov);
}

int64_t irix_guest_read_fd(IrixGuestRuntime *rt, int fd,
                           IrixGuestAddr addr, size_t count)
{
    uint64_t cursor = addr;
    size_t total = 0;

    if (!count)
        return 0;
    if (!guest_range_mapped(rt, addr, count)) {
        errno = EFAULT;
        return -1;
    }
    while (total < count) {
        IrixGuestSegment *seg = find_segment(rt, (IrixGuestAddr)cursor, 1u);
        uint64_t seg_end = (uint64_t)seg->guest_base + seg->size;
        size_t chunk = (size_t)(seg_end - cursor);
        ssize_t n;
        if (chunk > count - total)
            chunk = count - total;
        n = read(fd, seg->host_base + (size_t)(cursor - seg->guest_base),
                 chunk);
        if (n < 0)
            return total ? (int64_t)total : -1;
        total += (size_t)n;
        cursor += (size_t)n;
        if ((size_t)n < chunk)
            break;
    }
    return (int64_t)total;
}

int64_t irix_guest_write_fd(IrixGuestRuntime *rt, int fd,
                            IrixGuestAddr addr, size_t count)
{
    uint64_t cursor = addr;
    size_t total = 0;

    if (!count)
        return 0;
    if (!guest_range_mapped(rt, addr, count)) {
        errno = EFAULT;
        return -1;
    }
    while (total < count) {
        IrixGuestSegment *seg = find_segment(rt, (IrixGuestAddr)cursor, 1u);
        uint64_t seg_end = (uint64_t)seg->guest_base + seg->size;
        size_t chunk = (size_t)(seg_end - cursor);
        ssize_t n;
        if (chunk > count - total)
            chunk = count - total;
        n = write(fd, seg->host_base + (size_t)(cursor - seg->guest_base),
                  chunk);
        if (n < 0)
            return total ? (int64_t)total : -1;
        total += (size_t)n;
        cursor += (size_t)n;
        if ((size_t)n < chunk)
            break;
    }
    return (int64_t)total;
}

const char *irix_guest_cstr(IrixGuestRuntime *rt, IrixGuestAddr addr, size_t max_len)
{
    IrixGuestSegment *seg;
    size_t off;
    size_t avail;
    size_t n;
    const char *s;

    if (!rt || !addr || !max_len)
        return 0;
    seg = find_segment(rt, addr, 1u);
    if (!seg)
        return 0;
    off = (size_t)(addr - seg->guest_base);
    avail = (size_t)seg->size - off;
    n = avail < max_len ? avail : max_len;
    s = (const char *)seg->host_base + off;
    return memchr(s, '\0', n) ? s : 0;
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
static IrixGuestAddr g_xt_alloc_widget_hint;

IrixGuestAddr irix_guest_xt_compat_alloc(IrixGuestRuntime *rt, uint32_t size)
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

static uint32_t irix_guest_xrm_quark_literal(
    IrixGuestRuntime *rt, const char *literal)
{
    size_t n;
    IrixGuestAddr guest;
    char *dst;

    if (!rt || !literal || !rt->xrm_quarkify_encoded)
        return 0u;

    n = strlen(literal) + 1u;
    if (n > UINT32_MAX)
        return 0u;

    guest = irix_guest_xt_compat_alloc(rt, (uint32_t)n);
    if (!guest)
        return 0u;

    dst = (char *)irix_guest_ptr(rt, guest, n);
    if (!dst)
        return 0u;

    memcpy(dst, literal, n);
    return rt->xrm_quarkify_encoded(rt, guest);
}

void irix_guest_repair_xm_screen_private(IrixGuestRuntime *rt,
                                         IrixGuestAddr screen)
{
    IrixGuestAddr priv;
    IrixGuestAddr old_priv;

    /*
     * SGI Motif XmScreen carries a private pointer at +0x104. The original
     * instance-initialize path allocates an 8-byte zeroed block there.
     * Recovered/interpreted continuation entries can bypass the dispatcher
     * hook that used to repair only the public XmGetXmScreen entry.
     *
     * Do not treat merely-nonzero as initialized. Hybrid ABI damage can leave
     * a stack token or stale scalar in pointer fields. Accept an existing
     * private block only when all 8 bytes are actually guest-mapped.
     */
    if (!screen || !irix_guest_ptr(rt, screen, 0x108u))
        return;

    old_priv = irix_guest_read32be(rt, screen + 0x104u);
    if (old_priv && irix_guest_ptr(rt, old_priv, 8u))
        return;

    priv = irix_guest_xt_compat_alloc(rt, 8u);
    if (!priv || !irix_guest_ptr(rt, priv, 8u))
        return;

    irix_guest_write32be(rt, priv + 0u, 0u);
    irix_guest_write32be(rt, priv + 4u, 0u);
    irix_guest_write32be(rt, screen + 0x104u, priv);
    (void)0;
}

static int irix_guest_xt_resources_are_indirect(IrixGuestRuntime *rt,
                                                 IrixGuestAddr resources,
                                                 uint32_t num_resources)
{
    uint32_t i;

    /*
     * Do not classify an XtResource[] as XrmResourceList[] from the first
     * word alone.  In a raw XtResource that word is resource_name (a String
     * pointer), so a mapped string followed by coincidentally-small data can
     * satisfy a one-record pointer heuristic and make the class-resource
     * repair reinterpret the raw structure as a pointer table.
     *
     * An actual XrmResourceList[] must be a mapped array of pointers and every
     * element must point at a complete, plausibly-sized compiled resource.
     */
    if (!resources || !num_resources || num_resources > 4096u)
        return 0;
    if (!irix_guest_ptr(rt, resources, (size_t)num_resources * 4u))
        return 0;

    for (i = 0; i < num_resources; ++i) {
        IrixGuestAddr res =
            irix_guest_read32be(rt, resources + i * 4u);
        uint32_t size;

        if (!res || !irix_guest_ptr(rt, res, IRIX_XT_RESOURCE_SIZE))
            return 0;
        size = irix_guest_read32be(rt, res + 12u);
        if (!size || size > 0x10000u)
            return 0;
    }
    return 1;
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
    if (class_num > 4096u) {
        fprintf(stderr,
                "IRIX Xt class-resource repair: class=%08x "
                "invalid resource count=%u\n",
                wc, class_num);
        return;
    }
    resources_indirect =
        irix_guest_xt_resources_are_indirect(rt, resources, class_num);
    if (rt->xrm_quarkify_encoded)
        irix_xt_q_callback =
            rt->xrm_quarkify_encoded(rt, irix_xt_r_callback);

    if (sc) {
        if (!irix_guest_ptr(rt, sc, 100u)) {
            fprintf(stderr,
                    "IRIX Xt class-resource repair: class=%08x "
                    "invalid superclass=%08x\n",
                    wc, sc);
            return;
        }

        super_widget_size = irix_guest_read32be(rt, sc + 8u);
        if (!super_widget_size || super_widget_size > 0x01000000u) {
            (void)0;
            return;
        }

        irix_guest_xt_repair_class_resources_inner(rt, sc, depth + 1u);

        super_resources = irix_guest_read32be(rt, sc + 44u);
        super_num = irix_guest_read32be(rt, sc + 48u);
        if (super_num > 4096u ||
            (super_num &&
             !irix_guest_xt_resources_are_indirect(
                 rt, super_resources, super_num))) {
            fprintf(stderr,
                    "IRIX Xt class-resource repair: class=%08x "
                    "superclass=%08x invalid resources=%08x count=%u\n",
                    wc, sc, super_resources, super_num);
            return;
        }

        super_callbacks =
            irix_guest_read32be(rt, sc + IRIX_XT_CALLBACK_PRIVATE);
        if (super_callbacks && irix_guest_ptr(rt, super_callbacks, 4u)) {
            uint32_t n = irix_guest_read32be(rt, super_callbacks);
            uint64_t bytes = ((uint64_t)n + 1u) * 4u;
            if (n <= 4096u &&
                irix_guest_ptr(rt, super_callbacks, (size_t)bytes))
                super_callback_num = n;
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

        {
            uint64_t count = (uint64_t)super_num + (uint64_t)class_num;
            if (count > 4096u)
                return;
            table = irix_guest_xt_compat_alloc(rt, (uint32_t)count * 4u);
        }
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
    {
        IrixGuestAddr existing =
            irix_guest_read32be(rt, wc + IRIX_XT_CALLBACK_PRIVATE);
        if (existing) {
            uint32_t count = 0u;
            uint64_t bytes = 0u;
            int valid = 0;

            if (irix_guest_ptr(rt, existing, 4u)) {
                uint32_t k;
                count = irix_guest_read32be(rt, existing);
                bytes = ((uint64_t)count + 1u) * 4u;
                if (count <= 4096u &&
                    irix_guest_ptr(rt, existing, (size_t)bytes)) {
                    valid = 1;
                    for (k = 0; k < count; ++k) {
                        IrixGuestAddr res =
                            irix_guest_read32be(rt,
                                existing + (k + 1u) * 4u);
                        if (!res ||
                            !irix_guest_ptr(rt, res, IRIX_XT_RESOURCE_SIZE)) {
                            valid = 0;
                            break;
                        }
                    }
                }
            }

            if (valid)
                return;

            fprintf(stderr,
                    "IRIX Xt callback_private repair: "
                    "class=%08x invalid=%08x count=%u -> rebuild\n",
                    wc, existing, count);
            irix_guest_write32be(rt, wc + IRIX_XT_CALLBACK_PRIVATE, 0u);
        }
    }

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

/*
 * IRIX maps a readable zero page at address 0 (the "usenull" behaviour):
 * loading through a NULL pointer, or a small offset from one, yields zero
 * instead of faulting.  SGI code relies on it, e.g. tuFont::getFont reads the
 * first byte of strdup(NULL) expecting an empty string.  Only guest *loads*
 * see the zero page; stores still fault and irix_guest_ptr() still returns
 * NULL there, so native bridges keep treating NULL as an invalid pointer.
 */
#define IRIX_GUEST_NULL_PAGE_SIZE 0x4000u

static int guest_null_page_read(IrixGuestAddr addr)
{
    static int enabled = -1;
    if (enabled < 0) {
        const char *v = getenv("IRIX_NULL_PAGE");
        enabled = !(v && v[0] == '0');
    }
    return enabled && addr < IRIX_GUEST_NULL_PAGE_SIZE;
}

uint8_t irix_guest_read8(IrixGuestRuntime *rt, IrixGuestAddr addr)
{
    uint8_t *p = (uint8_t *)irix_guest_ptr(rt, addr, 1);
    if (!p) {
        if (guest_null_page_read(addr)) return 0;
        memory_fault(rt, addr);
        return 0;
    }
    return *p;
}

uint16_t irix_guest_read16be(IrixGuestRuntime *rt, IrixGuestAddr addr)
{
    uint8_t *p = (uint8_t *)irix_guest_ptr(rt, addr, 2);
    if (!p) {
        if (guest_null_page_read(addr)) return 0;
        memory_fault(rt, addr);
        return 0;
    }
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

uint32_t irix_guest_read32be(IrixGuestRuntime *rt, IrixGuestAddr addr)
{
    uint8_t *p;
    if (rt->errno_proxy_addr && addr == rt->errno_proxy_addr)
        return (uint32_t)(int32_t)errno;
    p = (uint8_t *)irix_guest_ptr(rt, addr, 4);
    if (!p) {
        if (guest_null_page_read(addr)) return 0;
        memory_fault(rt, addr);
        return 0;
    }
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

void irix_guest_write8(IrixGuestRuntime *rt, IrixGuestAddr addr, uint8_t value)
{
    uint8_t *p;
    p = (uint8_t *)irix_guest_ptr(rt, addr, 1);
    if (!p) { memory_fault(rt, addr); return; }
    *p = value;
}

void irix_guest_write16be(IrixGuestRuntime *rt, IrixGuestAddr addr, uint16_t value)
{
    uint8_t *p;
    p = (uint8_t *)irix_guest_ptr(rt, addr, 2);
    if (!p) { memory_fault(rt, addr); return; }
    p[0] = (uint8_t)(value >> 8); p[1] = (uint8_t)value;
}

void irix_guest_write32be(IrixGuestRuntime *rt, IrixGuestAddr addr, uint32_t value)
{
    uint8_t *p;
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
    size_t prior_i;

    if (!function || rt->function_count >= IRIX_GUEST_MAX_FUNCTIONS)
        return -1;

    /*
     * Anonymous recovered entries (__debug_frame/__pdr/__ptr/__link/
     * __boundary and similar) are translation coverage, not authoritative
     * N32 function boundaries.
     *
     * They may be nested inside another recovered range, but they may also be
     * adjacent fragments of one real machine function.  One concrete libXm
     * case is 0x0b37f660: code-link recovery emitted only the first two
     * instructions, while a debug-frame range beginning at 0x0b37f668 owns
     * the continuation.  Treating the 8-byte fragment as a complete function
     * returned stale v0 and later turned ordinary pointer arithmetic into an
     * unmapped store.
     *
     * Therefore every anonymous recompiled range is interpreter-only when
     * reached as a guest call.  Named ELF symbols remain normal recompiled
     * ABI entry points.  Generated code may still use anonymous ranges as
     * local coverage labels inside its own recovered CFG.
     */
    if ((flags & IRIX_GUEST_FUNCTION_RECOMPILED) && !name && size)
        flags |= IRIX_GUEST_FUNCTION_INTERIOR;

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
    /*
     * IRIX N32's ordinary `struct stat` is not a 32-bit structure.  dev_t,
     * mode_t, uid_t, etc. are 32-bit, but ino_t and off_t remain 64-bit.
     * Keep the kernel xstat(2) version-2 wire layout from sys/stat.h:
     *   ino @16, size @56, times @68, blksize @92, blocks @96.
     * In particular, libc's _findbuf reads st_blksize at +92 while setting
     * up the FILE buffer; writing it at +80 silently makes regular-file
     * fgetc fail before it ever issues read(2).
     */
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
    memcpy(p + 104u, "efs", 4u);
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
    if (rt && rt->rootfs && strncmp(guest, "/usr/lib32/", 11u) == 0) {
        const char *name = guest + 11u;
        if (*name && !strchr(name, '/')) {
            n = snprintf(buf, bufsz, "%s/usr/lib32/mips4/%s",
                         rt->rootfs, name);
            if (n > 0 && (size_t)n < bufsz && stat(buf, &st) == 0) {
                (void)0;
                return buf;
            }

            n = snprintf(buf, bufsz, "%s/usr/lib32/mips3/%s",
                         rt->rootfs, name);
            if (n > 0 && (size_t)n < bufsz && stat(buf, &st) == 0) {
                (void)0;
                return buf;
            }
        }
    }

    return irix_guest_resolve_path(rt, guest, buf, bufsz,
                                   "guest syscall", 0);
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

/* Identify guest ELF images and interpreter scripts before handing execve to
   the host kernel. IRIX media trees are not executable on the host, but the
   generic guest runner can load the original paths and run their MIPS code. */
static int irix_guest_exec_kind(const char *host_path, char *interpreter,
                                size_t interpreter_size, char *script_arg,
                                size_t script_arg_size)
{
    unsigned char header[4096];
    FILE *file;
    size_t n;

    if (!host_path) return 0;
    file = fopen(host_path, "rb");
    if (!file) return 0;
    n = fread(header, 1u, sizeof(header), file);
    fclose(file);

    if (n >= 2u && header[0] == '#' && header[1] == '!') {
        size_t i = 2u, start, end, arg_start;
        while (i < n && (header[i] == ' ' || header[i] == '\t')) ++i;
        start = i;
        while (i < n && header[i] != ' ' && header[i] != '\t' &&
               header[i] != '\r' && header[i] != '\n') ++i;
        end = i;
        if (end == start || end - start >= interpreter_size) return 0;
        memcpy(interpreter, header + start, end - start);
        interpreter[end - start] = '\0';
        while (i < n && (header[i] == ' ' || header[i] == '\t')) ++i;
        arg_start = i;
        while (i < n && header[i] != '\r' && header[i] != '\n') ++i;
        while (i > arg_start && (header[i - 1u] == ' ' ||
                                 header[i - 1u] == '\t')) --i;
        if (script_arg_size) {
            size_t arg_len = i - arg_start;
            if (arg_len >= script_arg_size) return 0;
            memcpy(script_arg, header + arg_start, arg_len);
            script_arg[arg_len] = '\0';
        }
        return 2;
    }

    if (n >= 52u && header[0] == 0x7fu && header[1] == 'E' &&
        header[2] == 'L' && header[3] == 'F' &&
        header[EI_CLASS] == ELFCLASS32 && header[EI_DATA] == ELFDATA2MSB &&
        header[18] == 0 && header[19] == EM_MIPS)
        return 1;
    return 0;
}

static void irix_free_host_vector(char **vector)
{
    size_t i;
    if (!vector) return;
    for (i = 0; vector[i]; ++i) free(vector[i]);
    free(vector);
}

static char **irix_exec_environment(char *const base[], const char *guest_path)
{
    size_t count = 0, i, used = 0;
    char **out;
    char *path_entry;
    char *backend_entry;
    size_t path_len;
    size_t backend_len;
    static const char exec_key[] = "IRIX_GUEST_EXEC_PATH=";
    const char *backend = getenv("IRIX_EXEC_BACKEND");
    if (!backend || !backend[0]) backend = "jit";

    if (base) while (base[count]) ++count;
    out = (char **)calloc(count + 3u, sizeof(*out));
    if (!out) return 0;
    for (i = 0; i < count; ++i) {
        if (!strncmp(base[i], "IRIX_GUEST_EXEC_PATH=", 21u) ||
            !strncmp(base[i], "IRIX_EXEC_BACKEND=", 18u))
            continue;
        out[used] = strdup(base[i]);
        if (!out[used++]) { irix_free_host_vector(out); return 0; }
    }
    path_len = sizeof(exec_key) - 1u + strlen(guest_path) + 1u;
    path_entry = (char *)malloc(path_len);
    if (!path_entry) {
        irix_free_host_vector(out);
        return 0;
    }
    snprintf(path_entry, path_len, "%s%s", exec_key, guest_path);
    out[used++] = path_entry;
    backend_len = strlen(backend) + sizeof("IRIX_EXEC_BACKEND=");
    backend_entry = (char *)malloc(backend_len);
    if (!backend_entry) { irix_free_host_vector(out); return 0; }
    snprintf(backend_entry, backend_len, "IRIX_EXEC_BACKEND=%s", backend);
    out[used] = backend_entry;
    return out;
}

/* Environment for a native child.
 *
 * The guest's own environment is often empty (Toolchest children, for
 * instance).  A native runner needs at least DISPLAY and friends; a host
 * program such as a terminal or the user's login shell needs the whole host
 * environment (PATH, ENV, LANG, ...) to behave as it does outside the
 * session.  host_env selects the second case: start from the host
 * environment, let variables the guest set override it, and always take
 * SHELL from the host (the guest sees /bin/sh). */
static int irix_env_has(char *const vec[], size_t count, const char *entry)
{
    const char *eq = strchr(entry, '=');
    size_t n = eq ? (size_t)(eq - entry) : strlen(entry), i;
    for (i = 0; i < count; ++i)
        if (!strncmp(vec[i], entry, n) && vec[i][n] == '=')
            return 1;
    return 0;
}

/* Variables the runners set for the *guest* that must not leak into host
 * programs: IRIX app-defaults carry SGI color names that host Xt programs
 * (xclock, ...) cannot resolve. */
static int irix_env_guest_only(const char *entry)
{
    size_t n = strlen("/usr/lib/X11/app-defaults");
    if (!strncmp(entry, "XAPPLRESDIR=", 12u)) {
        size_t len = strlen(entry);
        return len >= n && !strcmp(entry + len - n, "/usr/lib/X11/app-defaults");
    }
    if (!strncmp(entry, "XENVIRONMENT=", 13u))
        return strstr(entry, "/irix-desktop-resources-") != 0;
    return 0;
}

static char **irix_merge_host_env(char *const base[], int host_env)
{
    static const char *const keep[] = {
        "DISPLAY", "XAUTHORITY", "HOME", "USER", "LOGNAME", "LANG",
        "XDG_RUNTIME_DIR", "IRIX_RECOMP_ROOT", "IRIX_SESSION_SCRIPT", 0
    };
    extern char **environ;
    size_t bcount = 0, hcount = 0, used = 0, i, k;
    char **out;
    if (base) while (base[bcount]) ++bcount;
    if (host_env && environ) while (environ[hcount]) ++hcount;
    out = (char **)calloc(bcount + hcount + 16u, sizeof(*out));
    if (!out) return 0;
    for (i = 0; i < bcount; ++i) {
        if (host_env && !strncmp(base[i], "SHELL=", 6u) && getenv("SHELL"))
            continue;               /* replaced by the host value below */
        if (host_env && irix_env_guest_only(base[i]))
            continue;
        out[used] = strdup(base[i]);
        if (!out[used]) { irix_free_host_vector(out); return 0; }
        ++used;
    }
    if (host_env) {
        for (i = 0; i < hcount; ++i) {
            if (!strncmp(environ[i], "IRIX_GUEST_EXEC_PATH=", 21u) ||
                !strncmp(environ[i], "IRIX_EXEC_BACKEND=", 18u) ||
                irix_env_guest_only(environ[i]))
                continue;
            if (irix_env_has(out, used, environ[i]))
                continue;
            out[used] = strdup(environ[i]);
            if (!out[used]) { irix_free_host_vector(out); return 0; }
            ++used;
        }
    } else {
        for (k = 0; keep[k]; ++k) {
            const char *v = getenv(keep[k]);
            size_t n = strlen(keep[k]);
            if (!v || irix_env_has(out, used, keep[k]))
                continue;
            out[used] = (char *)malloc(n + strlen(v) + 2u);
            if (!out[used]) { irix_free_host_vector(out); return 0; }
            sprintf(out[used++], "%s=%s", keep[k], v);
        }
    }
    return out;
}

/* Shared process replacement for syscall and libc entry points. */
int irix_guest_execve(IrixGuestRuntime *rt, const char *guest_path,
                       char *const argv[], char *const envp[])
{
    char host_path[PATH_MAX], native_path[PATH_MAX];
    char interpreter[512] = "", script_arg[512] = "";
    const char *path, *native_root = getenv("IRIX_RECOMP_ROOT");
    char **child_argv = 0, **child_env = 0, **merged_env = 0;
    size_t argc = 0, i, j;
    int kind, saved_errno;
    if (!guest_path || !argv) { errno = EFAULT; return -1; }
    path = irix_host_path(rt, guest_path, host_path, sizeof(host_path));
    if (!path) return -1;
    /* Installed x86 runners retain the guest pathname. Prefer one when
       available; a missing runner still uses the original IRIX image. */
    if (guest_path[0] == '/' && native_root && native_root[0]) {
        int n = snprintf(native_path, sizeof(native_path), "%s%s",
                         native_root, guest_path);
        if (n > 0 && (size_t)n < sizeof(native_path) &&
            access(native_path, X_OK) == 0)
            path = native_path;
    }
    kind = irix_guest_exec_kind(path, interpreter, sizeof(interpreter),
                                script_arg, sizeof(script_arg));
    if (kind) {
        const char *runner = getenv("IRIX_GUEST_EXEC_RUNNER");
        const char *image = kind == 2 ? interpreter : guest_path;
        if (!runner || !runner[0] || access(runner, X_OK)) {
            errno = ENOENT;
            return -1;
        }
        if (kind == 2) {
            while (argv[argc]) {
                if (++argc >= 4096u) { errno = E2BIG; return -1; }
            }
            child_argv = (char **)calloc(argc + 4u, sizeof(*child_argv));
            if (!child_argv) { errno = ENOMEM; return -1; }
            child_argv[0] = (char *)image;
            i = 1u;
            if (script_arg[0]) child_argv[i++] = script_arg;
            child_argv[i++] = (char *)guest_path;
            for (j = 1u; j < argc; ++j) child_argv[i++] = argv[j];
        }
        merged_env = irix_merge_host_env(envp ? envp : environ, 0);
        child_env = irix_exec_environment(merged_env ? merged_env :
                                          (envp ? envp : environ), image);
        if (child_env)
            execve(runner, child_argv ? child_argv : argv, child_env);
    } else {
        merged_env = irix_merge_host_env(envp ? envp : environ, 1);
        execve(path, argv, merged_env ? merged_env : (envp ? envp : environ));
    }
    saved_errno = errno;
    irix_free_host_vector(child_env);
    irix_free_host_vector(merged_env);
    free(child_argv);
    errno = saved_errno;
    return -1;
}

/* Convert an IRIX N32 NULL-terminated char ** into host pointers.  The
 * strings themselves are already mapped in this process; only the 32-bit,
 * big-endian pointer vector needs ABI conversion. */
static char **irix_guest_string_vector(IrixGuestRuntime *rt,
                                       IrixGuestAddr vector)
{
    size_t count = 0, i;
    char **out;

    if (!vector) {
        errno = EFAULT;
        return 0;
    }
    for (;;) {
        IrixGuestAddr slot = vector + (IrixGuestAddr)(count * 4u);
        IrixGuestAddr string;
        if (slot < vector || !irix_guest_ptr(rt, slot, 4u)) {
            errno = EFAULT;
            return 0;
        }
        string = irix_guest_read32be(rt, slot);
        if (rt->trapped) {
            errno = EFAULT;
            return 0;
        }
        if (!string) break;
        if (!irix_guest_cstr(rt, string, 65536u)) {
            errno = EFAULT;
            return 0;
        }
        if (++count >= 4096u) {
            errno = E2BIG;
            return 0;
        }
    }

    out = (char **)calloc(count + 1u, sizeof(*out));
    if (!out) return 0;
    for (i = 0; i < count; ++i) {
        IrixGuestAddr string = irix_guest_read32be(
            rt, vector + (IrixGuestAddr)(i * 4u));
        out[i] = (char *)irix_guest_cstr(rt, string, 65536u);
    }
    return out;
}


/* ---- IRIX termio <-> Linux termios ------------------------------------ */

#include <termios.h>

/* Indices into IRIX c_cc[] (struct termio / termios, SVR4 numbering). */
enum { IV_INTR = 0, IV_QUIT = 1, IV_ERASE = 2, IV_KILL = 3, IV_EOF = 4,
       IV_EOL = 5, IV_EOL2 = 6, IV_SWTCH = 7, IV_START = 8, IV_STOP = 9,
       IV_SUSP = 10, IV_DSUSP = 11, IV_REPRINT = 12, IV_DISCARD = 13,
       IV_WERASE = 14, IV_LNEXT = 15, IV_NCCS = 23 };

static uint32_t irix_lflag_from_host(tcflag_t l)
{
    uint32_t o = l & (ISIG | ICANON | XCASE | ECHO | ECHOE | ECHOK | ECHONL |
                      NOFLSH);
    if (l & IEXTEN) o |= 0000400u;
    if (l & TOSTOP) o |= 0001000u;
    return o;
}

static tcflag_t host_lflag_from_irix(uint32_t l)
{
    tcflag_t o = (tcflag_t)(l & (ISIG | ICANON | XCASE | ECHO | ECHOE | ECHOK |
                                 ECHONL | NOFLSH));
    if (l & 0000400u) o |= IEXTEN;
    if (l & 0001000u) o |= TOSTOP;
    return o;
}

static int irix_termio_get(IrixGuestRuntime *rt, int fd, IrixGuestAddr a,
                           int with_speeds)
{
    struct termios t;
    uint8_t cc[IV_NCCS];
    unsigned o = 0, i;
    if (tcgetattr(fd, &t) != 0) return -1;
    memset(cc, 0, sizeof(cc));
    cc[IV_INTR] = t.c_cc[VINTR];   cc[IV_QUIT] = t.c_cc[VQUIT];
    cc[IV_ERASE] = t.c_cc[VERASE]; cc[IV_KILL] = t.c_cc[VKILL];
    if (t.c_lflag & ICANON) {      /* slots 4/5 are EOF/EOL when canonical */
        cc[IV_EOF] = t.c_cc[VEOF];
        cc[IV_EOL] = t.c_cc[VEOL];
    } else {                       /* ... and VMIN/VTIME otherwise */
        cc[IV_EOF] = t.c_cc[VMIN];
        cc[IV_EOL] = t.c_cc[VTIME];
    }
    cc[IV_EOL2] = t.c_cc[VEOL2];   cc[IV_SWTCH] = t.c_cc[VSWTC];
    cc[IV_START] = t.c_cc[VSTART]; cc[IV_STOP] = t.c_cc[VSTOP];
    cc[IV_SUSP] = t.c_cc[VSUSP];   cc[IV_DSUSP] = 0x19;       /* ^Y */
    cc[IV_REPRINT] = t.c_cc[VREPRINT]; cc[IV_DISCARD] = t.c_cc[VDISCARD];
    cc[IV_WERASE] = t.c_cc[VWERASE];   cc[IV_LNEXT] = t.c_cc[VLNEXT];
    if (!irix_guest_ptr(rt, a, with_speeds ? 48u : 40u)) {
        errno = EFAULT;
        return -1;
    }
    irix_guest_write32be(rt, a + 0u, (uint32_t)(t.c_iflag & 0x7fffu));
    irix_guest_write32be(rt, a + 4u, (uint32_t)(t.c_oflag & 0x7fffu));
    irix_guest_write32be(rt, a + 8u, (uint32_t)(t.c_cflag & 0x7fffu));
    irix_guest_write32be(rt, a + 12u, irix_lflag_from_host(t.c_lflag));
    o = 16u;
    if (with_speeds) {
        uint32_t sp = (uint32_t)(cfgetospeed(&t) & 017u);
        irix_guest_write32be(rt, a + o, sp);
        irix_guest_write32be(rt, a + o + 4u, sp);
        o += 8u;
    }
    irix_guest_write8(rt, a + o, t.c_line);
    for (i = 0; i < IV_NCCS; ++i)
        irix_guest_write8(rt, a + o + 1u + i, cc[i]);
    return 0;
}

static int irix_termio_set(IrixGuestRuntime *rt, int fd, IrixGuestAddr a,
                           int with_speeds, int action)
{
    struct termios t;
    uint8_t cc[IV_NCCS];
    uint32_t lflag;
    unsigned o = 16u, i;
    if (tcgetattr(fd, &t) != 0) return -1;
    if (!irix_guest_ptr(rt, a, with_speeds ? 48u : 40u)) {
        errno = EFAULT;
        return -1;
    }
    t.c_iflag = (t.c_iflag & ~0x7fffu) | (irix_guest_read32be(rt, a) & 0x7fffu);
    t.c_oflag = (t.c_oflag & ~0x7fffu) | (irix_guest_read32be(rt, a + 4u) & 0x7fffu);
    t.c_cflag = (t.c_cflag & ~0x7fffu) | (irix_guest_read32be(rt, a + 8u) & 0x7fffu);
    lflag = irix_guest_read32be(rt, a + 12u);
    t.c_lflag = (t.c_lflag & ~(ISIG | ICANON | XCASE | ECHO | ECHOE | ECHOK |
                               ECHONL | NOFLSH | IEXTEN | TOSTOP)) |
                host_lflag_from_irix(lflag);
    if (with_speeds) {
        uint32_t sp = irix_guest_read32be(rt, a + 16u) & 017u;
        if (sp) {
            cfsetospeed(&t, (speed_t)sp);
            cfsetispeed(&t, (speed_t)sp);
        }
        o = 24u;
    }
    for (i = 0; i < IV_NCCS; ++i)
        cc[i] = irix_guest_read8(rt, a + o + 1u + i);
    t.c_cc[VINTR] = cc[IV_INTR];   t.c_cc[VQUIT] = cc[IV_QUIT];
    t.c_cc[VERASE] = cc[IV_ERASE]; t.c_cc[VKILL] = cc[IV_KILL];
    if (lflag & ICANON) {
        t.c_cc[VEOF] = cc[IV_EOF];
        t.c_cc[VEOL] = cc[IV_EOL];
    } else {
        t.c_cc[VMIN] = cc[IV_EOF];
        t.c_cc[VTIME] = cc[IV_EOL];
    }
    t.c_cc[VEOL2] = cc[IV_EOL2];   t.c_cc[VSWTC] = cc[IV_SWTCH];
    t.c_cc[VSTART] = cc[IV_START]; t.c_cc[VSTOP] = cc[IV_STOP];
    t.c_cc[VSUSP] = cc[IV_SUSP];
    t.c_cc[VREPRINT] = cc[IV_REPRINT]; t.c_cc[VDISCARD] = cc[IV_DISCARD];
    t.c_cc[VWERASE] = cc[IV_WERASE];   t.c_cc[VLNEXT] = cc[IV_LNEXT];
    return tcsetattr(fd, action, &t);
}

void irix_guest_syscall(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    uint32_t number = (uint32_t)cpu->gpr[2];
    int64_t result;
    static int trace_syscalls = -1;

    if (trace_syscalls < 0)
        trace_syscalls = getenv("IRIX_TRACE_SYSCALLS") != 0;
    if (trace_syscalls && (number == 1001u ||
                           (number == 1004u && getenv("IRIX_RING_ALL")))) {
        unsigned k;
        fprintf(stderr, "EXITSTACK status=%d:", (int)(int32_t)cpu->gpr[4]);
        for (k = 0; k < 96; ++k) {
            uint32_t v = irix_guest_read32be(
                rt, (IrixGuestAddr)((uint32_t)cpu->gpr[29] + 4u * k));
            if (v >= 0x10000000u && v < 0x10100000u)
                fprintf(stderr, " %08x", v);
        }
        fputc('\n', stderr);
        {
            extern uint32_t irix_pc_ring[8192];
            extern unsigned irix_pc_ring_pos;
            unsigned q;
            fprintf(stderr, "PCRING (oldest..newest, 0x1000xxxx only):");
            for (q = 0; q < 8192; ++q) {
                uint32_t v = irix_pc_ring[(irix_pc_ring_pos + q) & 8191u];
                if (getenv("IRIX_RING_ALL") || (v >= 0x10000000u && v < 0x10100000u)) {
                    if (getenv("IRIX_RING_ALL"))
                        fprintf(stderr, " %08x:%08x", v,
                                irix_guest_read32be(rt, v));
                    else
                        fprintf(stderr, " %08x", v);
                }
            }
            fputc('\n', stderr);
        }
    }
    if (trace_syscalls)
        fprintf(stderr, "SYSCALL %u a0-a3=%08x,%08x,%08x,%08x ra=%08x\n",
                number, (unsigned)cpu->gpr[4], (unsigned)cpu->gpr[5],
                (unsigned)cpu->gpr[6], (unsigned)cpu->gpr[7],
                (unsigned)cpu->gpr[31]);

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
    case SYS_socket: { /* socket(domain, type, protocol) */
        int domain = host_socket_domain_from_irix(
            (int)(uint32_t)cpu->gpr[4]);
        int type = host_socket_type_from_irix(
            (int)(uint32_t)cpu->gpr[5]);
        int protocol = (int)(uint32_t)cpu->gpr[6];
        if (domain < 0 || type < 0) {
            errno = EAFNOSUPPORT;
            syscall_result(cpu, -1);
            return;
        }
        syscall_result(cpu, (int64_t)socket(domain, type, protocol));
        return;
    }
    case SYS_socketpair: { /* socketpair(domain, type, protocol, sv) */
        int domain = host_socket_domain_from_irix(
            (int)(uint32_t)cpu->gpr[4]);
        int type = host_socket_type_from_irix(
            (int)(uint32_t)cpu->gpr[5]);
        int protocol = (int)(uint32_t)cpu->gpr[6];
        IrixGuestAddr guest_fds = (uint32_t)cpu->gpr[7];
        int fds[2];
        int rc;

        if (domain < 0 || type < 0) {
            errno = EAFNOSUPPORT;
            syscall_result(cpu, -1);
            return;
        }
        if (!guest_fds || !irix_guest_ptr(rt, guest_fds, 8u)) {
            errno = EFAULT;
            syscall_result(cpu, -1);
            return;
        }
        rc = socketpair(domain, type, protocol, fds);
        if (rc == 0) {
            irix_guest_write32be(rt, guest_fds, (uint32_t)fds[0]);
            irix_guest_write32be(rt, guest_fds + 4u, (uint32_t)fds[1]);
            if (rt->trapped) {
                close(fds[0]);
                close(fds[1]);
                syscall_result(cpu, -1);
                return;
            }
        }
        syscall_result(cpu, rc);
        return;
    }
    case SYS_listen: {
        int fd = (int)(int32_t)cpu->gpr[4];
        int backlog = (int)(int32_t)cpu->gpr[5];
        syscall_result(cpu, (int64_t)listen(fd, backlog));
        return;
    }
    case SYS_accept: {
        int fd = (int)(int32_t)cpu->gpr[4];
        IrixGuestAddr address = (IrixGuestAddr)(uint32_t)cpu->gpr[5];
        IrixGuestAddr length_addr = (IrixGuestAddr)(uint32_t)cpu->gpr[6];
        struct sockaddr_storage host_address;
        socklen_t host_length = sizeof(host_address);
        uint32_t guest_capacity = 0;

        /* IRIX accept(2) uses a 32-bit N32 socklen_t in guest memory. */
        if (address && (!length_addr ||
            !irix_guest_ptr(rt, length_addr, 4u))) {
            errno = EFAULT;
            syscall_result(cpu, -1);
            return;
        }
        if (address) {
            guest_capacity = irix_guest_read32be(rt, length_addr);
            if (!irix_guest_ptr(rt, address, guest_capacity)) {
                errno = EFAULT;
                syscall_result(cpu, -1);
                return;
            }
        }
        memset(&host_address, 0, sizeof(host_address));
        result = (int64_t)accept(fd,
            address ? (struct sockaddr *)&host_address : NULL,
            address ? &host_length : NULL);
        if (result >= 0 && address) {
            size_t copy_length = guest_capacity < host_length
                               ? guest_capacity : host_length;
            uint8_t *guest_address =
                (uint8_t *)irix_guest_ptr(rt, address, copy_length);
            if (copy_length && !guest_address) {
                close((int)result);
                errno = EFAULT;
                syscall_result(cpu, -1);
                return;
            }
            if (copy_length) {
                memcpy(guest_address, &host_address, copy_length);
                if (copy_length >= sizeof(sa_family_t))
                    irix_guest_write16be(rt, address,
                        (uint16_t)((struct sockaddr *)&host_address)->sa_family);
            }
            irix_guest_write32be(rt, length_addr, (uint32_t)host_length);
            if (rt->trapped) {
                close((int)result);
                syscall_result(cpu, -1);
                return;
            }
        }
        syscall_result(cpu, result);
        return;
    }
    case SYS_sendto: {
        int fd = (int)(int32_t)cpu->gpr[4];
        IrixGuestAddr buffer = (IrixGuestAddr)(uint32_t)cpu->gpr[5];
        size_t length = (size_t)(uint32_t)cpu->gpr[6];
        int flags = (int)(int32_t)cpu->gpr[7];
        IrixGuestAddr address = (IrixGuestAddr)(uint32_t)cpu->gpr[8];
        socklen_t address_length = (socklen_t)(uint32_t)cpu->gpr[9];
        void *data = irix_guest_ptr(rt, buffer, length);
        struct sockaddr_storage host_address;
        if (!data && length) {
            errno = EFAULT;
            syscall_result(cpu, -1);
            return;
        }
        if (address && read_irix_sockaddr(rt, address, address_length,
                                          &host_address) != 0) {
            syscall_result(cpu, -1);
            return;
        }
        syscall_result(cpu, (int64_t)sendto(fd, data, length, flags,
            address ? (const struct sockaddr *)&host_address : NULL,
            address ? address_length : 0));
        return;
    }
    case SYS_recvfrom: {
        int fd = (int)(int32_t)cpu->gpr[4];
        IrixGuestAddr buffer = (IrixGuestAddr)(uint32_t)cpu->gpr[5];
        size_t length = (size_t)(uint32_t)cpu->gpr[6];
        int flags = (int)(int32_t)cpu->gpr[7];
        IrixGuestAddr address = (IrixGuestAddr)(uint32_t)cpu->gpr[8];
        IrixGuestAddr addrlen_addr = (IrixGuestAddr)(uint32_t)cpu->gpr[9];
        void *data = irix_guest_ptr(rt, buffer, length);
        struct sockaddr_storage host_address;
        socklen_t host_length = sizeof(host_address);
        if ((!data && length) || (address &&
            (!addrlen_addr || !irix_guest_ptr(rt, addrlen_addr, 4u)))) {
            errno = EFAULT;
            syscall_result(cpu, -1);
            return;
        }
        if (address) {
            uint32_t capacity = irix_guest_read32be(rt, addrlen_addr);
            if (!irix_guest_ptr(rt, address, capacity)) {
                errno = EFAULT;
                syscall_result(cpu, -1);
                return;
            }
            if (capacity < host_length) host_length = capacity;
        }
        result = (int64_t)recvfrom(fd, data, length, flags,
            address ? (struct sockaddr *)&host_address : NULL,
            address ? &host_length : NULL);
        if (result >= 0 && address) {
            uint8_t *guest_address = irix_guest_ptr(rt, address, host_length);
            memcpy(guest_address, &host_address, host_length);
            irix_guest_write16be(rt, address,
                (uint16_t)((struct sockaddr *)&host_address)->sa_family);
            irix_guest_write32be(rt, addrlen_addr, (uint32_t)host_length);
        }
        syscall_result(cpu, result);
        return;
    }
    case SYS_connect:
    case SYS_bind: { /* connect/bind(fd, sockaddr, length) */
        int fd = (int)(int32_t)cpu->gpr[4];
        IrixGuestAddr address = (IrixGuestAddr)(uint32_t)cpu->gpr[5];
        socklen_t length = (socklen_t)(uint32_t)cpu->gpr[6];
        struct sockaddr_storage host_address;
        
        if (read_irix_sockaddr(rt, address, length, &host_address) != 0) {
            syscall_result(cpu, -1);
            return;
        }
        if (number == SYS_connect) {
            int rc;
            const struct sockaddr *connect_address =
                (const struct sockaddr *)&host_address;
            socklen_t connect_length = length;

            /*
             * The SGI xconnc shared-memory transport is kernel-specific.
             * When an original client chooses the standard TCP loopback X11
             * transport, connect it to the host X server's Unix socket
             * instead. This keeps all X protocol I/O on the original guest
             * libX11 path while allowing Xephyr/Xorg without xconnc to serve
             * the client. Other addresses and ports retain normal TCP
             * behavior.
             */
            if (host_address.ss_family == AF_INET &&
                length >= sizeof(struct sockaddr_in)) {
                const struct sockaddr_in *in =
                    (const struct sockaddr_in *)&host_address;
                uint32_t host = ntohl(in->sin_addr.s_addr);
                unsigned port = ntohs(in->sin_port);
                if ((host >> 24) == 127u && port >= 6000u && port < 6100u) {
                    struct sockaddr_un x_socket;
                    unsigned display_number = port - 6000u;
                    int path_len = snprintf(x_socket.sun_path,
                                            sizeof(x_socket.sun_path),
                                            "/tmp/.X11-unix/X%u",
                                            display_number);
                    if (path_len > 0 &&
                        (size_t)path_len < sizeof(x_socket.sun_path)) {
                        int unix_fd;
                        memset(&x_socket, 0, sizeof(x_socket));
                        x_socket.sun_family = AF_UNIX;
                        snprintf(x_socket.sun_path, sizeof(x_socket.sun_path),
                                 "/tmp/.X11-unix/X%u", display_number);
                        unix_fd = socket(AF_UNIX, SOCK_STREAM, 0);
                        if (unix_fd >= 0) {
                            socklen_t unix_len = (socklen_t)(
                                offsetof(struct sockaddr_un, sun_path) +
                                (size_t)path_len + 1u);
                            if (connect(unix_fd,
                                        (const struct sockaddr *)&x_socket,
                                        unix_len) == 0) {
                                if (dup2(unix_fd, fd) >= 0) {
                                    close(unix_fd);
                                    rc = 0;
                                    
                                    syscall_result(cpu, (int64_t)rc);
                                    return;
                                }
                            }
                            close(unix_fd);
                        }
                    }
                }
            }
            rc = connect(fd, connect_address, connect_length);
            
            syscall_result(cpu, (int64_t)rc);
        } else
            syscall_result(cpu, (int64_t)bind(
                fd, (const struct sockaddr *)&host_address, length));
        return;
    }
    case SYS_getsockname:
    case SYS_getpeername: { /* get local/peer address in IRIX sockaddr ABI */
        int fd = (int)(int32_t)cpu->gpr[4];
        IrixGuestAddr address = (IrixGuestAddr)(uint32_t)cpu->gpr[5];
        IrixGuestAddr length_addr = (IrixGuestAddr)(uint32_t)cpu->gpr[6];
        struct sockaddr_storage host_address;
        socklen_t host_length = sizeof(host_address);
        uint32_t guest_length;
        unsigned char *guest_bytes;
        unsigned char *host_bytes = (unsigned char *)&host_address;
        size_t copy_length;
        int rc;

        if (!length_addr || !irix_guest_ptr(rt, length_addr, 4u)) {
            errno = EFAULT;
            syscall_result(cpu, -1);
            return;
        }
        guest_length = irix_guest_read32be(rt, length_addr);
        if (guest_length &&
            (!address || !irix_guest_ptr(rt, address, guest_length))) {
            errno = EFAULT;
            syscall_result(cpu, -1);
            return;
        }
        memset(&host_address, 0, sizeof(host_address));
        if (number == SYS_getsockname)
            rc = getsockname(fd, (struct sockaddr *)&host_address, &host_length);
        else
            rc = getpeername(fd, (struct sockaddr *)&host_address, &host_length);
        if (rc == 0) {
            if (host_length < sizeof(sa_family_t)) {
                errno = EINVAL;
                syscall_result(cpu, -1);
                return;
            }
            copy_length = guest_length < host_length ? guest_length : host_length;
            guest_bytes = (unsigned char *)irix_guest_ptr(rt, address,
                                                          copy_length);
            if (copy_length && !guest_bytes) {
                errno = EFAULT;
                syscall_result(cpu, -1);
                return;
            }
            if (copy_length >= 2u) {
                uint16_t family = ((struct sockaddr *)&host_address)->sa_family;
                uint16_t guest_family = family == AF_UNIX ? 1u :
                                        family == AF_INET ? 2u :
                                        family == AF_INET6 ? 24u : family;
                irix_guest_write16be(rt, address, guest_family);
                if (copy_length > 2u)
                    memcpy(guest_bytes + 2u, host_bytes + 2u,
                           copy_length - 2u);
            } else if (copy_length) {
                memcpy(guest_bytes, host_bytes, copy_length);
            }
            irix_guest_write32be(rt, length_addr, (uint32_t)host_length);
        }
        syscall_result(cpu, (int64_t)rc);
        return;
    }
    case SYS_sendmsg:
    case SYS_recvmsg: { /* sendmsg/recvmsg with the IRIX N32 BSD layout */
        int fd = (int)(int32_t)cpu->gpr[4];
        IrixGuestAddr msg_addr = (IrixGuestAddr)(uint32_t)cpu->gpr[5];
        int flags = (int)(int32_t)cpu->gpr[6];
        uint32_t name_addr, name_len, iov_addr, iov_count;
        uint32_t rights_addr, rights_len;
        struct sockaddr_storage name_storage;
        struct iovec *host_iov = NULL;
        void **host_iov_storage = NULL;
        struct msghdr msg;
        unsigned i;
        ssize_t rc;

        if (!irix_guest_ptr(rt, msg_addr, 24u)) {
            errno = EFAULT;
            
            syscall_result(cpu, -1);
            return;
        }
        name_addr = irix_guest_read32be(rt, msg_addr + 0u);
        name_len = irix_guest_read32be(rt, msg_addr + 4u);
        iov_addr = irix_guest_read32be(rt, msg_addr + 8u);
        iov_count = irix_guest_read32be(rt, msg_addr + 12u);
        rights_addr = irix_guest_read32be(rt, msg_addr + 16u);
        rights_len = irix_guest_read32be(rt, msg_addr + 20u);
        if (iov_count > 64u ||
            (iov_count && !guest_range_mapped(
                rt, iov_addr, (size_t)iov_count * 8u))) {
            errno = iov_count > 64u
                        ? EMSGSIZE : EFAULT;
            
            syscall_result(cpu, -1);
            return;
        }
        if (rights_addr || rights_len) {
            /* The legacy SGI access-rights ABI needs explicit SCM_RIGHTS
             * conversion; never pass guest pointers to the host kernel. */
            errno = EOPNOTSUPP;
            syscall_result(cpu, -1);
            return;
        }
        memset(&msg, 0, sizeof(msg));
        if (name_addr) {
            if (read_irix_sockaddr(rt, name_addr, name_len, &name_storage) != 0) {
                syscall_result(cpu, -1);
                return;
            }
            msg.msg_name = &name_storage;
            msg.msg_namelen = (socklen_t)name_len;
        }
        if (irix_guest_iov(rt, iov_addr, iov_count,
                           number == SYS_sendmsg,
                           &host_iov, &host_iov_storage) != 0) {
            
            syscall_result(cpu, -1);
            return;
        }
        msg.msg_iov = host_iov;
        msg.msg_iovlen = iov_count;
        if (number == SYS_sendmsg)
            rc = sendmsg(fd, &msg, flags);
        else
            rc = recvmsg(fd, &msg, flags);
        
        if (rc >= 0 && number == SYS_recvmsg) {
            size_t remaining = (size_t)rc;
            for (i = 0; i < iov_count && remaining; ++i) {
                IrixGuestAddr entry = iov_addr + (IrixGuestAddr)(i * 8u);
                IrixGuestAddr base = irix_guest_read32be(rt, entry);
                size_t copied = host_iov[i].iov_len;
                if (copied > remaining) copied = remaining;
                if (guest_copy_from_host(rt, base, host_iov[i].iov_base,
                                         copied) != 0) {
                    errno = EFAULT;
                    rc = -1;
                    break;
                }
                remaining -= copied;
            }
        }
        if (rc >= 0 && number == SYS_recvmsg) {
            if (name_addr)
                irix_guest_write32be(rt, msg_addr + 4u,
                                     (uint32_t)msg.msg_namelen);
            irix_guest_write32be(rt, msg_addr + 20u,
                                 (uint32_t)msg.msg_controllen);
        }
        irix_guest_iov_free(host_iov, host_iov_storage, iov_count);
        syscall_result(cpu, (int64_t)rc);
        return;
    }
    case SYS_shutdown: { /* shutdown(fd, how) */
        syscall_result(cpu, (int64_t)shutdown(
            (int)(int32_t)cpu->gpr[4], (int)(int32_t)cpu->gpr[5]));
        return;
    }
    case SYS_setsockopt: { /* setsockopt(fd, level, option, value, length) */
        int fd = (int)(int32_t)cpu->gpr[4];
        int level = (int)(int32_t)cpu->gpr[5];
        int option = (int)(int32_t)cpu->gpr[6];
        IrixGuestAddr value_addr = (IrixGuestAddr)(uint32_t)cpu->gpr[7];
        socklen_t length = (socklen_t)(uint32_t)cpu->gpr[8];
        int value;

        if (level == 0xffff) {
            level = SOL_SOCKET;
            switch (option) {
            case 0x0001: option = SO_DEBUG; break;
            case 0x0004: option = SO_REUSEADDR; break;
            case 0x0008: option = SO_KEEPALIVE; break;
            case 0x0010: option = SO_DONTROUTE; break;
            case 0x0020: option = SO_BROADCAST; break;
            case 0x0100: option = SO_OOBINLINE; break;
            case 0x1001: option = SO_SNDBUF; break;
            case 0x1002: option = SO_RCVBUF; break;
            case 0x1007: option = SO_ERROR; break;
            case 0x1008: option = SO_TYPE; break;
            default: errno = ENOPROTOOPT; syscall_result(cpu, -1); return;
            }
        } else if (level != IPPROTO_TCP && level != IPPROTO_IP &&
                   level != IPPROTO_IPV6) {
            errno = ENOPROTOOPT;
            syscall_result(cpu, -1);
            return;
        }
        if (length != 4u || !value_addr ||
            !irix_guest_ptr(rt, value_addr, 4u)) {
            errno = length == 4u ? EFAULT : EINVAL;
            syscall_result(cpu, -1);
            return;
        }
        value = (int32_t)irix_guest_read32be(rt, value_addr);
        syscall_result(cpu, (int64_t)setsockopt(fd, level, option,
                                                &value, sizeof(value)));
        return;
    }
    case SYS_getsockopt: { /* getsockopt(fd, level, option, value, lengthp) */
        int fd = (int)(int32_t)cpu->gpr[4];
        int level = (int)(int32_t)cpu->gpr[5];
        int option = (int)(int32_t)cpu->gpr[6];
        IrixGuestAddr value_addr = (IrixGuestAddr)(uint32_t)cpu->gpr[7];
        IrixGuestAddr length_addr = (IrixGuestAddr)(uint32_t)cpu->gpr[8];
        socklen_t length = sizeof(int);
        int value;
        int rc;

        if (level == 0xffff) {
            level = SOL_SOCKET;
            switch (option) {
            case 0x0001: option = SO_DEBUG; break;
            case 0x0004: option = SO_REUSEADDR; break;
            case 0x0008: option = SO_KEEPALIVE; break;
            case 0x0010: option = SO_DONTROUTE; break;
            case 0x0020: option = SO_BROADCAST; break;
            case 0x0100: option = SO_OOBINLINE; break;
            case 0x1001: option = SO_SNDBUF; break;
            case 0x1002: option = SO_RCVBUF; break;
            case 0x1007: option = SO_ERROR; break;
            case 0x1008: option = SO_TYPE; break;
            default: errno = ENOPROTOOPT; syscall_result(cpu, -1); return;
            }
        } else if (level != IPPROTO_TCP && level != IPPROTO_IP &&
                   level != IPPROTO_IPV6) {
            errno = ENOPROTOOPT;
            syscall_result(cpu, -1);
            return;
        }
        if (!value_addr || !length_addr ||
            !irix_guest_ptr(rt, value_addr, 4u) ||
            !irix_guest_ptr(rt, length_addr, 4u)) {
            errno = EFAULT;
            syscall_result(cpu, -1);
            return;
        }
        rc = getsockopt(fd, level, option, &value, &length);
        if (rc == 0) {
            irix_guest_write32be(rt, value_addr, (uint32_t)value);
            irix_guest_write32be(rt, length_addr, (uint32_t)length);
        }
        syscall_result(cpu, (int64_t)rc);
        return;
    }
    case SYS_select: { /* select(nfds, readfds, writefds, exceptfds, timeout) */
        int nfds = (int)(int32_t)cpu->gpr[4];
        IrixGuestAddr guest_sets[3] = {
            (IrixGuestAddr)(uint32_t)cpu->gpr[5],
            (IrixGuestAddr)(uint32_t)cpu->gpr[6],
            (IrixGuestAddr)(uint32_t)cpu->gpr[7]
        };
        fd_set host_sets[3];
        fd_set *host_ptrs[3] = { NULL, NULL, NULL };
        IrixGuestAddr timeout_addr = (IrixGuestAddr)(uint32_t)cpu->gpr[8];
        struct timeval timeout;
        struct timeval *timeout_ptr = NULL;
        unsigned set_index;
        int fd;
        int rc;

        if (nfds < 0 || nfds > FD_SETSIZE) {
            errno = EINVAL;
            syscall_result(cpu, -1);
            return;
        }
        for (set_index = 0; set_index < 3u; ++set_index) {
            unsigned bit;
            if (!guest_sets[set_index])
                continue;
            if (!irix_guest_ptr(rt, guest_sets[set_index], 128u)) {
                errno = EFAULT;
                syscall_result(cpu, -1);
                return;
            }
            FD_ZERO(&host_sets[set_index]);
            host_ptrs[set_index] = &host_sets[set_index];
            for (bit = 0; bit < (unsigned)nfds; ++bit) {
                uint32_t word = irix_guest_read32be(
                    rt, guest_sets[set_index] + (bit / 32u) * 4u);
                if (rt->trapped) {
                    errno = EFAULT;
                    syscall_result(cpu, -1);
                    return;
                }
                if (word & (1u << (bit & 31u)))
                    FD_SET((int)bit, &host_sets[set_index]);
            }
        }
        if (timeout_addr) {
            if (!irix_guest_ptr(rt, timeout_addr, 8u)) {
                errno = EFAULT;
                syscall_result(cpu, -1);
                return;
            }
            timeout.tv_sec = (int32_t)irix_guest_read32be(rt, timeout_addr);
            timeout.tv_usec = (int32_t)irix_guest_read32be(rt, timeout_addr + 4u);
            if (timeout.tv_sec < 0 || timeout.tv_usec < 0 ||
                timeout.tv_usec >= 1000000) {
                errno = EINVAL;
                syscall_result(cpu, -1);
                return;
            }
            timeout_ptr = &timeout;
        }
        rc = select(nfds, host_ptrs[0], host_ptrs[1], host_ptrs[2], timeout_ptr);
        
        if (rc >= 0) {
            for (set_index = 0; set_index < 3u; ++set_index) {
                if (!guest_sets[set_index])
                    continue;
                for (fd = 0; fd < 1024; fd += 32) {
                    uint32_t word = 0;
                    unsigned bit;
                    for (bit = 0; bit < 32u && fd + (int)bit < nfds; ++bit)
                        if (FD_ISSET(fd + (int)bit, &host_sets[set_index]))
                            word |= 1u << bit;
                    irix_guest_write32be(
                        rt, guest_sets[set_index] + (unsigned)fd / 8u, word);
                }
            }
            if (timeout_addr) {
                irix_guest_write32be(rt, timeout_addr,
                                     (uint32_t)timeout.tv_sec);
                irix_guest_write32be(rt, timeout_addr + 4u,
                                     (uint32_t)timeout.tv_usec);
            }
        }
        syscall_result(cpu, (int64_t)rc);
        return;
    }
    case SYS_sigprocmask: { /* ksigprocmask(how, set, oldset) */
        int how = (int)(int32_t)cpu->gpr[4];
        IrixGuestAddr set_addr = (IrixGuestAddr)(uint32_t)cpu->gpr[5];
        IrixGuestAddr old_addr = (IrixGuestAddr)(uint32_t)cpu->gpr[6];
        sigset_t set, old;
        int host_how;
        int signo;

        if (how == 0) host_how = 0; /* SIG_NOP: query only */
        else if (how == 1) host_how = SIG_BLOCK;
        else if (how == 2) host_how = SIG_UNBLOCK;
        else if (how == 3) host_how = SIG_SETMASK;
        else { errno = EINVAL; syscall_result(cpu, -1); return; }

        sigemptyset(&set);
        if (set_addr) {
            if (!irix_guest_ptr(rt, set_addr, 16u)) {
                errno = EFAULT; syscall_result(cpu, -1); return;
            }
            for (signo = 1; signo <= 128; ++signo) {
                uint32_t word = irix_guest_read32be(
                    rt, set_addr + (IrixGuestAddr)((signo - 1) / 32) * 4u);
                if (rt->trapped) { errno = EFAULT; syscall_result(cpu, -1); return; }
                if ((word & (1u << ((signo - 1) & 31))) && signo <= SIGRTMAX)
                    sigaddset(&set, signo);
            }
        } else if (how != 0) {
            errno = EFAULT; syscall_result(cpu, -1); return;
        }

        if (sigprocmask(set_addr ? host_how : 0, set_addr ? &set : NULL,
                        old_addr ? &old : NULL) != 0) {
            syscall_result(cpu, -1); return;
        }
        if (old_addr) {
            if (!irix_guest_ptr(rt, old_addr, 16u)) {
                errno = EFAULT; syscall_result(cpu, -1); return;
            }
            for (signo = 0; signo < 4; ++signo) {
                uint32_t word = 0;
                int bit;
                for (bit = 0; bit < 32; ++bit) {
                    int sig = signo * 32 + bit + 1;
                    if (sig <= SIGRTMAX && sigismember(&old, sig) == 1)
                        word |= 1u << bit;
                }
                irix_guest_write32be(rt, old_addr + (IrixGuestAddr)signo * 4u,
                                     word);
            }
            if (rt->trapped) { errno = EFAULT; syscall_result(cpu, -1); return; }
        }
        syscall_result(cpu, 0);
        return;
    }
    case SYS_kill: { /* kill(pid, signo) */
        pid_t pid = (pid_t)(int32_t)cpu->gpr[4];
        int signo = (int)(int32_t)cpu->gpr[5];
        syscall_result(cpu, kill(pid, signo));
        return;
    }
    case SYS_setpgrp: { /* setpgrp(flags): return pgrp or create a new session */
        int flags = (int)(int32_t)cpu->gpr[4];

        /* IRIX's System V setpgrp syscall returns the current process group
         * when flags is zero, and behaves like setsid when flags is nonzero.
         * The SGI kernel's irix_setpgrp(flags) implements this convention. */
        syscall_result(cpu, flags ? (int64_t)setsid() : (int64_t)getpgrp());
        return;
    }
    case SYS_fork: { /* fork */
        pid_t pid = fork();

        if (pid < 0) {
            syscall_result(cpu, -1);
            return;
        }

        /*
         * SGI libc branches on v1 after the raw fork syscall: zero selects
         * the parent path, one selects child initialization (which then
         * returns zero from libc fork). Always replace the incoming v1.
         * Because the guest
         * runtime lives in the host process, host fork() also gives the child
         * a copy-on-write clone of the complete guest address space and CPU
         * state, which is exactly what this bring-up path needs.
         */
        if (pid == 0) {
            cpu->gpr[2] = 0u;
            cpu->gpr[3] = 1u;
            cpu->gpr[7] = 0u;
            cpu->gpr[0] = 0u;
        } else {
            cpu->gpr[2] = (uint64_t)(int64_t)pid;
            cpu->gpr[3] = 0u;
            cpu->gpr[7] = 0u;
            cpu->gpr[0] = 0u;
        }
        return;
    }
    case SYS_execve: { /* execve(path, argv, envp) */
        const char *path = irix_guest_cstr(rt, (IrixGuestAddr)cpu->gpr[4], 65536u);
        char **argv = 0, **envp = 0;
        IrixGuestAddr env_addr = (IrixGuestAddr)cpu->gpr[6];
        int saved_errno;
        if (!path) { errno = EFAULT; goto exec_syscall_failed; }
        argv = irix_guest_string_vector(rt, (IrixGuestAddr)cpu->gpr[5]);
        if (!argv) goto exec_syscall_failed;
        if (env_addr) {
            envp = irix_guest_string_vector(rt, env_addr);
            if (!envp) goto exec_syscall_failed;
        }
        irix_guest_execve(rt, path, argv, envp);
exec_syscall_failed:
        saved_errno = errno;
        free(argv);
        free(envp);
        errno = saved_errno;
        syscall_result(cpu, -1);
        return;
    }

    case SYS_getdents: { /* getdents(fd, IRIX dirent buffer, byte capacity) */
        struct LinuxDirent64 {
            uint64_t ino;
            int64_t off;
            uint16_t reclen;
            uint8_t type;
            char name[];
        };
        int fd = (int)(int32_t)cpu->gpr[4];
        IrixGuestAddr out_addr = (IrixGuestAddr)(uint32_t)cpu->gpr[5];
        size_t capacity = (size_t)(uint32_t)cpu->gpr[6];
        unsigned char *host_buffer;
        long got;
        size_t in_offset = 0, out_offset = 0, n;
        if (capacity && !irix_guest_ptr(rt, out_addr, capacity)) {
            errno = EFAULT;
            syscall_result(cpu, -1);
            return;
        }
        host_buffer = malloc(capacity ? capacity : 1u);
        if (!host_buffer) {
            errno = ENOMEM;
            syscall_result(cpu, -1);
            return;
        }
        got = syscall(__NR_getdents64, fd, host_buffer, capacity);
        if (got >= 0) {
            while (in_offset + offsetof(struct LinuxDirent64, name) < (size_t)got) {
                struct LinuxDirent64 *entry =
                    (struct LinuxDirent64 *)(host_buffer + in_offset);
                size_t name_len, record_len;
                if (entry->reclen < offsetof(struct LinuxDirent64, name) + 1u ||
                    in_offset + entry->reclen > (size_t)got)
                    break;
                name_len = strnlen(entry->name,
                    entry->reclen - offsetof(struct LinuxDirent64, name));
                record_len = (18u + name_len + 1u + 7u) & ~7u;
                if (out_offset + record_len > capacity)
                    break;
                irix_guest_write64be(rt, out_addr + (IrixGuestAddr)out_offset,
                                     entry->ino);
                irix_guest_write64be(rt, out_addr + (IrixGuestAddr)out_offset + 8u,
                                     (uint64_t)entry->off);
                irix_guest_write16be(rt, out_addr + (IrixGuestAddr)out_offset + 16u,
                                     (uint16_t)record_len);
                for (n = 0; n <= name_len; ++n)
                    irix_guest_write8(rt, out_addr + (IrixGuestAddr)out_offset + 18u + n,
                                      (uint8_t)entry->name[n]);
                in_offset += entry->reclen;
                out_offset += record_len;
            }
        }
        free(host_buffer);
        syscall_result(cpu, got < 0 ? -1 : (int64_t)out_offset);
        return;
    }
    case 1207: { /* ngetdents64: return directory bytes and byte count */
        struct LinuxDirent64 {
            uint64_t ino;
            int64_t off;
            uint16_t reclen;
            uint8_t type;
            char name[];
        };
        int fd = (int)(int32_t)cpu->gpr[4];
        IrixGuestAddr out_addr = (IrixGuestAddr)(uint32_t)cpu->gpr[5];
        size_t capacity = (size_t)(uint32_t)cpu->gpr[6];
        IrixGuestAddr count_addr = (IrixGuestAddr)(uint32_t)cpu->gpr[7];
        unsigned char *host_buffer;
        long got;
        size_t in_offset = 0, out_offset = 0, n;
        if ((capacity && !irix_guest_ptr(rt, out_addr, capacity)) ||
            (count_addr && !irix_guest_ptr(rt, count_addr, 4u))) {
            errno = EFAULT;
            syscall_result(cpu, -1);
            return;
        }
        host_buffer = malloc(capacity ? capacity : 1u);
        if (!host_buffer) {
            free(host_buffer);
            errno = ENOMEM;
            syscall_result(cpu, -1);
            return;
        }
        got = syscall(__NR_getdents64, fd, host_buffer, capacity);
        if (got >= 0) {
            while (in_offset + offsetof(struct LinuxDirent64, name) < (size_t)got) {
                struct LinuxDirent64 *entry =
                    (struct LinuxDirent64 *)(host_buffer + in_offset);
                size_t name_len, record_len;
                if (entry->reclen < offsetof(struct LinuxDirent64, name) + 1u ||
                    in_offset + entry->reclen > (size_t)got)
                    break;
                name_len = strnlen(entry->name,
                    entry->reclen - offsetof(struct LinuxDirent64, name));
                record_len = (18u + name_len + 1u + 7u) & ~7u;
                if (out_offset + record_len > capacity)
                    break;
                irix_guest_write64be(rt, out_addr + (IrixGuestAddr)out_offset,
                                     entry->ino);
                irix_guest_write64be(rt, out_addr + (IrixGuestAddr)out_offset + 8u,
                                     (uint64_t)entry->off);
                irix_guest_write16be(rt, out_addr + (IrixGuestAddr)out_offset + 16u,
                                     (uint16_t)record_len);
                for (n = 0; n <= name_len; ++n)
                    irix_guest_write8(rt, out_addr + (IrixGuestAddr)out_offset + 18u + n,
                                      (uint8_t)entry->name[n]);
                in_offset += entry->reclen;
                out_offset += record_len;
            }
            if (count_addr)
                irix_guest_write32be(rt, count_addr, (uint32_t)out_offset);
        }
        free(host_buffer);
        syscall_result(cpu, got < 0 ? -1 : (int64_t)out_offset);
        return;
    }
    case SYS_read: { /* read */
        IrixGuestAddr addr = (IrixGuestAddr)(uint32_t)cpu->gpr[5];
        size_t count = (size_t)(uint32_t)cpu->gpr[6];
        void *p = count ? irix_guest_ptr(rt, addr, count) : NULL;
        result = p || !count
            ? (int64_t)read((int)(int32_t)cpu->gpr[4], p, count)
            : irix_guest_read_fd(rt, (int)(int32_t)cpu->gpr[4], addr, count);
        
        syscall_result(cpu, result);
        return;
    }
    case SYS_write: { /* write */
        IrixGuestAddr addr = (IrixGuestAddr)(uint32_t)cpu->gpr[5];
        size_t count = (size_t)(uint32_t)cpu->gpr[6];
        const void *p = irix_guest_ptr(rt, addr, count);
        result = p || !count
            ? (int64_t)write((int)(int32_t)cpu->gpr[4], p, count)
            : irix_guest_write_fd(rt, (int)(int32_t)cpu->gpr[4], addr, count);
        
        
        syscall_result(cpu, result);
        return;
    }
    case SYS_readv:
    case SYS_writev: {
        int fd = (int)(int32_t)cpu->gpr[4];
        IrixGuestAddr vector_addr = (IrixGuestAddr)(uint32_t)cpu->gpr[5];
        int32_t signed_count = (int32_t)cpu->gpr[6];
        uint32_t vector_count;
        struct iovec *iov = NULL;
        void **storage = NULL;

        if (signed_count < 0 ||
            (uint32_t)signed_count > IRIX_GUEST_IOV_MAX) {
            errno = EINVAL;
            syscall_result(cpu, -1);
            return;
        }
        vector_count = (uint32_t)signed_count;
        if (irix_guest_iov(rt, vector_addr, vector_count,
                           number == SYS_writev, &iov, &storage) != 0) {
            
            syscall_result(cpu, -1);
            return;
        }

        result = number == SYS_writev
            ? (int64_t)writev(fd, iov, (int)vector_count)
            : (int64_t)readv(fd, iov, (int)vector_count);
        
        if (result >= 0 && number == SYS_readv) {
            size_t remaining = (size_t)result;
            uint32_t i;
            for (i = 0; i < vector_count && remaining; ++i) {
                IrixGuestAddr entry = vector_addr + (IrixGuestAddr)(i * 8u);
                IrixGuestAddr base = irix_guest_read32be(rt, entry);
                size_t copied = iov[i].iov_len < remaining
                              ? iov[i].iov_len : remaining;
                if (copied && guest_copy_from_host(
                        rt, base, storage[i], copied) != 0) {
                    errno = EFAULT;
                    result = -1;
                    break;
                }
                remaining -= copied;
            }
        }
        irix_guest_iov_free(iov, storage, vector_count);
        syscall_result(cpu, result);
        return;
    }
    case SYS_creat: { /* creat(path, mode) */
        IrixGuestAddr path_addr = (IrixGuestAddr)(uint32_t)cpu->gpr[4];
        const char *guest_path = irix_guest_cstr(rt, path_addr, 65536u);
        char host_path[PATH_MAX];
        const char *path;
        mode_t mode = (mode_t)(uint32_t)cpu->gpr[5];
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
        result = (int64_t)open(path, O_WRONLY | O_CREAT | O_TRUNC, mode);
        syscall_result(cpu, result);
        return;
    }
    case SYS_open: { /* open */
        IrixGuestAddr path_addr = (IrixGuestAddr)(uint32_t)cpu->gpr[4];
        const char *guest_path = irix_guest_cstr(rt, path_addr, 65536u);
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
        syscall_result(cpu, result);
        return;
    }
    case SYS_close: /* close */
        result = (int64_t)close((int)(int32_t)cpu->gpr[4]);
        syscall_result(cpu, result);
        return;
    case SYS_link: {
        const char *old_guest = irix_guest_cstr(rt, (IrixGuestAddr)(uint32_t)cpu->gpr[4], 65536u);
        const char *new_guest = irix_guest_cstr(rt, (IrixGuestAddr)(uint32_t)cpu->gpr[5], 65536u);
        char old_host[PATH_MAX], new_host[PATH_MAX];
        const char *old_path, *new_path;
        if (!old_guest || !new_guest) {
            errno = EFAULT;
            syscall_result(cpu, -1);
            return;
        }
        old_path = irix_host_path(rt, old_guest, old_host, sizeof(old_host));
        new_path = irix_host_path(rt, new_guest, new_host, sizeof(new_host));
        if (!old_path || !new_path) {
            syscall_result(cpu, -1);
            return;
        }
        syscall_result(cpu, (int64_t)link(old_path, new_path));
        return;
    }
    case SYS_unlink: {
        IrixGuestAddr path_addr = (IrixGuestAddr)(uint32_t)cpu->gpr[4];
        const char *guest_path = irix_guest_cstr(rt, path_addr, 65536u);
        char host_path[PATH_MAX];
        const char *path;
        if (!guest_path) { errno = EFAULT; syscall_result(cpu, -1); return; }
        path = irix_host_path(rt, guest_path, host_path, sizeof(host_path));
        if (!path) { syscall_result(cpu, -1); return; }
        syscall_result(cpu, (int64_t)unlink(path));
        return;
    }
    case SYS_pipe: { /* pipe */
        int fds[2];

        /*
         * IRIX/MIPS returns the two pipe descriptors in v0/v1 from the raw
         * syscall.  libc's _pipe wrapper stores those registers into the
         * caller's int[2].  Do not write through a0 here: at the kernel ABI
         * boundary it is merely the libc wrapper's still-live argument.
         */
        if (pipe(fds) != 0) {
            syscall_result(cpu, -1);
            return;
        }

        cpu->gpr[2] = (uint64_t)(int64_t)fds[0];
        cpu->gpr[3] = (uint64_t)(int64_t)fds[1];
        cpu->gpr[7] = 0u;
        cpu->gpr[0] = 0u;
        return;
    }
    case SYS_fcntl: { /* fcntl(fd, cmd, arg) */
        int fd = (int)(int32_t)cpu->gpr[4];
        int cmd = (int)(int32_t)cpu->gpr[5];
        uint32_t arg = (uint32_t)cpu->gpr[6];

        /*
         * The basic IRIX fcntl command numbers used by the desktop match the
         * traditional SysV values: 0..4 are DUPFD/GETFD/SETFD/GETFL/SETFL.
         * Translate the status flags rather than passing IRIX bit values
         * directly to the Linux kernel.
         */
        switch (cmd) {
        case 0: /* F_DUPFD */
            result = (int64_t)fcntl(fd, F_DUPFD, (int)arg);
            break;
        case 1: /* F_GETFD */
            result = (int64_t)fcntl(fd, F_GETFD);
            break;
        case 2: /* F_SETFD */
            result = (int64_t)fcntl(fd, F_SETFD, (int)arg);
            break;
        case 3: { /* F_GETFL */
            int hf = fcntl(fd, F_GETFL);
            uint32_t gf = 0u;
            if (hf < 0) {
                result = -1;
                break;
            }
            switch (hf & O_ACCMODE) {
            case O_WRONLY: gf |= 1u; break;
            case O_RDWR:   gf |= 2u; break;
            default: break;
            }
            if (hf & O_NONBLOCK) gf |= 0x0080u;
            if (hf & O_APPEND)   gf |= 0x0008u;
#ifdef O_SYNC
            if (hf & O_SYNC)     gf |= 0x0010u;
#endif
#ifdef O_ASYNC
            if (hf & O_ASYNC)    gf |= 0x0040u;
#endif
            result = (int64_t)gf;
            break;
        }
        case 4: /* F_SETFL */
            result = (int64_t)fcntl(fd, F_SETFL, irix_open_flags(arg));
            break;
        case 6:  /* F_SETLK */
        case 7:  /* F_SETLKW */
        case 12: /* F_SETBSDLK: IRIX compatibility alias for F_SETLK */
        case 13: /* F_SETBSDLKW: IRIX compatibility alias for F_SETLKW */
        case 14: { /* F_GETLK */
            const size_t guest_flock_size = 48u;
            IrixGuestAddr guest_flock = arg;
            struct flock host_flock;
            int host_cmd;
            uint16_t guest_lock_type;
            void *guest_flock_ptr = guest_flock
                ? irix_guest_ptr(rt, guest_flock, guest_flock_size) : 0;

            if (!guest_flock_ptr || rt->trapped) {
                errno = EFAULT;
                result = -1;
                break;
            }

            /*
             * IRIX N32 struct flock uses 64-bit offsets and reserves the
             * final 16 bytes. Translate its 48-byte big-endian form instead
             * of passing the guest layout to Linux's smaller host struct.
             */
            memset(&host_flock, 0, sizeof(host_flock));
            guest_lock_type = irix_guest_read16be(rt, guest_flock);
            switch (guest_lock_type) {
            case 1u: host_flock.l_type = F_RDLCK; break;
            case 2u: host_flock.l_type = F_WRLCK; break;
            case 3u: host_flock.l_type = F_UNLCK; break;
            default: {
                errno = EINVAL;
                result = -1;
                goto fcntl_lock_done;
            }
            }
            host_flock.l_whence = (short)irix_guest_read16be(rt,
                                                            guest_flock + 2u);
            host_flock.l_start = (off_t)(int64_t)irix_guest_read64be(
                rt, guest_flock + 8u);
            host_flock.l_len = (off_t)(int64_t)irix_guest_read64be(
                rt, guest_flock + 16u);
            if (rt->trapped) {
                errno = EFAULT;
                result = -1;
                break;
            }

            host_cmd = (cmd == 6 || cmd == 12) ? F_SETLK
                     : (cmd == 7 || cmd == 13) ? F_SETLKW : F_GETLK;
            result = (int64_t)fcntl(fd, host_cmd, &host_flock);

            if (result == 0 && cmd == 14) {
                uint16_t guest_lock_type = host_flock.l_type == F_RDLCK ? 1u
                    : host_flock.l_type == F_WRLCK ? 2u : 3u;
                irix_guest_write16be(rt, guest_flock,
                                     guest_lock_type);
                irix_guest_write16be(rt, guest_flock + 2u,
                                     (uint16_t)host_flock.l_whence);
                irix_guest_write64be(rt, guest_flock + 8u,
                                     (uint64_t)(int64_t)host_flock.l_start);
                irix_guest_write64be(rt, guest_flock + 16u,
                                     (uint64_t)(int64_t)host_flock.l_len);
                /* Linux has no IRIX remote lock system-id field. */
                irix_guest_write32be(rt, guest_flock + 24u, 0u);
                irix_guest_write32be(rt, guest_flock + 28u,
                                     (uint32_t)host_flock.l_pid);
            }
        fcntl_lock_done:
            break;
        }
        default:
            errno = EINVAL;
            result = -1;
            break;
        }

        syscall_result(cpu, result);
        return;
    }
    case SYS_ioctl: { /* ioctl(fd, cmd, arg) */
        int fd = (int)(int32_t)cpu->gpr[4];
        uint32_t cmd = (uint32_t)cpu->gpr[5];
        IrixGuestAddr arg_addr = (IrixGuestAddr)(uint32_t)cpu->gpr[6];

        /* X11 transport's IRIX socket bytes-readable query. */
        if (cmd == 0x4004730au) {
            int available = 0;
            if (!arg_addr || !irix_guest_ptr(rt, arg_addr, 4u)) {
                errno = EFAULT;
                syscall_result(cpu, -1);
                return;
            }
            result = (int64_t)ioctl(fd, FIONREAD, &available);
            if (result == 0)
                irix_guest_write32be(rt, arg_addr, (uint32_t)available);
            syscall_result(cpu, result);
            return;
        }

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

        /* Terminal attributes: IRIX termio (TIOC|1..4 old, TIOC|201..204
         * new layout), window size and flush, translated for the host tty. */
        switch (cmd) {
        case 0x5401u: case 0x54c9u:        /* TCGETA */
            syscall_result(cpu, (int64_t)irix_termio_get(
                rt, fd, arg_addr, cmd == 0x54c9u));
            return;
        case 0x5402u: case 0x54cau:        /* TCSETA  */
        case 0x5403u: case 0x54cbu:        /* TCSETAW */
        case 0x5404u: case 0x54ccu: {      /* TCSETAF */
            int act = (cmd & 0xffu) == 0x02u || (cmd & 0xffu) == 0xcau ? TCSANOW
                    : (cmd & 0xffu) == 0x03u || (cmd & 0xffu) == 0xcbu ? TCSADRAIN
                    : TCSAFLUSH;
            syscall_result(cpu, (int64_t)irix_termio_set(
                rt, fd, arg_addr, (cmd & 0xffu) >= 0xc9u, act));
            return;
        }
        case 0x5407u: {                    /* TCFLSH */
            int q = (int)(uint32_t)cpu->gpr[6];
            syscall_result(cpu, (int64_t)tcflush(fd,
                q == 0 ? TCIFLUSH : q == 1 ? TCOFLUSH : TCIOFLUSH));
            return;
        }
        case 0x5405u:                      /* TCSBRK: drain or send break */
            syscall_result(cpu, (int64_t)((uint32_t)cpu->gpr[6]
                ? tcdrain(fd) : tcsendbreak(fd, 0)));
            return;
        case 0x40087468u: {                /* TIOCGWINSZ (struct winsize) */
            struct winsize ws;
            if (!arg_addr || !irix_guest_ptr(rt, arg_addr, 8u)) {
                errno = EFAULT;
                syscall_result(cpu, -1);
                return;
            }
            if (ioctl(fd, TIOCGWINSZ, &ws) != 0) {
                syscall_result(cpu, -1);
                return;
            }
            irix_guest_write16be(rt, arg_addr, ws.ws_row);
            irix_guest_write16be(rt, arg_addr + 2u, ws.ws_col);
            irix_guest_write16be(rt, arg_addr + 4u, ws.ws_xpixel);
            irix_guest_write16be(rt, arg_addr + 6u, ws.ws_ypixel);
            syscall_result(cpu, 0);
            return;
        }
        case 0x80087467u: {                /* TIOCSWINSZ */
            struct winsize ws;
            if (!arg_addr || !irix_guest_ptr(rt, arg_addr, 8u)) {
                errno = EFAULT;
                syscall_result(cpu, -1);
                return;
            }
            ws.ws_row = irix_guest_read16be(rt, arg_addr);
            ws.ws_col = irix_guest_read16be(rt, arg_addr + 2u);
            ws.ws_xpixel = irix_guest_read16be(rt, arg_addr + 4u);
            ws.ws_ypixel = irix_guest_read16be(rt, arg_addr + 6u);
            syscall_result(cpu, (int64_t)ioctl(fd, TIOCSWINSZ, &ws));
            return;
        }
        default:
            break;
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
        syscall_result(cpu, 0);
        return;
    }
    case SYS_alarm: { /* alarm(seconds) */
        uint32_t seconds = (uint32_t)cpu->gpr[4];
        uint64_t now = monotonic_time_ns();
        uint32_t remaining = 0u;
        if (rt->alarm_deadline_ns && now && rt->alarm_deadline_ns > now) {
            uint64_t delta = rt->alarm_deadline_ns - now;
            remaining = (uint32_t)((delta + 999999999ull) / 1000000000ull);
        }
        rt->alarm_deadline_ns = seconds && now
            ? now + (uint64_t)seconds * 1000000000ull : 0u;
        syscall_result(cpu, remaining);
        return;
    }
    case SYS_waitsys: { /* waitid(idtype, id, siginfo, options) */
        int idtype = (int)(int32_t)cpu->gpr[4];
        id_t id = (id_t)(uint32_t)cpu->gpr[5];
        IrixGuestAddr info_addr = (IrixGuestAddr)(uint32_t)cpu->gpr[6];
        uint32_t guest_options = (uint32_t)cpu->gpr[7];
        idtype_t host_type;
        int host_options = 0;
        siginfo_t info;
        unsigned i;
        if (idtype == 0) host_type = P_PID;
        else if (idtype == 2) host_type = P_PGID;
        else if (idtype == 7) host_type = P_ALL;
        else {
            errno = EINVAL;
            syscall_result(cpu, -1);
            return;
        }
        if (guest_options & 1u) host_options |= WEXITED;
        if (guest_options & (2u | 4u)) host_options |= WSTOPPED;
        if (guest_options & 8u) host_options |= WCONTINUED;
        if (guest_options & 0100u) host_options |= WNOHANG;
        if (guest_options & 0200u) host_options |= WNOWAIT;
        if (!host_options) host_options = WEXITED;
        if (!info_addr || !irix_guest_ptr(rt, info_addr, 128u)) {
            errno = EFAULT;
            syscall_result(cpu, -1);
            return;
        }
        memset(&info, 0, sizeof(info));
        result = (int64_t)waitid(host_type, id, &info, host_options);
        if (result == 0) {
            for (i = 0; i < 128u; i += 4u)
                irix_guest_write32be(rt, info_addr + i, 0u);
            irix_guest_write32be(rt, info_addr + 0u,
                                 info.si_pid ? 18u : 0u); /* IRIX SIGCLD */
            irix_guest_write32be(rt, info_addr + 4u, (uint32_t)info.si_code);
            irix_guest_write32be(rt, info_addr + 8u, (uint32_t)info.si_errno);
            irix_guest_write32be(rt, info_addr + 12u, (uint32_t)info.si_pid);
            irix_guest_write32be(rt, info_addr + 20u, (uint32_t)info.si_status);
        }
        syscall_result(cpu, result);
        return;
    }
    case SYS_sgikopt: { /* sgikopt(name, value, capacity) */
        IrixGuestAddr name_addr = (IrixGuestAddr)(uint32_t)cpu->gpr[4];
        IrixGuestAddr value_addr = (IrixGuestAddr)(uint32_t)cpu->gpr[5];
        size_t capacity = (size_t)(uint32_t)cpu->gpr[6];
        const char *name = irix_guest_cstr(rt, name_addr, 256u);
        const char *value = NULL;
        size_t length;
        if (name && strcmp(name, "diskless") == 0)
            value = "isfalse";
        if (!name || !value) {
            errno = name ? ENOENT : EFAULT;
            syscall_result(cpu, -1);
            return;
        }
        length = strlen(value) + 1u;
        if (capacity < length || !irix_guest_ptr(rt, value_addr, length)) {
            errno = capacity < length ? ERANGE : EFAULT;
            syscall_result(cpu, -1);
            return;
        }
        memcpy(irix_guest_ptr(rt, value_addr, length), value, length);
        syscall_result(cpu, 0);
        return;
    }
    case SYS_sginap: { /* sginap(ticks), IRIX clock is 100 Hz */
        int32_t ticks = (int32_t)cpu->gpr[4];
        struct timespec requested, remaining;
        if (ticks <= 0) {
            (void)sched_yield();
            syscall_result(cpu, 0);
            return;
        }
        requested.tv_sec = ticks / 100;
        requested.tv_nsec = (long)(ticks % 100) * 10000000L;
        if (nanosleep(&requested, &remaining) == 0) {
            syscall_result(cpu, 0);
            return;
        }
        if (errno == EINTR) {
            int64_t left = (int64_t)remaining.tv_sec * 100 +
                           (remaining.tv_nsec + 9999999L) / 10000000L;
            syscall_result(cpu, left > INT32_MAX ? INT32_MAX : left);
            return;
        }
        syscall_result(cpu, -1);
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
         * validates 0 < sig <= SIGRTMAX, and stores the fourth syscall argument
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
    case SYS_getdomainname: { /* getdomainname(char *name, size_t len) */
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
        syscall_result(cpu, (int64_t)getdomainname(name, len));
        return;
    }
    case SYS_chdir: {
        const char *path = irix_guest_cstr(rt, (IrixGuestAddr)(uint32_t)cpu->gpr[4], 65536u);
        if (!path) { errno = EFAULT; syscall_result(cpu, -1); return; }
        syscall_result(cpu, (int64_t)chdir(path));
        return;
    }
    case SYS_chmod: {
        const char *guest_path = irix_guest_cstr(rt, (IrixGuestAddr)(uint32_t)cpu->gpr[4], 65536u);
        char host_path[PATH_MAX];
        const char *path;
        if (!guest_path) { errno = EFAULT; syscall_result(cpu, -1); return; }
        path = irix_host_path(rt, guest_path, host_path, sizeof(host_path));
        if (!path) { syscall_result(cpu, -1); return; }
        syscall_result(cpu, (int64_t)chmod(path, (mode_t)(uint32_t)cpu->gpr[5]));
        return;
    }
    case SYS_chown: {
        const char *guest_path = irix_guest_cstr(rt, (IrixGuestAddr)(uint32_t)cpu->gpr[4], 65536u);
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
        const char *path = irix_guest_cstr(rt, (IrixGuestAddr)(uint32_t)cpu->gpr[4], 65536u);
        if (!path) { errno = EFAULT; syscall_result(cpu, -1); return; }
        syscall_result(cpu, (int64_t)rmdir(path));
        return;
    }
    case SYS_mkdir: {
        const char *path = irix_guest_cstr(rt, (IrixGuestAddr)(uint32_t)cpu->gpr[4], 65536u);
        if (!path) { errno = EFAULT; syscall_result(cpu, -1); return; }
        syscall_result(cpu, (int64_t)mkdir(path, (mode_t)(uint32_t)cpu->gpr[5]));
        return;
    }
    case SYS_rename: {
        const char *old_guest = irix_guest_cstr(rt, (IrixGuestAddr)(uint32_t)cpu->gpr[4], 65536u);
        const char *new_guest = irix_guest_cstr(rt, (IrixGuestAddr)(uint32_t)cpu->gpr[5], 65536u);
        char old_host[PATH_MAX], new_host[PATH_MAX];
        const char *oldp, *newp;
        if (!old_guest || !new_guest) { errno = EFAULT; syscall_result(cpu, -1); return; }
        oldp = irix_host_path(rt, old_guest, old_host, sizeof(old_host));
        newp = irix_host_path(rt, new_guest, new_host, sizeof(new_host));
        if (!oldp || !newp) { syscall_result(cpu, -1); return; }
        syscall_result(cpu, (int64_t)rename(oldp, newp));
        return;
    }
    case SYS_symlink: {
        const char *target = irix_guest_cstr(rt, (IrixGuestAddr)(uint32_t)cpu->gpr[4], 65536u);
        const char *linkpath = irix_guest_cstr(rt, (IrixGuestAddr)(uint32_t)cpu->gpr[5], 65536u);
        if (!target || !linkpath) { errno = EFAULT; syscall_result(cpu, -1); return; }
        syscall_result(cpu, (int64_t)symlink(target, linkpath));
        return;
    }
    case SYS_readlink: {
        const char *path = irix_guest_cstr(rt, (IrixGuestAddr)(uint32_t)cpu->gpr[4], 65536u);
        IrixGuestAddr buf_addr = (IrixGuestAddr)(uint32_t)cpu->gpr[5];
        size_t count = (size_t)(uint32_t)cpu->gpr[6];
        void *buf = irix_guest_ptr(rt, buf_addr, count);
        if (!path || (count && !buf)) { errno = EFAULT; syscall_result(cpu, -1); return; }
        syscall_result(cpu, (int64_t)readlink(path, (char *)buf, count));
        return;
    }
    case SYS_truncate: {
        const char *path = irix_guest_cstr(rt, (IrixGuestAddr)(uint32_t)cpu->gpr[4], 65536u);
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
        const char *path = irix_guest_cstr(rt, (IrixGuestAddr)(uint32_t)cpu->gpr[4], 65536u);
        if (!path) { errno = EFAULT; syscall_result(cpu, -1); return; }
        syscall_result(cpu, (int64_t)lchown(path, (uid_t)(uint32_t)cpu->gpr[5],
                                            (gid_t)(uint32_t)cpu->gpr[6]));
        return;
    }
    case SYS_getpid: /* getpid */
        syscall_result(cpu, (int64_t)getpid());
        return;
    case SYS_time: { /* time(time_t *tloc), IRIX N32 time_t is 32-bit */
        IrixGuestAddr out = (IrixGuestAddr)(uint32_t)cpu->gpr[4];
        time_t now = time(0);
        if (now == (time_t)-1) {
            syscall_result(cpu, -1);
            return;
        }
        if (out) {
            if (!irix_guest_ptr(rt, out, 4u)) {
                errno = EFAULT;
                syscall_result(cpu, -1);
                return;
            }
            irix_guest_write32be(rt, out, (uint32_t)now);
        }
        syscall_result(cpu, (int32_t)now);
        return;
    }
    case SYS_getuid: /* getuid */
        syscall_result(cpu, (int64_t)getuid());
        /* IRIX's getuid syscall returns both IDs: libc's getuid() uses v0,
           while geteuid() reads v1 when the syscall succeeds. */
        cpu->gpr[3] = (uint64_t)geteuid();
        return;
    case SYS_BSD_getime: { /* BSDgettimeofday(timeval *, timezone *) */
        IrixGuestAddr tv_addr = (IrixGuestAddr)(uint32_t)cpu->gpr[4];
        IrixGuestAddr tz_addr = (IrixGuestAddr)(uint32_t)cpu->gpr[5];
        struct timeval tv;
        if (gettimeofday(&tv, NULL) != 0) {
            syscall_result(cpu, -1);
            return;
        }
        /* IRIX N32 uses 32-bit time_t and long in struct timeval. */
        if (tv_addr) {
            if (!irix_guest_ptr(rt, tv_addr, 8u)) {
                errno = EFAULT;
                syscall_result(cpu, -1);
                return;
            }
            irix_guest_write32be(rt, tv_addr, (uint32_t)tv.tv_sec);
            irix_guest_write32be(rt, tv_addr + 4u, (uint32_t)tv.tv_usec);
        }
        if (tz_addr) {
            if (!irix_guest_ptr(rt, tz_addr, 8u)) {
                errno = EFAULT;
                syscall_result(cpu, -1);
                return;
            }
            /* The obsolete timezone result is not needed by Xlib callers. */
            irix_guest_write32be(rt, tz_addr, 0u);
            irix_guest_write32be(rt, tz_addr + 4u, 0u);
        }
        syscall_result(cpu, 0);
        return;
    }
    case SYS_access: { /* access */
        IrixGuestAddr path_addr = (IrixGuestAddr)(uint32_t)cpu->gpr[4];
        int mode = (int)(int32_t)cpu->gpr[5];
        const char *guest_path = irix_guest_cstr(rt, path_addr, 65536u);
        char host_path[PATH_MAX];
        const char *path;
        int access_rc;
        int access_errno;
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
        access_rc = access(path, mode);
        access_errno = access_rc < 0 ? errno : 0;
        
        if (access_rc < 0)
            errno = access_errno;
        syscall_result(cpu, (int64_t)access_rc);
        return;
    }
    case SYS_syssgi: /* syssgi */
        irix_compat_syssgi(rt, cpu);
        return;
    case SYS_getgid: /* getgid */
        syscall_result(cpu, (int64_t)getgid());
        /* Match the paired real/effective GID return used by IRIX libc. */
        cpu->gpr[3] = (uint64_t)getegid();
        return;
    case SYS_sysmp: /* sysmp(cmd, ...) */
        irix_compat_sysmp(rt, cpu);
        return;
    case SYS_sysget: { /* sysget(name, buf, buflen, flags, cookie) */
        int err = irix_compat_sysget(rt, cpu);
        if (err) {
            errno = err;
            syscall_result(cpu, -1);
        } else {
            syscall_result(cpu, 0);
        }
        return;
    }
    case SYS_times: { /* times(struct tms *): four 32-bit tick counts */
        IrixGuestAddr out = (IrixGuestAddr)(uint32_t)cpu->gpr[4];
        struct tms t;
        clock_t r = times(&t);
        if (r == (clock_t)-1) {
            syscall_result(cpu, -1);
            return;
        }
        if (out) {
            if (!irix_guest_ptr(rt, out, 16u)) {
                errno = EFAULT;
                syscall_result(cpu, -1);
                return;
            }
            irix_guest_write32be(rt, out, (uint32_t)t.tms_utime);
            irix_guest_write32be(rt, out + 4u, (uint32_t)t.tms_stime);
            irix_guest_write32be(rt, out + 8u, (uint32_t)t.tms_cutime);
            irix_guest_write32be(rt, out + 12u, (uint32_t)t.tms_cstime);
        }
        syscall_result(cpu, (int64_t)(uint32_t)r);
        return;
    }
    case SYS_pagelock: /* pagelock(addr, len, op): pin pages; nothing to do */
        syscall_result(cpu, 0);
        return;
    case SYS_utssys: { /* utssys(buf, mv, type): libc uname() is type 0 */
        IrixGuestAddr buf = (IrixGuestAddr)(uint32_t)cpu->gpr[4];
        uint32_t type = (uint32_t)cpu->gpr[6];
        if (type == 0u) { /* UTS_UNAME: same identity as the uname bridge */
            enum { NMLN = 257, FIELDS = 13 };  /* sizeof(struct utsname) */
            char host[NMLN];
            unsigned char *p = (unsigned char *)irix_guest_ptr(
                rt, buf, (size_t)NMLN * FIELDS);
            if (!p) {
                errno = EFAULT;
                syscall_result(cpu, -1);
                return;
            }
            if (gethostname(host, sizeof(host)) != 0 || !host[0])
                strcpy(host, "IRIS");
            memset(p, 0, (size_t)NMLN * FIELDS);
            strncpy((char *)p + 0 * NMLN, "IRIX", NMLN - 1);
            strncpy((char *)p + 1 * NMLN, host, NMLN - 1);
            strncpy((char *)p + 2 * NMLN, "6.5", NMLN - 1);
            strncpy((char *)p + 3 * NMLN, "6.5.7", NMLN - 1);
            strncpy((char *)p + 4 * NMLN, irix_kinfo_machine(), NMLN - 1);
            syscall_result(cpu, 0);
            return;
        }
        errno = EINVAL;
        syscall_result(cpu, -1);
        return;
    }
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
        const char *guest_path = irix_guest_cstr(rt, path_addr, 65536u);
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
        const char *guest_path = irix_guest_cstr(rt, path_addr, 65536u);
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

    (void)0;

    if (wc_num == 0u) {
        irix_guest_write32be(rt, wc_ptrp, sc);
        irix_guest_write32be(rt, wc_nump, sc_num);
        cpu->gpr[2] = 0u;
        cpu->gpr[0] = 0u;
        return rt->trapped ? -1 : 0;
    }

    if (wc_num > 4096u || sc_num > 4096u) {
        /*
         * Stability policy: a malformed superclass side must not leave the
         * current class half-initialized.  The original routine only replaces
         * *wc_ptrp/*wc_nump after it has built a complete merged table, so if
         * the current widget-class table is already sane, preserving it and
         * dropping the impossible superclass merge is the safest equivalent
         * failure mode.
         *
         * One repeatedly observed corruption is sc_num carrying a heap
         * pointer (for example 0x3000ad00) instead of a count.  Returning an
         * error without repairing anything allowed later Xm private fields to
         * inherit tiny scalar poison such as 0x37a/0x37c/0x37e.
         */
        if (wc_num <= 4096u &&
            (!wc_num || (wc && irix_guest_ptr(rt, wc, wc_num * rec_size)))) {
            fprintf(stderr,
                    "IRIX native _XmBuildResources: malformed superclass "
                    "merge dropped wc=%08x/%u sc=%08x/%u\n",
                    wc, wc_num, sc, sc_num);
            cpu->gpr[2] = 0u;
            cpu->gpr[0] = 0u;
            return rt->trapped ? -1 : 0;
        }

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

    (void)0;

    cpu->gpr[2] = 0u;
    cpu->gpr[0] = 0u;
    return rt->trapped ? -1 : 0;
}

static int irix_native_xt_gc_alloc(IrixGuestRuntime *rt,
                                   IrixGuestCpu *cpu,
                                   int allocate_form)
{
    IrixGuestAddr widget = (IrixGuestAddr)(uint32_t)cpu->gpr[4];
    IrixGuestAddr screen;
    IrixGuestAddr display;
    uint32_t drawable;
    uint32_t mask;
    IrixGuestAddr values;
    IrixGuestAddr gc;

    if (!widget || !irix_guest_ptr(rt, widget, 0x5cu))
        return -1;

    screen = irix_guest_read32be(rt, widget + 0x58u);
    if (!screen || !irix_guest_ptr(rt, screen, 8u))
        return -1;

    /*
     * IRIX/N32 Screen prefix mirrored by IrixX11BridgeExtra:
     *   +0x00 ext_data/private
     *   +0x04 Display *
     *   +0x08 root Window
     */
    display = irix_guest_read32be(rt, screen + 0x04u);
    drawable = irix_guest_read32be(rt, screen + 0x08u);

    /*
     * XtGetGC(widget, value_mask, values)
     * XtAllocateGC(widget, depth, value_mask, values,
     *              dynamic_mask, unused_mask)
     *
     * The dynamic/unused masks control Xt's private cache behavior.  The
     * direct compatibility path deliberately does not cache, so only the
     * actual XGCValues mask is relevant to XCreateGC.
     */
    if (allocate_form) {
        mask = (uint32_t)cpu->gpr[6];
        values = (IrixGuestAddr)(uint32_t)cpu->gpr[7];
    } else {
        mask = (uint32_t)cpu->gpr[5];
        values = (IrixGuestAddr)(uint32_t)cpu->gpr[6];
    }

    gc = irix_x11_primary_create_gc(rt, display, drawable, mask, values);
    

    cpu->gpr[2] = (uint64_t)(int64_t)(int32_t)gc;
    cpu->gpr[0] = 0;
    return gc ? 0 : -1;
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

/*
 * Synthetic-stack Motif BaseClass record-type coherence.
 *
 * GUI stability mode redirects synthetic Xt/Motif FUNCTIONS to the original
 * mapped DSOs, but the synthetic class DATA remains at 0x50000000+.  Thus
 * _XmInitializeExtensions initializes the DSO Motif state while static
 * XmBaseClassExtRec instances in the synthetic stack can retain the
 * NULLQUARK record_type from their compile-time initializer.
 *
 * Xt's xtWidgetAlloc asks XtGetClassExtension for a NULLQUARK
 * ObjectClassExtension.  A Motif BCE with record_type==0, version==2 and
 * record_size==0x78 then falsely matches, causing its initializePrehook at
 * +0x10 to be called as XtAllocateProc.
 *
 * Repair only that demonstrated SGI/N32 BCE shape at the extension lookup
 * boundary.  The Xrm bridge supplies the same quark namespace used by guest
 * Xrm calls.
 */
static int irix_guest_repair_synthetic_motif_bce_class(
    IrixGuestRuntime *rt, IrixGuestAddr cls)
{
    IrixGuestAddr ext;
    unsigned depth;

    if (cls < 0x50000000u || cls >= 0x70000000u ||
        !irix_guest_ptr(rt, cls + 0x70u, 4u))
        return 0;

    ext = irix_guest_read32be(rt, cls + 0x70u);
    for (depth = 0; ext && depth < 16u; ++depth) {
        IrixGuestAddr next;
        uint32_t record_type;
        uint32_t version;
        uint32_t record_size;

        if (!irix_guest_ptr(rt, ext, 0x10u))
            break;

        next = irix_guest_read32be(rt, ext + 0x00u);
        record_type = irix_guest_read32be(rt, ext + 0x04u);
        version = irix_guest_read32be(rt, ext + 0x08u);
        record_size = irix_guest_read32be(rt, ext + 0x0cu);

        if (record_type == 0u && version == 2u &&
            record_size == 0x78u) {
            uint32_t motif_q =
                irix_guest_xrm_quark_literal(rt, "OSF_MOTIF");
            if (motif_q) {
                irix_guest_write32be(rt, ext + 0x04u, motif_q);
                (void)0;
                return 1;
            }
            return 0;
        }

        ext = next;
    }
    return 0;
}

static void irix_guest_repair_synthetic_motif_bce_lookup(
    IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr cls = (IrixGuestAddr)(uint32_t)cpu->gpr[4];
    uint32_t requested_type = (uint32_t)cpu->gpr[6];

    /*
     * NULLQUARK is also the lookup key used by Xt's private class-extension
     * machinery.  A synthetic Motif BCE left at its compile-time NULLQUARK
     * can therefore alias *any* such lookup, not only one caller's expected
     * core-class offset.  The repair helper is already fenced to synthetic
     * classes and the exact Motif BCE {version=2,size=0x78} shape, so apply it
     * for every NULLQUARK lookup before Xt walks the extension list.
     */
    if (requested_type == 0u)
        (void)irix_guest_repair_synthetic_motif_bce_class(rt, cls);
}

static IrixGuestAddr irix_guest_find_motif_bce(
    IrixGuestRuntime *rt, IrixGuestAddr cls)
{
    IrixGuestAddr ext;
    uint32_t motif_q;
    unsigned depth;

    if (!cls || !irix_guest_ptr(rt, cls + 0x70u, 4u))
        return 0u;

    motif_q = irix_guest_xrm_quark_literal(rt, "OSF_MOTIF");
    ext = irix_guest_read32be(rt, cls + 0x70u);

    for (depth = 0; ext && depth < 32u; ++depth) {
        IrixGuestAddr next;
        uint32_t record_type;
        uint32_t version;
        uint32_t record_size;

        if (!irix_guest_ptr(rt, ext, 0x78u))
            return 0u;

        next = irix_guest_read32be(rt, ext + 0x00u);
        record_type = irix_guest_read32be(rt, ext + 0x04u);
        version = irix_guest_read32be(rt, ext + 0x08u);
        record_size = irix_guest_read32be(rt, ext + 0x0cu);

        if (version == 2u && record_size == 0x78u &&
            (record_type == motif_q ||
             (record_type == 0u &&
              cls >= 0x50000000u && cls < 0x70000000u)))
            return ext;

        ext = next;
    }
    return 0u;
}
/*
 * Mirror Motif BaseClassPartInitialize's inheritance folding for the
 * XmBaseClassExtRec method slots that use _XtInherit as their sentinel.
 *
 * All of these XmInherit... macros are casts of the same _XtInherit address.
 * Resolve the superclass first, then replace only exact sentinel values in
 * the subclass.  NULL is a legitimate inherited value and must be copied.
 */
static unsigned irix_guest_repair_motif_bce_inheritance_depth(
    IrixGuestRuntime *rt, IrixGuestAddr cls, unsigned depth)
{
    const IrixGuestAddr xt_inherit = 0x0f6588b8u;
    static const uint8_t inherit_offsets[] = {
        0x10u, /* initializePrehook */
        0x14u, /* setValuesPrehook */
        0x18u, /* initializePosthook */
        0x1cu, /* setValuesPosthook */
        0x24u, /* secondaryObjectCreate */
        0x28u, /* getSecResData */
        0x4cu, /* getValuesPrehook */
        0x50u, /* getValuesPosthook */
        0x54u, /* classPartInitPrehook */
        0x58u, /* classPartInitPosthook */
        0x6cu, /* widgetNavigable */
        0x70u  /* focusChange */
    };
    IrixGuestAddr super;
    IrixGuestAddr ext;
    IrixGuestAddr super_ext;
    unsigned repaired = 0u;
    unsigned i;

    if (!cls || depth >= 64u ||
        !irix_guest_looks_like_widget_class(rt, cls))
        return 0u;

    (void)irix_guest_repair_synthetic_motif_bce_class(rt, cls);
    ext = irix_guest_find_motif_bce(rt, cls);
    super = irix_guest_read32be(rt, cls + 0x00u);

    if (super)
        repaired += irix_guest_repair_motif_bce_inheritance_depth(
            rt, super, depth + 1u);

    if (!ext || !super ||
        !irix_guest_looks_like_widget_class(rt, super))
        return repaired;

    (void)irix_guest_repair_synthetic_motif_bce_class(rt, super);
    super_ext = irix_guest_find_motif_bce(rt, super);
    if (!super_ext)
        return repaired;

    for (i = 0u; i < sizeof(inherit_offsets); ++i) {
        uint32_t off = inherit_offsets[i];
        IrixGuestAddr value =
            irix_guest_read32be(rt, ext + off);

        if (value == xt_inherit) {
            IrixGuestAddr inherited =
                irix_guest_read32be(rt, super_ext + off);

            /*
             * If the superclass is itself still unresolved, leave this slot
             * alone.  Otherwise copy its value exactly, including NULL.
             */
            if (inherited != xt_inherit) {
                irix_guest_write32be(rt, ext + off, inherited);
                ++repaired;
                (void)0;
            }
        }
    }

    return repaired;
}

static unsigned irix_guest_repair_motif_bce_inheritance(
    IrixGuestRuntime *rt, IrixGuestAddr cls)
{
    return irix_guest_repair_motif_bce_inheritance_depth(rt, cls, 0u);
}

/*
 * SGI Motif installs four BaseClass root wrappers into Xt's Object class
 * from _XmInitializeExtensions():
 *
 *   class_part_initialize = ClassPartInitRootWrapper
 *   initialize            = InitializeRootWrapper
 *   set_values            = SetValuesRootWrapper
 *   get_values_hook       = GetValuesRootWrapper
 *
 * In direct-main the real Xt Object class can later be observed with those
 * slots back at their original NULL values.  That removes SGI's
 * _SG_ClassPartInitialize/_SG_XtInitialize lifecycle entirely: class-private
 * descriptors are never registered and per-instance SGI state (for example
 * XmManager's final pointer at +0xe8) remains NULL.
 *
 * Restore exactly the mutations made by the original SGI
 * _XmInitializeExtensions, only after XmQmotif has been initialized and only
 * when a slot has fallen back to NULL.  Never replace another non-NULL owner.
 */
static unsigned irix_guest_restore_sg_motif_root_wrappers(
    IrixGuestRuntime *rt, IrixGuestAddr cls)
{
    static const struct {
        uint32_t offset;
        IrixGuestAddr wrapper;
        const char *name;
    } wrappers[] = {
        { 0x10u, 0x0b2969b0u, "class_part_initialize" },
        { 0x18u, 0x0b296b58u, "initialize" },
        { 0x48u, 0x0b297124u, "set_values" },
        { 0x54u, 0x0b2975f4u, "get_values_hook" }
    };
    IrixGuestAddr root;
    IrixGuestAddr name_addr;
    const char *name;
    uint32_t motif_quark;
    unsigned depth;
    unsigned repaired = 0u;
    unsigned i;

    if (!rt || !cls || !irix_guest_looks_like_widget_class(rt, cls))
        return 0u;

    /* XmQmotif is assigned by the original _XmInitializeExtensions(). */
    if (!irix_guest_ptr(rt, 0x0b3f8a30u, 4u))
        return 0u;
    motif_quark = irix_guest_read32be(rt, 0x0b3f8a30u);
    if (!motif_quark)
        return 0u;

    root = cls;
    for (depth = 0u; depth < 64u; ++depth) {
        IrixGuestAddr super;

        if (!irix_guest_looks_like_widget_class(rt, root))
            return repaired;
        super = irix_guest_read32be(rt, root + 0x00u);
        if (!super)
            break;
        root = super;
    }
    if (depth >= 64u || !irix_guest_ptr(rt, root, 0x58u))
        return repaired;

    name_addr = irix_guest_read32be(rt, root + 0x04u);
    name = irix_guest_cstr(rt, name_addr, 64u);
    if (!name || strcmp(name, "Object") != 0)
        return repaired;

    for (i = 0u; i < sizeof(wrappers) / sizeof(wrappers[0]); ++i) {
        IrixGuestAddr slot = root + wrappers[i].offset;
        IrixGuestAddr old;

        if (!irix_guest_ptr(rt, slot, 4u))
            continue;
        old = irix_guest_read32be(rt, slot);
        if (old == wrappers[i].wrapper)
            continue;

        /*
         * Once XmQmotif is initialized, SGI Motif owns these four Object
         * lifecycle slots for the lifetime of the process.  Direct-main can
         * leave a stale translated/original Xt value here rather than NULL;
         * treating only NULL as damaged skips InitializeRootWrapper and, in
         * turn, _SG_XtInitialize.  That leaves SGI's per-instance Manager and
         * Primitive private records unallocated (for example XmManager +0xe8).
         *
         * Restore the exact original libXm wrapper even when the stale slot
         * is non-NULL.  This is the state established by SGI's own
         * _XmInitializeExtensions(), not a per-widget substitute.
         */
        irix_guest_write32be(rt, slot, wrappers[i].wrapper);
        ++repaired;
        fprintf(stderr,
                "IRIX SGI Motif root-wrapper restore: "
                "Object=%08x slot=+%02x %s %08x -> %08x "
                "for class=%08x XmQmotif=%08x\n",
                root, wrappers[i].offset, wrappers[i].name,
                old, wrappers[i].wrapper, cls, motif_quark);
    }

    return repaired;
}

static IrixGuestAddr irix_guest_resolve_motif_bce_method(
    IrixGuestRuntime *rt, IrixGuestAddr cls, uint32_t offset)
{
    const IrixGuestAddr xt_inherit = 0x0f6588b8u;
    IrixGuestAddr ext;
    IrixGuestAddr method;

    if (!irix_guest_looks_like_widget_class(rt, cls))
        return 0u;

    (void)irix_guest_repair_motif_bce_inheritance(rt, cls);
    ext = irix_guest_find_motif_bce(rt, cls);
    if (!ext || offset > 0x74u ||
        !irix_guest_ptr(rt, ext + offset, 4u))
        return 0u;

    method = irix_guest_read32be(rt, ext + offset);
    return method == xt_inherit ? 0u : method;
}


/*
 * Resolve XmBaseClassExt.widgetNavigable inheritance lazily at the exact
 * point Motif proves class initialization left the sentinel behind.
 *
 * SGI/N32 XmBaseClassExtRec is 0x78 bytes and widgetNavigable is +0x6c.
 * _XmGetNavigability must never call _XtInherit directly: that value means
 * "copy the superclass method during class-part initialization".
 */
static IrixGuestAddr irix_guest_resolve_xm_widget_navigable(
    IrixGuestRuntime *rt, IrixGuestAddr widget)
{
    const IrixGuestAddr xt_inherit = 0x0f6588b8u;
    IrixGuestAddr cls;
    IrixGuestAddr ext;
    IrixGuestAddr method;
    IrixGuestAddr super;
    unsigned depth;

    if (!irix_guest_looks_like_widget(rt, widget))
        return 0u;

    cls = irix_guest_read32be(rt, widget + 0x04u);
    if (!irix_guest_looks_like_widget_class(rt, cls))
        return 0u;

    (void)irix_guest_repair_synthetic_motif_bce_class(rt, cls);
    ext = irix_guest_find_motif_bce(rt, cls);
    if (!ext)
        return 0u;

    method = irix_guest_read32be(rt, ext + 0x6cu);
    if (method && method != xt_inherit)
        return method;
    if (method != xt_inherit)
        return 0u;

    super = irix_guest_read32be(rt, cls + 0x00u);
    for (depth = 0; super && depth < 64u; ++depth) {
        IrixGuestAddr super_ext;
        IrixGuestAddr candidate;

        if (!irix_guest_looks_like_widget_class(rt, super))
            break;

        (void)irix_guest_repair_synthetic_motif_bce_class(rt, super);
        super_ext = irix_guest_find_motif_bce(rt, super);
        if (super_ext) {
            candidate = irix_guest_read32be(rt, super_ext + 0x6cu);
            if (candidate && candidate != xt_inherit) {
                IrixGuestAddr class_name_addr =
                    irix_guest_read32be(rt, cls + 0x04u);
                IrixGuestAddr super_name_addr =
                    irix_guest_read32be(rt, super + 0x04u);
                const char *class_name =
                    irix_guest_cstr(rt, class_name_addr, 256u);
                const char *super_name =
                    irix_guest_cstr(rt, super_name_addr, 256u);

                irix_guest_write32be(rt, ext + 0x6cu, candidate);
                (void)0;
                return candidate;
            }
        }

        super = irix_guest_read32be(rt, super + 0x00u);
    }

    fprintf(stderr,
            "IRIX Xm widgetNavigable inheritance unresolved: "
            "widget=%08x class=%08x ext=%08x\n",
            widget, cls, ext);
    return 0u;
}

/*
 * SGI/N32 CompositeClassPart compatibility.
 *
 * Xt's _XtCreateWidget uses XtGetClassExtension(parent_class, 0x84,
 * NULLQUARK, 1, 0) before allowing a non-Core child.  Any Composite-derived
 * class may intentionally accept RectObj/Object children; Motif Manager is
 * only one such family.  The IRIX desktop's "desktop" parent is another.
 *
 * Hybrid class initialization can lose that extension or leave its Boolean
 * cleared.  Repair Composite-derived parents structurally rather than keying
 * the policy to the XmManager class name.
 */
static int irix_guest_class_derives_from_named(IrixGuestRuntime *rt,
                                               IrixGuestAddr cls,
                                               const char *wanted)
{
    unsigned depth;

    for (depth = 0; cls && depth < 64u; ++depth) {
        IrixGuestAddr name_addr;
        const char *name;

        if (!irix_guest_ptr(rt, cls, 0x88u))
            return 0;
        name_addr = irix_guest_read32be(rt, cls + 0x04u);
        name = irix_guest_cstr(rt, name_addr, 256u);
        if (name && strcmp(name, wanted) == 0)
            return 1;
        cls = irix_guest_read32be(rt, cls + 0x00u);
    }
    return 0;
}

static IrixGuestAddr irix_guest_find_composite_extension(
    IrixGuestRuntime *rt, IrixGuestAddr cls)
{
    IrixGuestAddr ext;
    unsigned depth;

    if (!cls || !irix_guest_ptr(rt, cls, 0x88u))
        return 0;

    ext = irix_guest_read32be(rt, cls + 0x84u);
    for (depth = 0; ext && depth < 64u; ++depth) {
        uint32_t record_type;
        uint32_t version;
        uint32_t record_size;

        if (!irix_guest_ptr(rt, ext, 0x14u))
            return 0;
        record_type = irix_guest_read32be(rt, ext + 0x04u);
        version = irix_guest_read32be(rt, ext + 0x08u);
        record_size = irix_guest_read32be(rt, ext + 0x0cu);
        if (record_type == 0u && version >= 1u && record_size >= 0x14u)
            return ext;
        ext = irix_guest_read32be(rt, ext + 0x00u);
    }
    return 0;
}

static void irix_guest_repair_motif_accepts_objects(IrixGuestRuntime *rt,
                                                    IrixGuestAddr parent)
{
    IrixGuestAddr cls;
    IrixGuestAddr ext;
    IrixGuestAddr old_head;
    IrixGuestAddr inherited = 0u;
    IrixGuestAddr super;
    IrixGuestAddr fresh;
    uint32_t record_size = 0x14u;
    uint8_t allows_change = 0u;
    unsigned depth;

    if (!parent || !irix_guest_ptr(rt, parent, 8u))
        return;
    cls = irix_guest_read32be(rt, parent + 0x04u);
    if (!irix_guest_class_derives_from_named(rt, cls, "Composite") &&
        !irix_guest_class_derives_from_named(rt, cls, "Constraint") &&
        !irix_guest_class_derives_from_named(rt, cls, "XmManager"))
        return;

    ext = irix_guest_find_composite_extension(rt, cls);
    if (ext) {
        if (irix_guest_read8(rt, ext + 0x10u) == 0u) {
            irix_guest_write8(rt, ext + 0x10u, 1u);
            (void)0;
        }
        return;
    }

    /*
     * Prefer the nearest superclass's valid CompositeClassExtension as the
     * shape/source for the compatibility record.  Preserve this class's
     * existing extension list by prepending rather than overwriting it.
     */
    super = irix_guest_read32be(rt, cls + 0x00u);
    for (depth = 0; super && depth < 64u; ++depth) {
        inherited = irix_guest_find_composite_extension(rt, super);
        if (inherited && irix_guest_read8(rt, inherited + 0x10u))
            break;
        inherited = 0u;
        if (!irix_guest_ptr(rt, super, 4u))
            break;
        super = irix_guest_read32be(rt, super + 0x00u);
    }

    /*
     * A missing inherited extension is itself one of the hybrid-init failure
     * modes.  The CompositeClassExtension header is fixed-size enough for the
     * fields Xt consults here: next, record_type, version, record_size,
     * accepts_objects and allows_change_managed_set.  Build the minimal valid
     * record even when no superclass copy survived initialization.
     */
    if (inherited) {
        record_size = irix_guest_read32be(rt, inherited + 0x0cu);
        if (record_size < 0x14u || record_size > 0x100u)
            record_size = 0x14u;
        if (irix_guest_ptr(rt, inherited, 0x12u))
            allows_change = irix_guest_read8(rt, inherited + 0x11u);
    }

    fresh = irix_guest_xt_compat_alloc(rt, record_size);
    if (!fresh || !irix_guest_ptr(rt, fresh, record_size))
        return;

    memset(irix_guest_ptr(rt, fresh, record_size), 0, record_size);
    old_head = irix_guest_read32be(rt, cls + 0x84u);
    irix_guest_write32be(rt, fresh + 0x00u, old_head);
    irix_guest_write32be(rt, fresh + 0x04u, 0u);
    irix_guest_write32be(rt, fresh + 0x08u, 1u);
    irix_guest_write32be(rt, fresh + 0x0cu, record_size);
    irix_guest_write8(rt, fresh + 0x10u, 1u);
    irix_guest_write8(rt, fresh + 0x11u, allows_change);
    irix_guest_write32be(rt, cls + 0x84u, fresh);

    (void)0;
}

static int irix_guest_repair_xt_widget_alloc(IrixGuestRuntime *rt,
                                             IrixGuestCpu *cpu,
                                             IrixGuestAddr caller_sp,
                                             IrixGuestAddr widget_class,
                                             IrixGuestAddr csize_ptr,
                                             IrixGuestAddr out_slot)
{
    /*
     * The recovered Motif allocate callback can be entered at an interior PC
     * after it has made its own stack frame.  Therefore caller_sp is NOT a
     * reliable way to recover Xt's &csize / &widget argument locations.
     *
     * N32 gives XtAllocateProc eight register arguments:
     *   a1 = &csize
     *   a7 = &widget
     * Preserve and use those pointers directly.  Keep caller_sp only as a
     * guarded compatibility fallback for older call sites.
     */
    if (!out_slot || !irix_guest_ptr(rt, out_slot, 4u))
        out_slot = caller_sp + 0x20u;
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

    if (csize_ptr && irix_guest_ptr(rt, csize_ptr, 4u))
        csize = irix_guest_read32be(rt, csize_ptr);
    else if (irix_guest_ptr(rt, caller_sp + 0x10u, 4u))
        csize = irix_guest_read32be(rt, caller_sp + 0x10u);
    else
        return 0;
    wsize = irix_guest_read32be(rt, widget_class + 0x08u);
    if (!wsize || wsize > 0x01000000u || csize > 0x01000000u)
        return 0;

    aligned_wsize = wsize;
    if (csize && (csize & 7u) == 0u)
        aligned_wsize = (wsize + 7u) & ~7u;

    total = (uint64_t)aligned_wsize + (uint64_t)csize;
    if (!total || total > UINT32_MAX)
        return 0;

    /*
     * An extension allocator may have filled &widget already.  Accept that
     * result only if the complete object/tail range is actually guest-mapped.
     * A nonzero scalar or unmapped stale pointer must not suppress the normal
     * allocation fallback and then explode on Xt's first widget store.
     */
    widget = irix_guest_read32be(rt, out_slot);
    if (widget) {
        uint32_t sp = (uint32_t)caller_sp;
        uint32_t w = (uint32_t)widget;
        int stack_alias =
            (w >= sp - 0x00010000u && w <= sp + 0x00010000u);

        if (!stack_alias && irix_guest_ptr(rt, widget, (size_t)total))
            return 0;
        fprintf(stderr,
                "IRIX xtWidgetAlloc invalid output repair: "
                "class=%08x widget=%08x total=%u stack_alias=%d "
                "caller_sp=%08x -> replace\n",
                widget_class, widget, (uint32_t)total,
                stack_alias, caller_sp);
        irix_guest_write32be(rt, out_slot, 0u);
    }

    widget = irix_libc_guest_malloc((uint32_t)total);
    p = widget ? irix_guest_ptr(rt, widget, (size_t)total) : NULL;
    if (!widget || !p)
        return 0;
    memset(p, 0, (size_t)total);

    if (csize)
        constraints = widget + aligned_wsize;
    irix_guest_write32be(rt, widget + 0x18u, constraints);
    irix_guest_write32be(rt, out_slot, widget);

    if (widget_class == g_xt_alloc_widget_class_hint)
        g_xt_alloc_widget_hint = widget;

    fprintf(stderr,
            "IRIX xtWidgetAlloc fallback: class=%08x wsize=%u csize=%u "
            "widget=%08x constraints=%08x csize_ptr=%08x out_slot=%08x\\n",
            widget_class, wsize, csize, widget, constraints,
            csize_ptr, out_slot);
    cpu->gpr[2] = widget;
    return rt->trapped ? -1 : 1;
}

/*
 * SGI Motif VendorExtInitialize compatibility repair.
 *
 * The current XmDisplay bring-up reaches VendorExtInitialize with a valid
 * VendorShellExt object in s0, but at 0x0b314ffc the extParent value in a0 is
 * NULL immediately before code dereferences extParent+8.  SGI's XmExtPart is
 * layout-stable here: logicalParent is +0x1c and extensionType is +0x20.
 *
 * Only repair this exact demonstrated instruction, and only when s0 really
 * looks like an XmSHELL_EXTENSION object (extensionType == 3).  Prefer the
 * object's own logicalParent.  If that field is missing, use the in-progress
 * XmDisplay widget captured from Xt's allocation path and repair the object as
 * well, matching SecondaryObjectCreate's required invariant.
 */
int irix_guest_repair_xm_vendor_ext_parent(IrixGuestRuntime *rt,
                                           IrixGuestCpu *cpu,
                                           IrixGuestAddr pc)
{
    IrixGuestAddr ext;
    IrixGuestAddr parent = 0u;
    IrixGuestAddr logical_parent = 0u;

    if (!rt || !cpu || pc != 0x0b314ffcu ||
        (uint32_t)cpu->gpr[4] != 0u)
        return 0;

    /*
     * This is the exact SGI VendorExtInitialize dereference demonstrated by
     * the trace.  Do not require a guessed XmExtPart extensionType offset
     * here: on this IRIX Motif build that layout assumption rejected the
     * live object and the repair never fired.
     *
     * Prefer a plausible logicalParent from the secondary object when the
     * conventional +0x1c slot contains one.  Otherwise use the live shell
     * Widget created by Xt's allocation path.  We intentionally do not write
     * the guessed +0x1c field in the fallback case; only repair a0 so the
     * original SGI instruction stream can continue without corrupting an
     * uncertain private-object layout.
     */
    ext = (IrixGuestAddr)(uint32_t)cpu->gpr[16];
    if (ext && irix_guest_ptr(rt, ext, 0x20u))
        logical_parent = irix_guest_read32be(rt, ext + 0x1cu);

    if (irix_guest_looks_like_widget(rt, logical_parent))
        parent = logical_parent;
    else if (irix_guest_looks_like_widget(rt, g_xt_alloc_widget_hint))
        parent = g_xt_alloc_widget_hint;
    else {
        /*
         * Bring-up fallback for recovered VendorExtInitialize interiors.
         * The canonical shell hint can be cleared before this late Motif path
         * is revisited.  Prefer live widget-shaped saved registers from the
         * surrounding SGI frame rather than dying on extParent == NULL.
         */
        static const unsigned saved_candidates[] = { 23u, 22u, 20u, 19u, 18u };
        size_t i;

        for (i = 0; i < sizeof(saved_candidates) / sizeof(saved_candidates[0]); ++i) {
            IrixGuestAddr candidate =
                (IrixGuestAddr)(uint32_t)cpu->gpr[saved_candidates[i]];
            if (irix_guest_looks_like_widget(rt, candidate)) {
                parent = candidate;
                (void)0;
                break;
            }
        }

        if (!parent) {
            IrixGuestAddr a3 =
                (IrixGuestAddr)(uint32_t)cpu->gpr[7];
            if (a3 && irix_guest_ptr(rt, a3, 0x18u)) {
                IrixGuestAddr candidate =
                    irix_guest_read32be(rt, a3 + 0x10u);
                if (irix_guest_looks_like_widget(rt, candidate)) {
                    parent = candidate;
                    (void)0;
                }
            }
        }

        if (!parent)
            return 0;
    }

    (void)0;

    cpu->gpr[4] = (uint64_t)(int64_t)(int32_t)parent;
    cpu->gpr[0] = 0;
    return 1;
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
    static int motif_converters_forced;
    static IrixGuestAddr last_xt_convert_widget;
    static IrixGuestAddr last_xt_display;

    
    

    /*
     * Stability-first Xt GC cache bypass.
     *
     * SGI Xt's private GC-cache list is corrupted in the hybrid runtime
     * before first desktop realization.  This host Xlib bridge is only valid
     * for that static hybrid path: a generic original-image process owns an
     * SGI Xlib Display and must run the original Xt GC allocator instead.
     */
    if (address == 0x0f61cd84u &&
        getenv("IRIX_EXEC_BACKEND") &&
        strcmp(getenv("IRIX_EXEC_BACKEND"), "static") == 0) { /* XtGetGC */
        
        return irix_native_xt_gc_alloc(rt, cpu, 0);
    }
    if (address == 0x0f61cdccu &&
        getenv("IRIX_EXEC_BACKEND") &&
        strcmp(getenv("IRIX_EXEC_BACKEND"), "static") == 0) { /* XtAllocateGC */
        
        return irix_native_xt_gc_alloc(rt, cpu, 1);
    }

    /*
     * Emergency bring-up: XtRealizeWidget(NULL) is a no-op.
     *
     * 4Dwm reaches XtRealizeWidget with a NULL optional widget after class
     * initialization.  Stock Xt turns this into the fatal
     * "Object (null) does not have windowed ancestor" path.  There is no
     * object to realize, so skipping only the NULL case is safe and lets the
     * WM continue bringing up its remaining shells.
     */
    if (address == 0x0f62d5f4u &&
        (uint32_t)cpu->gpr[4] == 0u) {
        (void)0;
        cpu->gpr[2] = 0u;
        cpu->gpr[0] = 0u;
        return 0;
    }

    /*
     * BaseClassPartInitialize must see XmQmotif before it resolves inherited
     * Motif hooks.  Repair the static BCE before Xt enters class
     * initialization, then keep the later ObjectClassExtension lookup guarded
     * as well.
     */
    if (entry && entry->name &&
        strcmp(entry->name, "XtInitializeWidgetClass") == 0) {
        IrixGuestAddr init_cls =
            (IrixGuestAddr)(uint32_t)cpu->gpr[4];

        /*
         * The first Motif class can reach XtInitializeWidgetClass before
         * XmQmotif has been assigned.  In that ordering,
         * irix_guest_restore_sg_motif_root_wrappers() deliberately does
         * nothing, then Manager/Primitive class-part initialization runs
         * without SGI's Object root wrappers.  _SG_ClassPartInitialize never
         * registers the per-class private descriptor and _SG_XtInitialize
         * consequently has nothing to allocate for instances (XmManager's
         * +0xe8 and XmPrimitive's corresponding private pointer stay NULL).
         *
         * Run SGI's own process-global extension initializer before Xt starts
         * recursively initializing the first Manager/Primitive-family class.
         * This is the same initializer Motif's ClassInitialize calls; doing it
         * here only repairs direct-main's ordering split.  Preserve the
         * caller's CPU state and keep all state created by libXm itself.
         */
        if (irix_guest_ptr(rt, 0x0b3f8a30u, 4u) &&
            irix_guest_read32be(rt, 0x0b3f8a30u) == 0u &&
            (irix_guest_class_derives_from_named(rt, init_cls, "XmManager") ||
             irix_guest_class_derives_from_named(rt, init_cls, "XmPrimitive"))) {
            IrixGuestFunctionEntry *xe = NULL;
            size_t xi;

            for (xi = 0; xi < rt->function_count; ++xi) {
                IrixGuestFunctionEntry *candidate = &rt->functions[xi];

                if (!candidate->name ||
                    strcmp(candidate->name, "_XmInitializeExtensions") != 0)
                    continue;
                if (candidate->address >= 0x20000000u ||
                    (candidate->flags & IRIX_GUEST_FUNCTION_NATIVE) ||
                    !irix_guest_ptr(rt, candidate->address, 4u))
                    continue;
                xe = candidate;
                break;
            }

            if (xe) {
                IrixGuestCpu saved_cpu = *cpu;
                IrixGuestAddr target_gp =
                    irix_guest_gp_for_address(xe->address);
                int xrc;

                fprintf(stderr,
                        "IRIX SGI Motif bootstrap: "
                        "_XmInitializeExtensions @ %08x before class=%08x\n",
                        xe->address, init_cls);
                if (target_gp)
                    cpu->gpr[28] =
                        (uint64_t)(int64_t)(int32_t)target_gp;
                cpu->gpr[25] =
                    (uint64_t)(int64_t)(int32_t)xe->address;
                xrc = irix_mips_interp_call(rt, cpu, xe->address);
                if (!rt->trapped && xrc == 0) {
                    *cpu = saved_cpu;
                    fprintf(stderr,
                            "IRIX SGI Motif bootstrap: "
                            "_XmInitializeExtensions complete "
                            "XmQmotif=%08x\n",
                            irix_guest_read32be(rt, 0x0b3f8a30u));
                } else {
                    fprintf(stderr,
                            "IRIX SGI Motif bootstrap: "
                            "_XmInitializeExtensions failed rc=%d trapped=%d\n",
                            xrc, rt->trapped);
                    if (!rt->trapped)
                        *cpu = saved_cpu;
                }
            }
        }

        /*
         * Do this before Xt recursively runs class_part_initialize.  SGI's
         * Object root wrapper is what registers the per-class Roxy/private
         * descriptors consumed later by _SG_XtInitialize.
         */
        (void)irix_guest_restore_sg_motif_root_wrappers(rt, init_cls);
        (void)irix_guest_repair_synthetic_motif_bce_class(rt, init_cls);
        (void)irix_guest_repair_motif_bce_inheritance(rt, init_cls);
    }

    if (entry && entry->name &&
        strcmp(entry->name, "XtGetClassExtension") == 0)
        irix_guest_repair_synthetic_motif_bce_lookup(rt, cpu);

    /*
     * Xt object-child compatibility.
     *
     * _XtCreateWidget receives (name, object_class, parent, ...).  Repair the
     * parent CompositeClassExtension before Xt checks whether a RectObj/Gadget
     * child is legal.  The helper is a no-op for non-Motif-manager parents.
     */
    if (entry && entry->name &&
        strcmp(entry->name, "_XtCreateWidget") == 0) {
        IrixGuestAddr create_cls =
            (IrixGuestAddr)(uint32_t)cpu->gpr[5];

        /*
         * _XtCreateWidget may receive an already-initialized class and skip
         * XtInitializeWidgetClass.  Keep the original SGI Object initialize
         * wrapper alive for CallInitialize in that case as well.
         */
        (void)irix_guest_restore_sg_motif_root_wrappers(rt, create_cls);
        irix_guest_repair_motif_accepts_objects(
            rt, (IrixGuestAddr)(uint32_t)cpu->gpr[6]);
    }

    /*
     * Synthetic Motif BCE / direct-main InitializePrehook alias.
     *
     * The synthetic callback at 0x501c00d4 maps to the original SGI Motif
     * initializePrehook at 0x0b2e54f0, but Xt can also reach that same
     * synthetic address after mistaking a NULLQUARK Motif BCE for an
     * ObjectClassExtension allocator.
     *
     * These are distinguishable by ABI:
     *
     *   XtAllocateProc:
     *     a0 = WidgetClass
     *     a1 = &csize
     *     ...
     *     a7 = &widget
     *
     *   XtInitProc / initializePrehook:
     *     a0 = request Widget
     *     a1 = new Widget
     *     a2 = ArgList
     *     a3 = Cardinal *num_args
     *
     * Never execute the Motif prehook under the allocator ABI.  Conversely,
     * never bypass a legitimate InitializePrehook: it owns Motif secondary
     * object/cache setup that later InitializePosthook code must finalize.
     */
    if (address == 0x501c00d4u) {
        IrixGuestAddr a0 =
            (IrixGuestAddr)(uint32_t)cpu->gpr[4];
        IrixGuestAddr a1 =
            (IrixGuestAddr)(uint32_t)cpu->gpr[5];

        if (irix_guest_looks_like_widget_class(rt, a0)) {
            IrixGuestAddr alloc_class = a0;
            IrixGuestAddr csize_ptr = a1;
            IrixGuestAddr out_slot =
                (IrixGuestAddr)(uint32_t)cpu->gpr[11];
            IrixGuestAddr caller_sp =
                (IrixGuestAddr)(uint32_t)cpu->gpr[29];
            IrixGuestAddr repair_class = alloc_class;
            int repaired;

            (void)irix_guest_repair_synthetic_motif_bce_class(
                rt, alloc_class);

            if (!irix_guest_looks_like_widget_class(rt, repair_class) &&
                irix_guest_looks_like_widget_class(
                    rt, g_xt_alloc_widget_class_hint))
                repair_class = g_xt_alloc_widget_class_hint;

            repaired = irix_guest_repair_xt_widget_alloc(
                rt, cpu, caller_sp, repair_class, csize_ptr, out_slot);

            (void)0;

            cpu->gpr[0] = 0u;
            return rt->trapped ? -1 : 0;
        } else {
            uint64_t saved_gp = cpu->gpr[28];
            IrixGuestAddr target = 0x0b2e54f0u;
            IrixGuestAddr target_gp =
                irix_guest_gp_for_address(target);
            int rc;

            fprintf(stderr,
                    "IRIX Motif InitializePrehook alias: "
                    "501c00d4 -> 0b2e54f0 "
                    "req=%08x new=%08x args=%08x num_args=%08x "
                    "sp=%08x\n",
                    a0, a1,
                    (uint32_t)cpu->gpr[6],
                    (uint32_t)cpu->gpr[7],
                    (uint32_t)cpu->gpr[29]);

            if (target_gp)
                cpu->gpr[28] =
                    (uint64_t)(int64_t)(int32_t)target_gp;
            cpu->gpr[25] =
                (uint64_t)(int64_t)(int32_t)target;

            rc = irix_mips_interp_call(rt, cpu, target);

            if (!rt->trapped)
                cpu->gpr[28] = saved_gp;
            return rc;
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
        (void)0;
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
            (void)0;
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
            (void)0;
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
            (void)0;
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
            (void)0;
            irix_guest_write32be(rt, ext + 0x10u, 0u);
        }
    }

    /*
     * _XmGetNavigability calls the BaseClass extension's widgetNavigable
     * method.  A live _XtInherit here is not a callable implementation; it is
     * an unresolved class-part inheritance marker.  Resolve it from the
     * superclass chain, patch the subclass BCE slot, and dispatch the real
     * method with the original arguments.
     */
    if (address == 0x0f6588b8u &&
        (uint32_t)cpu->gpr[31] == 0x0b294facu) {
        IrixGuestAddr widget =
            (IrixGuestAddr)(uint32_t)cpu->gpr[4];
        IrixGuestAddr target =
            irix_guest_resolve_xm_widget_navigable(rt, widget);

        if (target && target != address) {
            fprintf(stderr,
                    "IRIX Xt inheritance dispatch: "
                    "_XmGetNavigability widget=%08x -> %08x\n",
                    widget, target);
            cpu->gpr[25] =
                (uint64_t)(int64_t)(int32_t)target;
            return irix_guest_call(rt, cpu, target);
        }
    }

    /*
     * Resolve the two older BaseClass inheritance sentinels structurally.
     *
     * 0x0b296a04 is ClassPartInitRootWrapper calling
     * XmBaseClassExt.classPartInitPrehook(wc), whose slot is +0x54.
     *
     * 0x0b2998cc is the traversal/focus path calling
     * XmBaseClassExt.focusChange(widget, change), whose slot is +0x70.
     *
     * BaseClassPartInitialize is required to fold both from the superclass.
     * If the inherited value is NULL, the Motif source simply skips the call;
     * otherwise dispatch the real inherited method with the original ABI.
     */
    if (address == 0x0f6588b8u &&
        (uint32_t)cpu->gpr[31] == 0x0b296a04u) {
        IrixGuestAddr cls =
            (IrixGuestAddr)(uint32_t)cpu->gpr[4];
        IrixGuestAddr target =
            irix_guest_resolve_motif_bce_method(rt, cls, 0x54u);

        if (target) {
            fprintf(stderr,
                    "IRIX Xt inheritance dispatch: "
                    "classPartInitPrehook class=%08x -> %08x\n",
                    cls, target);
            cpu->gpr[25] =
                (uint64_t)(int64_t)(int32_t)target;
            return irix_guest_call(rt, cpu, target);
        }

        fprintf(stderr,
                "IRIX Xt inheritance resolved NULL: "
                "classPartInitPrehook class=%08x\n", cls);
        cpu->gpr[2] = 0u;
        cpu->gpr[0] = 0u;
        return 0;
    }

    if (address == 0x0f6588b8u &&
        (uint32_t)cpu->gpr[31] == 0x0b2998ccu) {
        IrixGuestAddr widget =
            (IrixGuestAddr)(uint32_t)cpu->gpr[4];
        IrixGuestAddr cls =
            irix_guest_looks_like_widget(rt, widget)
                ? irix_guest_read32be(rt, widget + 0x04u) : 0u;
        IrixGuestAddr target =
            irix_guest_resolve_motif_bce_method(rt, cls, 0x70u);

        if (target) {
            fprintf(stderr,
                    "IRIX Xt inheritance dispatch: "
                    "focusChange widget=%08x class=%08x -> %08x\n",
                    widget, cls, target);
            cpu->gpr[25] =
                (uint64_t)(int64_t)(int32_t)target;
            return irix_guest_call(rt, cpu, target);
        }

        fprintf(stderr,
                "IRIX Xt inheritance resolved NULL: "
                "focusChange widget=%08x class=%08x\n",
                widget, cls);
        cpu->gpr[2] = 0u;
        cpu->gpr[0] = 0u;
        return 0;
    }

    /*
     * Log every other unresolved inheritance sentinel before the broad
     * Xt/Motif stability-mode interpreter fallback consumes it.
     */
    if (address == 0x0f6588b8u) {
        fprintf(stderr,
                "IRIX _XtInherit unresolved: caller_pc=%08x ra=%08x "
                "sp=%08x gp=%08x t9=%08x "
                "a0-a7=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x\n",
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

    /*
     * Xt per-display continuity repair.
     *
     * The real SGI Xt per-display record is created successfully by
     * _XtDisplayInitialize for the Xlib shadow returned by XOpenDisplay.
     * Later malformed object/resource paths can call _XtGetPerDisplay with a
     * NULL or non-Display value, which makes Xt report "Couldn't find per
     * display information" even though the registered record is still alive.
     *
     * Remember only bridge-owned Display shadows (0x72400000, 0x1000 stride,
     * up to 16 displays).  If a later lookup loses the Display argument,
     * restore the last real shadow rather than manufacturing a new Xt record.
     */
    if (entry && entry->name && strcmp(entry->name, "_XtGetPerDisplay") == 0) {
        IrixGuestAddr d = (IrixGuestAddr)(uint32_t)cpu->gpr[4];
        int valid_display =
            d >= 0x72400000u && d < 0x72410000u &&
            ((d - 0x72400000u) & 0xfffu) == 0u &&
            irix_guest_ptr(rt, d, 176u);

        if (valid_display) {
            last_xt_display = d;
        } else if (last_xt_display &&
                   irix_guest_ptr(rt, last_xt_display, 176u)) {
            (void)0;
            cpu->gpr[4] = (uint64_t)(int64_t)(int32_t)last_xt_display;
        } else {
            fprintf(stderr,
                    "IRIX Xt per-display lookup without known Display: "
                    "a0=%08x\\n", d);
        }
    }

    /*
     * 4Dwm bring-up: _XtConvert must receive a real Widget so Xt can recover
     * the correct application context and its converter table.  The failing
     * resource path reaches _XtConvert with a0 == NULL, after which
     * XtWidgetToApplicationContext walks address zero and reports missing
     * String->FontList / String->Pixel converters even though Motif's
     * converter registrar has run.
     *
     * Remember the last valid conversion widget and, for only the malformed
     * NULL case, recover a live widget from the surrounding saved-register
     * frame or the in-progress Xt allocation hints.
     */
    if (entry && entry->name && strcmp(entry->name, "_XtConvert") == 0) {
        IrixGuestAddr w = (IrixGuestAddr)(uint32_t)cpu->gpr[4];

        /*
         * Do not substitute XmDisplay here.  XtWidgetToApplicationContext
         * ultimately asks _XtGetPerDisplay for the widget's Display; the
         * synthetic XmDisplay bootstrap object is not guaranteed to carry
         * the registered Xt Display record.  Prefer an ordinary application
         * widget observed on a successful conversion path.
         */
        if (irix_guest_looks_like_widget(rt, w)) {
            last_xt_convert_widget = w;
        } else if (!w) {
            static const unsigned candidates[] = {16u,17u,18u,19u,20u,21u,22u,23u};
            IrixGuestAddr repair = 0u;
            size_t wi;

            if (irix_guest_looks_like_widget(rt, last_xt_convert_widget))
                repair = last_xt_convert_widget;
            else if (irix_guest_looks_like_widget(rt, g_xt_alloc_widget_hint))
                repair = g_xt_alloc_widget_hint;
            else {
                for (wi = 0; wi < sizeof(candidates)/sizeof(candidates[0]); ++wi) {
                    IrixGuestAddr cw = (IrixGuestAddr)(uint32_t)cpu->gpr[candidates[wi]];
                    if (irix_guest_looks_like_widget(rt, cw)) {
                        repair = cw;
                        break;
                    }
                }
            }

            if (repair) {
                (void)0;
                cpu->gpr[4] = (uint64_t)(int64_t)(int32_t)repair;
                last_xt_convert_widget = repair;
            }
        }
    }

    /*
     * 4Dwm bring-up: ensure Motif's String->FontList (and related) converters
     * are registered before Xt performs resource conversion.  The SGI
     * app-defaults database is loading correctly, but the hybrid startup path
     * can reach _XtConvert without having run _XmRegisterConverters, leaving
     * fontList resources empty and causing 4Dwm's fatal font load failure.
     *
     * Run the original IRIX Motif registrar exactly once in the original
     * Xt/Motif state universe.  Preserve the caller CPU state completely;
     * only the registrar's process-global Xt converter-table side effects are
     * wanted here.
     */
    if (!motif_converters_forced && entry && entry->name &&
        strcmp(entry->name, "_XtConvert") == 0 &&
        irix_guest_ptr(rt, 0x0b2b766cu, 4u)) {
        IrixGuestCpu saved_cpu = *cpu;
        IrixGuestAddr registrar = 0x0b2b766cu; /* _XmRegisterConverters */
        IrixGuestAddr registrar_gp = irix_guest_gp_for_address(registrar);
        int rrc;

        (void)0;
        if (registrar_gp)
            cpu->gpr[28] = (uint64_t)(int64_t)(int32_t)registrar_gp;
        cpu->gpr[25] = (uint64_t)(int64_t)(int32_t)registrar;
        rrc = irix_mips_interp_call(rt, cpu, registrar);
        if (!rt->trapped && rrc == 0) {
            motif_converters_forced = 1;
            *cpu = saved_cpu;
            (void)0;
        } else {
            fprintf(stderr,
                    "IRIX 4Dwm bring-up: _XmRegisterConverters failed rc=%d trapped=%d\\n",
                    rrc, rt->trapped);
            if (!rt->trapped)
                *cpu = saved_cpu;
        }
    }

    /*
     * Emergency direct-main coherence mode.
     *
     * The direct-main runner maps both the synthetic translated archive
     * (0x50000000+) and the original IRIX executable/DSO images.  Any named
     * synthetic function that also has an exact-name original mapped function
     * can otherwise cross between two independent copies of library globals.
     *
     * Under IRIX_EXEC_BACKEND=static, prefer the original mapped function for
     * such duplicates.  This is intentionally broad for bring-up: it keeps one
     * coherent Xt/Motif/application state universe while retaining native
     * bridge entries and translated-only helpers.
     */
    if (entry && entry->name &&
        !(entry->flags & IRIX_GUEST_FUNCTION_NATIVE) &&
        address >= 0x50000000u &&
        getenv("IRIX_EXEC_BACKEND") &&
        strcmp(getenv("IRIX_EXEC_BACKEND"), "static") == 0) {
        size_t xi;
        for (xi = 0; xi < rt->function_count; ++xi) {
            IrixGuestFunctionEntry *xe = &rt->functions[xi];
            uint64_t saved_gp;
            IrixGuestAddr target_gp;
            int rc;

            if (xe == entry || !xe->name ||
                strcmp(xe->name, entry->name) != 0)
                continue;
            if (xe->address >= 0x20000000u ||
                (xe->flags & IRIX_GUEST_FUNCTION_NATIVE) ||
                !irix_guest_ptr(rt, xe->address, 4u))
                continue;

            saved_gp = cpu->gpr[28];
            target_gp = irix_guest_gp_for_address(xe->address);
            if (target_gp)
                cpu->gpr[28] = (uint64_t)(int64_t)(int32_t)target_gp;
            cpu->gpr[25] = (uint64_t)(int64_t)(int32_t)xe->address;

            (void)0;

            rc = irix_mips_interp_call(rt, cpu, xe->address);
            if (!rt->trapped)
                cpu->gpr[28] = saved_gp;
            return rc;
        }
    }

    /*
     * GUI stability mode.
     *
     * For bring-up, keep the SGI Xt/Motif ABI internally coherent by running
     * every named non-native Xt/Xm function as original MIPS.  Anonymous
     * recovered entries already take the interpreter path.  Host-specific
     * native replacements (X11 bridges and the narrow compatibility shims
     * above) remain native because IRIX_GUEST_FUNCTION_NATIVE excludes them.
     *
     * This deliberately trades static-recomp speed for session stability.
     * Once the desktop is reliably usable, individual libraries/functions can
     * be moved back to generated C with regression tests around each boundary.
     */
    if (entry && entry->name &&
        !(entry->flags & IRIX_GUEST_FUNCTION_NATIVE) &&
        (!strncmp(entry->name, "Xt", 2) ||
         !strncmp(entry->name, "_Xt", 3) ||
         !strncmp(entry->name, "Xm", 2) ||
         !strncmp(entry->name, "_Xm", 3))) {
        uint64_t saved_gp = cpu->gpr[28];
        IrixGuestAddr exec_address = address;
        IrixGuestAddr target_gp;
        IrixGuestAddr xt_init_class =
            (strcmp(entry->name, "XtInitializeWidgetClass") == 0)
                ? (IrixGuestAddr)(uint32_t)cpu->gpr[4] : 0u;
        int rc;

        /*
         * Direct-main links the translated core into the synthetic 0x50000000
         * image, but also maps the original IRIX Xt/Motif DSOs at their real
         * addresses.  Executing a synthetic Xt copy and an original Xt copy in
         * one process splits process-global state (notably the per-display
         * list), producing "Couldn't find per display information".
         *
         * GUI stability mode is already intentionally running Xt/Xm as
         * original MIPS.  If the selected registry entry is the synthetic
         * archive copy, redirect by exact symbol name to the original mapped
         * DSO copy so every Xt/Xm call shares one set of globals.
         */
        if (address >= 0x50000000u) {
            size_t xi;
            for (xi = 0; xi < rt->function_count; ++xi) {
                IrixGuestFunctionEntry *xe = &rt->functions[xi];
                if (xe == entry || !xe->name ||
                    strcmp(xe->name, entry->name) != 0)
                    continue;
                if (xe->address < 0x20000000u &&
                    !(xe->flags & IRIX_GUEST_FUNCTION_NATIVE) &&
                    irix_guest_ptr(rt, xe->address, 4u)) {
                    exec_address = xe->address;
                    
                    break;
                }
            }
        }

        target_gp = irix_guest_gp_for_address(exec_address);
        if (target_gp)
            cpu->gpr[28] = (uint64_t)(int64_t)(int32_t)target_gp;
        cpu->gpr[25] = (uint64_t)(int64_t)(int32_t)exec_address;

        

        rc = irix_mips_interp_call(rt, cpu, exec_address);

        if (!rt->trapped && xt_init_class)
            (void)irix_guest_repair_motif_bce_inheritance(
                rt, xt_init_class);

        if (!rt->trapped)
            cpu->gpr[28] = saved_gp;
        return rc;
    }

    /*
     * Deadline bring-up: keep 4Dwm ProcessScreenResources in original MIPS.
     * This avoids regenerating/recompiling the translated 4Dwm bundle for
     * each narrow compatibility repair; subsequent fixes can live entirely
     * in the interpreter/runtime and rebuild quickly.
     */
    if (entry && entry->name &&
        strcmp(entry->name, "ProcessScreenResources") == 0) {
        uint64_t saved_gp = cpu->gpr[28];
        IrixGuestAddr target_gp = irix_guest_gp_for_address(address);
        int rc;
        if (target_gp)
            cpu->gpr[28] = (uint64_t)(int64_t)(int32_t)target_gp;
        cpu->gpr[25] = (uint64_t)(int64_t)(int32_t)address;
        (void)0;
        rc = irix_mips_interp_invoke(rt, cpu, address);
        if (!rt->trapped)
            cpu->gpr[28] = saved_gp;
        return rc;
    }

    /*
     * Deadline bring-up: keep 4Dwm dynamic-default lookup in original MIPS
     * so narrow compatibility fixes can be made in the interpreter/runtime
     * without regenerating the translated 4Dwm bundle.
     */
    if (entry && entry->name &&
        strcmp(entry->name, "_WmGetDynamicDefault") == 0) {
        uint64_t saved_gp = cpu->gpr[28];
        IrixGuestAddr target_gp = irix_guest_gp_for_address(address);
        int rc;
        if (target_gp)
            cpu->gpr[28] = (uint64_t)(int64_t)(int32_t)target_gp;
        cpu->gpr[25] = (uint64_t)(int64_t)(int32_t)address;
        (void)0;
        rc = irix_mips_interp_invoke(rt, cpu, address);
        if (!rt->trapped)
            cpu->gpr[28] = saved_gp;
        return rc;
    }

    /*
     * Deadline bring-up: keep 4Dwm background-pixmap default lookup in
     * original MIPS so NULL resource compatibility stays runtime-only.
     */
    if (entry && entry->name &&
        strcmp(entry->name, "_WmBackgroundPixmapDefault") == 0) {
        uint64_t saved_gp = cpu->gpr[28];
        IrixGuestAddr target_gp = irix_guest_gp_for_address(address);
        int rc;
        if (target_gp)
            cpu->gpr[28] = (uint64_t)(int64_t)(int32_t)target_gp;
        cpu->gpr[25] = (uint64_t)(int64_t)(int32_t)address;
        (void)0;
        rc = irix_mips_interp_invoke(rt, cpu, address);
        if (!rt->trapped)
            cpu->gpr[28] = saved_gp;
        return rc;
    }

    /*
     * Deadline bring-up: 4Dwm's resource default callbacks share the same
     * optional screen-resource structures.  Keep the private _Wm*Default
     * callbacks in original MIPS so NULL-resource compatibility stays in the
     * interpreter and does not regenerate the translated 4Dwm bundle.
     */
    if (entry && entry->name &&
        strncmp(entry->name, "_Wm", 3) == 0) {
        size_t n = strlen(entry->name);
        if (n >= 7 && strcmp(entry->name + n - 7, "Default") == 0) {
            uint64_t saved_gp = cpu->gpr[28];
            IrixGuestAddr target_gp = irix_guest_gp_for_address(address);
            int rc;
            if (target_gp)
                cpu->gpr[28] = (uint64_t)(int64_t)(int32_t)target_gp;
            cpu->gpr[25] = (uint64_t)(int64_t)(int32_t)address;
            (void)0;
            rc = irix_mips_interp_invoke(rt, cpu, address);
            if (!rt->trapped)
                cpu->gpr[28] = saved_gp;
            return rc;
        }
    }

    /*
     * Import veneers are registered as native forwarders.  Some link
     * veneers live inside broad interpreter-forcing ranges (notably the
     * libXm range), but interpreting their placeholder MIPS body bypasses the
     * binding and returns/clobbers state instead of reaching the real
     * implementation.  Keep explicit native registrations authoritative.
     */
    if ((!entry || !(entry->flags & IRIX_GUEST_FUNCTION_NATIVE)) &&
        irix_mips_interp_should_force(address)) {
        
        return irix_mips_interp_invoke(rt, cpu, address);
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
        rc = irix_mips_interp_invoke(rt, cpu, address);
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
        rc = irix_mips_interp_invoke(rt, cpu, address);
        if (!rt->trapped)
            cpu->gpr[28] = saved_gp;
        return rc;
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

            interp_rc = irix_mips_interp_invoke(rt, cpu, address);
            if (interp_rc == 0) {
                if (!rt->trapped)
                    cpu->gpr[28] = saved_gp;
                return 0;
            }
            if (rt->trapped)
                return -1;

            fprintf(stderr,
                    "IRIX guest interpreter dispatch failed: target=%08x rc=%d "
                    "cpu_pc=%08x current_pc=%08x ra=%08x gp=%08x\n",
                    address, interp_rc, (uint32_t)cpu->pc,
                    (uint32_t)rt->current_pc, (uint32_t)cpu->gpr[31],
                    (uint32_t)cpu->gpr[28]);

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
        IrixGuestAddr caller_ra = (IrixGuestAddr)(uint32_t)cpu->gpr[31];
        IrixMipsTranslatedFrame translated_frame;
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
            (void)0;
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
                (void)0;
            }
            uint32_t arg_size = 0, arg_addr = 0, arg_value = 0;
            if (args && irix_guest_ptr(rt, args, 8u)) {
                arg_size = irix_guest_read32be(rt, args + 0u);
                arg_addr = irix_guest_read32be(rt, args + 4u);
                if (arg_addr && irix_guest_ptr(rt, arg_addr, 4u))
                    arg_value = irix_guest_read32be(rt, arg_addr);
            }
            (void)0;
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
            (void)0;
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
            (void)0;
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
                    
                }
            }
        }

        if (xm_display_shell_call) {
            if (xm_display_shell_depth++ == 0u)
                xm_display_bootstrap_widget = 0u;
            g_xt_alloc_widget_class_hint =
                (IrixGuestAddr)(uint32_t)cpu->gpr[6];
            
        }

        if (entry->flags & IRIX_GUEST_FUNCTION_RECOMPILED)
            irix_mips_interp_push_translated(&translated_frame, address,
                                              entry->size, caller_ra);
        entry->function(rt, cpu);
        if (entry->flags & IRIX_GUEST_FUNCTION_RECOMPILED)
            irix_mips_interp_pop_translated(&translated_frame);

        if (sg_drop_swap && sg_class &&
            irix_guest_ptr(rt, sg_class + 0x60u, 4u)) {
            irix_guest_write32be(rt, sg_class + 0x60u, sg_saved_private);
            
        }

        /*
         * Repair the public XmGetXmScreen return as before. Interior entries
         * in the same machine function are handled in irix_guest_call().
         */
        if (!rt->trapped && address == 0x0b2a80acu)
            irix_guest_repair_xm_screen_private(
                rt, (IrixGuestAddr)(uint32_t)cpu->gpr[2]);

        if (xm_display_shell_call) {
            if (xm_display_shell_depth)
                --xm_display_shell_depth;
            
            if (!xm_display_shell_depth) {
                xm_display_bootstrap_widget = 0u;
                g_xt_alloc_widget_class_hint = 0u;
                g_xt_alloc_widget_hint = 0u;
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
                (void)0;
                cpu->pc = 0x0b2942b0u;
                rt->current_pc = 0x0b2942b0u;
                cont->function(rt, cpu);
            } else {
                (void)0;
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
    if (!entry || entry->address != address ||
        ((entry->flags & IRIX_GUEST_FUNCTION_INTERIOR) &&
         !(entry->flags & IRIX_GUEST_FUNCTION_NATIVE)))
        return irix_guest_call(rt, cpu, address);
    caller = rt->current_pc;
    irix_guest_profile_hit(rt, caller, address, entry);
    if (irix_mips_jit_requested() &&
        !(entry->flags & IRIX_GUEST_FUNCTION_NATIVE))
        return irix_mips_jit_call(rt, cpu, address);
    {
        int rc = irix_guest_call_entry_impl(rt, cpu, address, entry);
        return rc;
    }
}

int irix_guest_call(IrixGuestRuntime *rt, IrixGuestCpu *cpu, IrixGuestAddr address)
{
    IrixGuestAddr caller = rt->current_pc;
    IrixGuestFunctionEntry *entry = irix_guest_find_function(rt, address);

    if (getenv("IRIX_TRACE_FPRINTF")) {
        IrixGuestAddr fmt = (IrixGuestAddr)(uint32_t)cpu->gpr[5];
        IrixGuestAddr fmt2 = (IrixGuestAddr)(uint32_t)cpu->gpr[4];
        const char *f = irix_guest_ptr(rt, fmt, 8u) ? irix_guest_cstr(rt, fmt, 128u) : 0;
        const char *f2 = irix_guest_ptr(rt, fmt2, 8u) ? irix_guest_cstr(rt, fmt2, 128u) : 0;
        if ((f && strstr(f, "[ %s%s =")) || (f2 && strstr(f2, "[ %s%s ="))) {
            uint32_t sp = (uint32_t)cpu->gpr[29];
            {
                unsigned r;
                for (r = 6; r <= 11; ++r) {
                    IrixGuestAddr pa = (IrixGuestAddr)(uint32_t)cpu->gpr[r];
                    const char *ps = irix_guest_ptr(rt, pa, 4u) ? irix_guest_cstr(rt, pa, 40u) : 0;
                    fprintf(stderr, "  arg r%u=%08x [%s]\n", r, (unsigned)pa, ps ? ps : "");
                }
            }
            fprintf(stderr, "FPRINTF target=%08x caller=%08x a0=%llx a2=%llx a3=%llx a4=%llx a5=%llx a6=%llx a7=%llx sp=%08x slot0=%016llx slot1=%016llx\n",
                    (unsigned)address, (unsigned)caller,
                    (unsigned long long)cpu->gpr[4], (unsigned long long)cpu->gpr[6],
                    (unsigned long long)cpu->gpr[7], (unsigned long long)cpu->gpr[8],
                    (unsigned long long)cpu->gpr[9], (unsigned long long)cpu->gpr[10],
                    (unsigned long long)cpu->gpr[11], sp,
                    (unsigned long long)irix_guest_read64be(rt, sp),
                    (unsigned long long)irix_guest_read64be(rt, sp + 8u));
        }
    }
    
    
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
     *
     * Anonymous exact entries tagged IRIX_GUEST_FUNCTION_INTERIOR take the
     * same path. They are recovered continuation labels whose exact registry
     * entries would otherwise hide the fact that they lie inside a larger
     * translated machine-function range.
     */
    if (entry &&
        (entry->address != address ||
         (entry->flags & IRIX_GUEST_FUNCTION_INTERIOR)) &&
        !(entry->flags & IRIX_GUEST_FUNCTION_NATIVE)) {
        uint64_t saved_s[9];
        uint64_t saved_gp = cpu->gpr[28];
        uint64_t saved_sp = cpu->gpr[29];
        uint64_t saved_ra = cpu->gpr[31];
        IrixGuestAddr saved_a0 = (IrixGuestAddr)(uint32_t)cpu->gpr[4];
        IrixGuestAddr saved_a1 = (IrixGuestAddr)(uint32_t)cpu->gpr[5];
        IrixGuestAddr saved_a7 = (IrixGuestAddr)(uint32_t)cpu->gpr[11];
        unsigned saved_i;
        IrixGuestAddr target_gp = irix_guest_gp_for_address(address);
        int rc;

        /*
         * Motif menu hover diagnostic.
         *
         * 0x0b28ee30 is the anonymous gadget input-dispatch target reached
         * from _XmDispatchGadgetInput immediately before the hover-triggered
         * runaway.  Snapshot the actual child gadget here so we can compare a
         * successful item with the item whose private/cache state is NULL.
         * This is intentionally diagnostic only; no guest state is changed.
         */
        if (address == 0x0b28ee30u &&
            getenv("IRIX_MOTIF_GADGET_TRACE") &&
            getenv("IRIX_MOTIF_GADGET_TRACE")[0] == '1') {
            IrixGuestAddr w =
                (IrixGuestAddr)(uint32_t)cpu->gpr[4];
            IrixGuestAddr cls = 0u;
            IrixGuestAddr parent = 0u;
            IrixGuestAddr name_addr = 0u;
            const char *name = NULL;
            unsigned off;

            if (w && irix_guest_ptr(rt, w, 12u)) {
                cls = irix_guest_read32be(rt, w + 4u);
                parent = irix_guest_read32be(rt, w + 8u);
            }
            if (cls && irix_guest_ptr(rt, cls + 4u, 4u)) {
                name_addr = irix_guest_read32be(rt, cls + 4u);
                name = irix_guest_cstr(rt, name_addr, 256u);
            }

            (void)0;

            if (w && irix_guest_ptr(rt, w, 0x100u)) {
                for (off = 0u; off < 0x100u; off += 0x20u) {
                    fprintf(stderr,
                            "IRIX Motif gadget words +%02x: "
                            "%08x %08x %08x %08x "
                            "%08x %08x %08x %08x\n",
                            off,
                            irix_guest_read32be(rt, w + off + 0x00u),
                            irix_guest_read32be(rt, w + off + 0x04u),
                            irix_guest_read32be(rt, w + off + 0x08u),
                            irix_guest_read32be(rt, w + off + 0x0cu),
                            irix_guest_read32be(rt, w + off + 0x10u),
                            irix_guest_read32be(rt, w + off + 0x14u),
                            irix_guest_read32be(rt, w + off + 0x18u),
                            irix_guest_read32be(rt, w + off + 0x1cu));
                }
            }

            /*
             * Enter() stores this gadget into
             * XtParent(w)->manager.active_child.  On the subsequent Leave()
             * the failing SGI store is the corresponding NULL clear.  Find
             * the exact SGI instance offset from live state rather than
             * assuming an OpenMotif struct layout.
             */
            if (parent && w && irix_guest_ptr(rt, parent, 12u)) {
                IrixGuestAddr pcls = irix_guest_read32be(rt, parent + 4u);
                uint32_t psz = 0u;
                unsigned hit_count = 0u;

                if (pcls && irix_guest_ptr(rt, pcls + 8u, 4u))
                    psz = irix_guest_read32be(rt, pcls + 8u);
                if (!psz || psz > 0x1000u)
                    psz = 0x400u;

                (void)0;

                if (irix_guest_ptr(rt, parent, psz)) {
                    for (off = 0u; off + 4u <= psz; off += 4u) {
                        if (irix_guest_read32be(rt, parent + off) == w) {
                            fprintf(stderr,
                                    "IRIX Motif gadget parent child-hit: "
                                    "parent=%08x +%03x = %08x\n",
                                    parent, off, w);
                            ++hit_count;
                        }
                    }
                }
                (void)0;
            }
        }

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

        /*
         * Emergency imdFtrChooseVisual null-candidate guard.
         *
         * The hidden helper at 0x0980c8b4 returns a boolean byte to
         * imdFtrChooseVisual.  In 4Dwm bring-up it is called with a1 == NULL
         * and immediately dereferences a1+0x28.  A NULL candidate cannot
         * match a visual, so return false instead of faulting.
         */
        if (address == 0x0980c8b4u &&
            (uint32_t)cpu->gpr[5] == 0u) {
            fprintf(stderr,
                    "IRIX imdFtrChooseVisual compatibility: "
                    "null candidate -> false; "
                    "a0-a3=%08x,%08x,%08x,%08x "
                    "s0-s7=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x "
                    "sp=%08x ra=%08x\n",
                    (uint32_t)cpu->gpr[4], (uint32_t)cpu->gpr[5],
                    (uint32_t)cpu->gpr[6], (uint32_t)cpu->gpr[7],
                    (uint32_t)cpu->gpr[16], (uint32_t)cpu->gpr[17],
                    (uint32_t)cpu->gpr[18], (uint32_t)cpu->gpr[19],
                    (uint32_t)cpu->gpr[20], (uint32_t)cpu->gpr[21],
                    (uint32_t)cpu->gpr[22], (uint32_t)cpu->gpr[23],
                    (uint32_t)cpu->gpr[29], (uint32_t)cpu->gpr[31]);
            cpu->gpr[2] = 0u;
            cpu->gpr[0] = 0u;
            return 0;
        }

        /*
         * Hidden Motif XtAllocateProc continuation at 0x0b2fcc64.
         *
         * This exact entry begins by dereferencing a1 as a Widget
         * (widget->core.widget_class, then class+0x64).  In the malformed
         * hybrid call returning to Xt at 0x0f62b578, a1 is an Xt stack
         * scratch pointer while caller-saved s1 still names the live widget.
         * Anonymous/interior entries bypass call_entry_impl entirely, so this
         * repair belongs at the common guest-call boundary before entering
         * original MIPS.
         */
        if (address == 0x0b2fcc64u) {
            IrixGuestAddr new_w =
                (IrixGuestAddr)(uint32_t)cpu->gpr[5];
            IrixGuestAddr saved_new_w =
                (IrixGuestAddr)(uint32_t)cpu->gpr[17];

            if (!irix_guest_looks_like_widget(rt, new_w) &&
                irix_guest_looks_like_widget(rt, saved_new_w)) {
                (void)0;
                cpu->gpr[5] =
                    (uint64_t)(int64_t)(int32_t)saved_new_w;
            }
        }

        
        if (target_gp)
            cpu->gpr[28] = (uint64_t)(int64_t)(int32_t)target_gp;
        rc = irix_mips_interp_invoke(rt, cpu, address);
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
                    (void)0;
                    alloc_class = g_xt_alloc_widget_class_hint;
                }

                (void)irix_guest_repair_xt_widget_alloc(
                    rt, cpu, (IrixGuestAddr)(uint32_t)saved_sp,
                    alloc_class, saved_a1, saved_a7);
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
    {
        uint64_t caller_saved[9];
        uint64_t caller_gp = cpu->gpr[28];
        uint64_t caller_sp = cpu->gpr[29];
        uint64_t caller_ra = cpu->gpr[31];
        unsigned saved_i;
        IrixGuestAddr caller_domain_gp = irix_guest_gp_for_address(caller);
        IrixGuestAddr target_domain_gp = irix_guest_gp_for_address(address);
        int restore_call_ra = caller_domain_gp && target_domain_gp &&
                              caller_domain_gp == target_domain_gp;
        int rc;

        /* Calls into original MIPS still cross this C dispatcher boundary.
         * Enforce the N32 nonvolatile-register contract here as well as for
         * recovered interior entries above: a nested interpreter dispatch
         * can otherwise leak the callee's s0-s7/fp/gp/sp back into its
         * caller.  iconv_comp's stdio scanner exposed this when its saved
         * input-buffer pointer in s6 was replaced by the callee's EOF value.
         */
        for (saved_i = 0; saved_i < 8u; ++saved_i)
            caller_saved[saved_i] = cpu->gpr[16u + saved_i];
        caller_saved[8] = cpu->gpr[30];
        rc = irix_guest_call_entry_impl(rt, cpu, address, entry);
        if (!rt->trapped) {
            for (saved_i = 0; saved_i < 8u; ++saved_i)
                cpu->gpr[16u + saved_i] = caller_saved[saved_i];
            cpu->gpr[28] = caller_gp;
            cpu->gpr[29] = caller_sp;
            cpu->gpr[30] = caller_saved[8];
            /* Keep the caller's continuation across same-image interpreted
             * calls. Cross-GP transfers may be SGI rld lazy-PLT tail calls,
             * whose return address is deliberately propagated from the
             * original .MIPS.stubs entry. */
            if (restore_call_ra)
                cpu->gpr[31] = caller_ra;
        }
        return rc;
    }
}
