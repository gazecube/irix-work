#include "IrixGuest.h"
#include "IrixCompat.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>

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
}

void irix_guest_runtime_destroy(IrixGuestRuntime *rt)
{
    size_t i;
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
    seg->host_base = (uint8_t *)calloc(1, size);
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

int irix_guest_register_function_range(IrixGuestRuntime *rt, IrixGuestAddr address,
                                       uint32_t size, IrixGuestFunction function,
                                       const char *name)
{
    IrixGuestFunctionEntry *entry;
    if (!function || rt->function_count >= IRIX_GUEST_MAX_FUNCTIONS)
        return -1;
    entry = &rt->functions[rt->function_count++];
    entry->address = address;
    entry->size = size;
    entry->function = function;
    entry->name = name;
    return 0;
}

int irix_guest_register_function(IrixGuestRuntime *rt, IrixGuestAddr address,
                                 IrixGuestFunction function, const char *name)
{
    return irix_guest_register_function_range(rt, address, 0u, function, name);
}

IrixGuestFunctionEntry *irix_guest_find_function(IrixGuestRuntime *rt, IrixGuestAddr address)
{
    IrixGuestFunctionEntry *containing = 0;
    size_t i;
    for (i = 0; i < rt->function_count; ++i) {
        IrixGuestFunctionEntry *entry = &rt->functions[i];
        if (entry->address == address)
            return entry;
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

static int write_irix_xstat32(IrixGuestRuntime *rt, IrixGuestAddr out,
                              const struct stat *st)
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
    irix_guest_write32be(rt, out + 36u, irix_syscall_dev(st->st_rdev));
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
                              const struct stat *st)
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
    irix_guest_write32be(rt, out + 40u, irix_syscall_dev(st->st_rdev));
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
                            IrixGuestAddr out, const struct stat *st)
{
    if (version == 2u)
        return write_irix_xstat32(rt, out, st);
    if (version == 3u)
        return write_irix_xstat64(rt, out, st);
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
    case 1000: { /* syscall(indirect_number, ...) */
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
    case 1001: { /* exit */
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
    case 1003: { /* read */
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
    case 1004: { /* write */
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
    case 1005: { /* open */
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
    case 1006: /* close */
        result = (int64_t)close((int)(int32_t)cpu->gpr[4]);
        syscall_result(cpu, result);
        return;
    case 1017: { /* break */
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
    case 1020: /* getpid */
        syscall_result(cpu, (int64_t)getpid());
        return;
    case 1024: /* getuid */
        syscall_result(cpu, (int64_t)getuid());
        return;
    case 1040: /* syssgi */
        irix_compat_syssgi(rt, cpu);
        return;
    case 1047: /* getgid */
        syscall_result(cpu, (int64_t)getgid());
        return;
    case 1056: /* sysmp(cmd, ...) */
        irix_compat_sysmp(rt, cpu);
        return;
    case 1130: /* prctl */
        irix_compat_prctl(rt, cpu);
        return;
    case 1134: /* mmap */
        irix_compat_mmap(rt, cpu);
        return;
    case 1135: /* munmap */
        irix_compat_munmap(rt, cpu);
        return;
    case 1136: /* mprotect */
        irix_compat_mprotect(rt, cpu);
        return;
    case 1140: /* getpagesize */
        syscall_result(cpu, 16384);
        return;
    case 1156: /* sysinfo(command, buf, count) */
        irix_compat_systeminfo(rt, cpu);
        return;
    case 1158: { /* xstat(version, path, statbuf) */
        uint32_t version = (uint32_t)cpu->gpr[4];
        IrixGuestAddr path_addr = (IrixGuestAddr)(uint32_t)cpu->gpr[5];
        IrixGuestAddr out = (IrixGuestAddr)(uint32_t)cpu->gpr[6];
        const char *path = (const char *)irix_guest_ptr(rt, path_addr, 1u);
        struct stat st;
        if (!path) {
            errno = EFAULT;
            syscall_result(cpu, -1);
            return;
        }
        result = (int64_t)stat(path, &st);
        if (result == 0 && write_irix_xstat(rt, version, out, &st) != 0)
            result = -1;
        syscall_result(cpu, result);
        return;
    }
    case 1159: { /* lxstat(version, path, statbuf) */
        uint32_t version = (uint32_t)cpu->gpr[4];
        IrixGuestAddr path_addr = (IrixGuestAddr)(uint32_t)cpu->gpr[5];
        IrixGuestAddr out = (IrixGuestAddr)(uint32_t)cpu->gpr[6];
        const char *path = (const char *)irix_guest_ptr(rt, path_addr, 1u);
        struct stat st;
        if (!path) {
            errno = EFAULT;
            syscall_result(cpu, -1);
            return;
        }
        result = (int64_t)lstat(path, &st);
        if (result == 0 && write_irix_xstat(rt, version, out, &st) != 0)
            result = -1;
        syscall_result(cpu, result);
        return;
    }
    case 1160: { /* fxstat(version, fd, statbuf) */
        uint32_t version = (uint32_t)cpu->gpr[4];
        int fd = (int)(int32_t)cpu->gpr[5];
        IrixGuestAddr out = (IrixGuestAddr)(uint32_t)cpu->gpr[6];
        struct stat st;
        result = (int64_t)fstat(fd, &st);
        if (result == 0 && write_irix_xstat(rt, version, out, &st) != 0)
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

static int trace_passwd_call(const char *name)
{
    if (!name) return 0;
    return strcmp(name, "getpwuid") == 0 ||
           strcmp(name, "getpwnam") == 0 ||
           strcmp(name, "getpwuid_r") == 0 ||
           strcmp(name, "getpwnam_r") == 0;
}

static int trace_xwsh_passwd_decision(IrixGuestAddr pc)
{
    /* We found the decision function: authenticateUser__6wshAppGv at
     * 0x10039c50.  Trace the calls it makes so we can identify the lookup
     * which returns NULL, rather than tracing the fatal fprintf itself. */
    return pc >= 0x10039c50u && pc < 0x10039d0cu;
}

static void dump_guest_bytes(IrixGuestRuntime *rt, const char *label,
                             IrixGuestAddr addr, uint32_t count)
{
    unsigned i;
    uint32_t limit = count < 64u ? count : 64u;
    fprintf(stderr, "%s addr=%08x len=%u", label, addr, count);
    if (!addr || !limit || !irix_guest_ptr(rt, addr, limit)) {
        fputc('\n', stderr);
        return;
    }
    fputs(" bytes:", stderr);
    for (i = 0; i < limit; ++i)
        fprintf(stderr, " %02x", (unsigned)irix_guest_read8(rt, addr + i));
    fputc('\n', stderr);
}

int irix_guest_call(IrixGuestRuntime *rt, IrixGuestCpu *cpu, IrixGuestAddr address)
{
    IrixGuestFunctionEntry *entry = irix_guest_find_function(rt, address);
    /*
     * Once authenticateUser is running, its callees live outside the xwsh
     * address window.  Use the caller PC to report those outbound calls.
     */
    if ((uint32_t)cpu->pc >= 0x10039c50u &&
        (uint32_t)cpu->pc < 0x10039d0cu &&
        address != 0x10039c50u) {
        fprintf(stderr,
                "IRIX authenticateUser call: target=%08x owner=%08x:%s caller=%08x ra=%08x v0=%08x a0-a7=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x sp=%08x gp=%08x t9=%08x\n",
                address,
                entry ? entry->address : 0u,
                entry && entry->name ? entry->name : "?",
                (uint32_t)cpu->pc, (uint32_t)cpu->gpr[31],
                (uint32_t)cpu->gpr[2],
                (uint32_t)cpu->gpr[4], (uint32_t)cpu->gpr[5],
                (uint32_t)cpu->gpr[6], (uint32_t)cpu->gpr[7],
                (uint32_t)cpu->gpr[8], (uint32_t)cpu->gpr[9],
                (uint32_t)cpu->gpr[10], (uint32_t)cpu->gpr[11],
                (uint32_t)cpu->gpr[29], (uint32_t)cpu->gpr[28],
                (uint32_t)cpu->gpr[25]);
        report_nearest_functions(rt, address);
        fputc('\n', stderr);
    }
    if (((uint32_t)cpu->pc == 0x10039c7cu ||
         (uint32_t)cpu->pc == 0x10039c88u ||
         (uint32_t)cpu->pc == 0x10039cd0u ||
         (uint32_t)cpu->pc == 0x10039d04u) &&
        address != 0x10039c50u) {
        IrixGuestAddr cs = (IrixGuestAddr)(uint32_t)cpu->pc;
        fprintf(stderr, "  authenticateUser callsite %08x:", cs);
        if (irix_guest_ptr(rt, cs - 16u, 40u)) {
            unsigned j;
            for (j = 0; j < 10u; ++j)
                fprintf(stderr, " %08x", irix_guest_read32be(rt, cs - 16u + j * 4u));
        }
        fputc('\n', stderr);
    }
    if (trace_xwsh_passwd_decision(address)) {
        fprintf(stderr,
                "IRIX passwd window call: target=%08x owner=%08x:%s caller=%08x ra=%08x v0=%08x a0-a7=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x sp=%08x gp=%08x t9=%08x\n",
                address,
                entry ? entry->address : 0u,
                entry && entry->name ? entry->name : "?",
                (uint32_t)cpu->pc, (uint32_t)cpu->gpr[31],
                (uint32_t)cpu->gpr[2],
                (uint32_t)cpu->gpr[4], (uint32_t)cpu->gpr[5],
                (uint32_t)cpu->gpr[6], (uint32_t)cpu->gpr[7],
                (uint32_t)cpu->gpr[8], (uint32_t)cpu->gpr[9],
                (uint32_t)cpu->gpr[10], (uint32_t)cpu->gpr[11],
                (uint32_t)cpu->gpr[29], (uint32_t)cpu->gpr[28],
                (uint32_t)cpu->gpr[25]);
        report_nearest_functions(rt, address);
        fputc('\n', stderr);
    }
    if (!entry) {
        IrixGuestAddr s4 = (IrixGuestAddr)(uint32_t)cpu->gpr[20];

        if (try_trivial_stripped_ctor(rt, cpu, address))
            return rt->trapped ? -1 : 0;
        if (try_shared_return_epilogue(rt, cpu, address))
            return rt->trapped ? -1 : 0;

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
    {
        uint64_t caller_saved[9];
        uint64_t caller_gp = cpu->gpr[28];
        uint64_t caller_sp = cpu->gpr[29];
        IrixGuestAddr caller_pc = cpu->pc;
        int passwd_function_trace = trace_passwd_call(entry->name);
        unsigned saved_i;

        /* N32 callee-saved integer state. s0-s7 and s8/fp must survive a
           completed guest call just as $gp already does below. Keeping this
           contract at the dispatcher boundary is especially important when
           a translated callee crosses recovered ranges before returning. */
        for (saved_i = 0; saved_i < 8u; ++saved_i)
            caller_saved[saved_i] = cpu->gpr[16u + saved_i];
        caller_saved[8] = cpu->gpr[30];
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
        if (passwd_function_trace) {
            fprintf(stderr,
                    "IRIX passwd guest entry: callee=%08x:%s caller=%08x ra=%08x a0-a7=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x sp=%08x gp=%08x\n",
                    address, entry->name, caller_pc, (uint32_t)cpu->gpr[31],
                    (uint32_t)cpu->gpr[4], (uint32_t)cpu->gpr[5],
                    (uint32_t)cpu->gpr[6], (uint32_t)cpu->gpr[7],
                    (uint32_t)cpu->gpr[8], (uint32_t)cpu->gpr[9],
                    (uint32_t)cpu->gpr[10], (uint32_t)cpu->gpr[11],
                    (uint32_t)cpu->gpr[29], (uint32_t)cpu->gpr[28]);
            if ((strcmp(entry->name, "getpwnam") == 0 ||
                 strcmp(entry->name, "getpwnam_r") == 0) &&
                cpu->gpr[4])
                dump_guest_bytes(rt, "  passwd name",
                                 (IrixGuestAddr)(uint32_t)cpu->gpr[4], 64u);
        }
        if (entry->name && strcmp(entry->name, "strcpy") == 0) {
            fprintf(stderr,
                    "IRIX strcpy call: caller=%08x ra=%08x a0=%08x a1=%08x v0=%08x sp=%08x gp=%08x t9=%08x",
                    (uint32_t)caller_pc, (uint32_t)cpu->gpr[31],
                    (uint32_t)cpu->gpr[4], (uint32_t)cpu->gpr[5],
                    (uint32_t)cpu->gpr[2], (uint32_t)cpu->gpr[29],
                    (uint32_t)cpu->gpr[28], (uint32_t)cpu->gpr[25]);
            {
                IrixGuestAddr ra = (IrixGuestAddr)(uint32_t)cpu->gpr[31];
                IrixGuestAddr callsite = ra >= 8u ? ra - 8u : ra;
                if (callsite >= 16u && irix_guest_ptr(rt, callsite - 16u, 40u)) {
                    fprintf(stderr,
                            " callsite=%08x call_words=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x",
                            (uint32_t)callsite,
                            irix_guest_read32be(rt, callsite - 16u),
                            irix_guest_read32be(rt, callsite - 12u),
                            irix_guest_read32be(rt, callsite - 8u),
                            irix_guest_read32be(rt, callsite - 4u),
                            irix_guest_read32be(rt, callsite),
                            irix_guest_read32be(rt, callsite + 4u),
                            irix_guest_read32be(rt, callsite + 8u),
                            irix_guest_read32be(rt, callsite + 12u),
                            irix_guest_read32be(rt, callsite + 16u),
                            irix_guest_read32be(rt, callsite + 20u));
                }
            }
            fputc('\n', stderr);
        }
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
        entry->function(rt, cpu);
        if (trace_xwsh_passwd_decision(address)) {
            fprintf(stderr,
                    "IRIX passwd window return: target=%08x owner=%08x:%s trapped=%d v0=%08x v1=%08x a0-a3=%08x,%08x,%08x,%08x sp=%08x gp=%08x\n",
                    address, entry->address, entry->name ? entry->name : "?",
                    rt->trapped, (uint32_t)cpu->gpr[2], (uint32_t)cpu->gpr[3],
                    (uint32_t)cpu->gpr[4], (uint32_t)cpu->gpr[5],
                    (uint32_t)cpu->gpr[6], (uint32_t)cpu->gpr[7],
                    (uint32_t)cpu->gpr[29], (uint32_t)cpu->gpr[28]);
        }
        if (passwd_function_trace) {
            fprintf(stderr,
                    "IRIX passwd guest return: callee=%08x:%s trapped=%d v0=%08x v1=%08x a0-a3=%08x,%08x,%08x,%08x sp=%08x\n",
                    address, entry->name, rt->trapped,
                    (uint32_t)cpu->gpr[2], (uint32_t)cpu->gpr[3],
                    (uint32_t)cpu->gpr[4], (uint32_t)cpu->gpr[5],
                    (uint32_t)cpu->gpr[6], (uint32_t)cpu->gpr[7],
                    (uint32_t)cpu->gpr[29]);
            if (!rt->trapped && cpu->gpr[2])
                dump_guest_bytes(rt, "  passwd return object",
                                 (IrixGuestAddr)(uint32_t)cpu->gpr[2], 36u);
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
