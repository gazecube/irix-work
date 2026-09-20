#include "IrixCompat.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/utsname.h>
#include <fcntl.h>

/*
 * This file ports the useful semantics of Linux/MIPS' historical IRIX
 * compatibility layer into our user-space guest runtime.  Kernel-only
 * mechanisms (copy_from_user, VMAs, current, fd tables, etc.) are expressed
 * through IrixGuestRuntime instead.
 */

static void compat_result(IrixGuestCpu *cpu, int64_t value)
{
    if (value < 0) {
        cpu->gpr[2] = (uint64_t)(int64_t)errno;
        cpu->gpr[7] = 1;
    } else {
        cpu->gpr[2] = (uint64_t)value;
        cpu->gpr[7] = 0;
    }
}

static int compat_range_contains_addr(IrixGuestAddr base, uint32_t len,
                                      IrixGuestAddr addr, uint32_t width)
{
    uint64_t lo = base;
    uint64_t hi = lo + len;
    uint64_t alo = addr;
    uint64_t ahi = alo + width;
    return alo >= lo && ahi <= hi;
}

static void compat_watch_libc_got(IrixGuestRuntime *rt, const char *where,
                                  IrixGuestAddr base, uint32_t len)
{
    const IrixGuestAddr slot = 0x0fb5291cu;
    if (!compat_range_contains_addr(base, len, slot, 4u))
        return;
    if (!irix_guest_ptr(rt, slot, 4u)) {
        fprintf(stderr, "IRIX libc GOT bulk watch: %s slot=0x%08x unmapped\n",
                where, slot);
        return;
    }
    fprintf(stderr,
            "IRIX libc GOT bulk watch: %s range=0x%08x+0x%x slot=0x%08x value=%08x\n",
            where, base, len, slot, irix_guest_read32be(rt, slot));
}

static int compat_elfmap(IrixGuestRuntime *rt, int fd,
                         IrixGuestAddr phdr_addr, uint32_t count,
                         IrixGuestAddr *first_out)
{
    uint32_t i;
    IrixGuestAddr first = 0;

    for (i = 0; i < count; ++i) {
        IrixGuestAddr p = phdr_addr + i * 32u;
        uint32_t type = irix_guest_read32be(rt, p + 0u);
        uint32_t offset = irix_guest_read32be(rt, p + 4u);
        IrixGuestAddr vaddr = irix_guest_read32be(rt, p + 8u);
        uint32_t filesz = irix_guest_read32be(rt, p + 16u);
        uint32_t memsz = irix_guest_read32be(rt, p + 20u);
        uint32_t flags = irix_guest_read32be(rt, p + 24u);
        void *dst;
        ssize_t got;

        if (rt->trapped) {
            errno = EFAULT;
            return -1;
        }
        if (type != 1u || filesz > memsz || !memsz) {
            errno = ENOEXEC;
            return -1;
        }

        dst = irix_guest_ptr(rt, vaddr, memsz);
        if (!dst) {
            if (irix_guest_map(rt, vaddr, memsz, flags) != 0) {
                errno = ENOMEM;
                return -1;
            }
            dst = irix_guest_ptr(rt, vaddr, memsz);
            if (!dst) {
                errno = EFAULT;
                return -1;
            }
        }

        compat_watch_libc_got(rt, "ELFMAP before pread", vaddr, filesz);
        if (filesz) {
            got = pread(fd, dst, filesz, (off_t)offset);
            if (got < 0)
                return -1;
            if ((uint32_t)got != filesz) {
                errno = ENOEXEC;
                return -1;
            }
        }
        compat_watch_libc_got(rt, "ELFMAP after pread", vaddr, filesz);
        if (memsz > filesz) {
            compat_watch_libc_got(rt, "ELFMAP before bss clear",
                                  vaddr + filesz, memsz - filesz);
            memset((unsigned char *)dst + filesz, 0, memsz - filesz);
            compat_watch_libc_got(rt, "ELFMAP after bss clear",
                                  vaddr + filesz, memsz - filesz);
        }

        if (!first)
            first = vaddr;

        fprintf(stderr,
                "IRIX SGI_ELFMAP: fd=%d seg=%u vaddr=0x%08x "
                "offset=0x%x filesz=0x%x memsz=0x%x flags=0x%x\n",
                fd, i, vaddr, offset, filesz, memsz, flags);
    }

    if (first_out)
        *first_out = first;
    return 0;
}

void irix_compat_syssgi(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    uint32_t request = (uint32_t)cpu->gpr[4];
    int64_t result;

    switch (request) {
    case 21u: /* SGI_SETPGID */
        result = (int64_t)setpgid((pid_t)(int32_t)cpu->gpr[5],
                                 (pid_t)(int32_t)cpu->gpr[6]);
        compat_result(cpu, result);
        return;

    case 22u: { /* SGI_SYSCONF */
        int name = (int)(int32_t)cpu->gpr[5];
        long value;

        /*
         * IRIX libc routes kernel-backed sysconf names through syssgi.
         * Keep IRIX selector numbers here rather than passing them directly
         * to host sysconf(), whose _SC_* numbering is ABI-specific.
         */
        switch (name) {
        case 1: /* _SC_ARG_MAX */
#ifdef ARG_MAX
            value = ARG_MAX;
#else
            value = 131072;
#endif
            break;
        case 2: /* _SC_CHILD_MAX */
            value = sysconf(_SC_CHILD_MAX);
            break;
        case 3: /* _SC_CLK_TCK */
            value = sysconf(_SC_CLK_TCK);
            break;
        case 4: /* _SC_NGROUPS_MAX */
            value = sysconf(_SC_NGROUPS_MAX);
            break;
        case 5: /* _SC_OPEN_MAX */
            value = sysconf(_SC_OPEN_MAX);
            break;
        default:
            fprintf(stderr, "UNIMPLEMENTED IRIX SGI_SYSCONF name %d\n", name);
            errno = EINVAL;
            compat_result(cpu, -1);
            return;
        }
        compat_result(cpu, (int64_t)value);
        return;
    }

    case 40u: { /* SGI_SETGROUPS */
        int count = (int)(int32_t)cpu->gpr[5];
        IrixGuestAddr list_addr = (IrixGuestAddr)(uint32_t)cpu->gpr[6];
        gid_t *groups = NULL;
        int i;

        if (count < 0 || count > 65536) {
            errno = EINVAL;
            compat_result(cpu, -1);
            return;
        }
        if (count) {
            if (!irix_guest_ptr(rt, list_addr, (size_t)count * 4u)) {
                errno = EFAULT;
                compat_result(cpu, -1);
                return;
            }
            groups = (gid_t *)calloc((size_t)count, sizeof(*groups));
            if (!groups) {
                errno = ENOMEM;
                compat_result(cpu, -1);
                return;
            }
            for (i = 0; i < count; ++i)
                groups[i] = (gid_t)irix_guest_read32be(rt, list_addr + (IrixGuestAddr)i * 4u);
        }

        compat_result(cpu, (int64_t)setgroups((size_t)count, groups));
        free(groups);
        return;
    }

    case 62u: /* SGI_SSYNC */
        sync();
        compat_result(cpu, 0);
        return;

    case 65u: /* SGI_GETSID */
        result = (int64_t)getsid((pid_t)(int32_t)cpu->gpr[5]);
        compat_result(cpu, result);
        return;

    case 68u: { /* SGI_ELFMAP */
        int fd = (int)(int32_t)cpu->gpr[5];
        IrixGuestAddr phdr = (IrixGuestAddr)(uint32_t)cpu->gpr[6];
        uint32_t count = (uint32_t)cpu->gpr[7];
        IrixGuestAddr first = 0;
        if (compat_elfmap(rt, fd, phdr, count, &first) != 0) {
            compat_result(cpu, -1);
            return;
        }
        compat_result(cpu, (int64_t)(uint32_t)first);
        return;
    }

    case 92u:
        /*
         * Used by the IRIX 6.5 runtime loader during early startup.  The
         * compatibility behavior needed by rld is successful completion with
         * no returned payload.
         */
        compat_result(cpu, 0);
        return;

    case 108u: /* SGI_TOSSTSAVE */
        compat_result(cpu, 0);
        return;

    case 129u: /* SGI_USE_FP_BCOPY / SGI_FP_BCOPY */
        compat_result(cpu, 0);
        return;

    default:
        fprintf(stderr,
                "UNIMPLEMENTED IRIX syssgi request %u pc=0x%08x "
                "a1-a3=%08x,%08x,%08x\n",
                request, (uint32_t)cpu->pc,
                (uint32_t)cpu->gpr[5], (uint32_t)cpu->gpr[6],
                (uint32_t)cpu->gpr[7]);
        rt->fault_addr = request;
        irix_guest_trap(rt, cpu, 0xeeee2304u);
        return;
    }
}

static int compat_copy_string(IrixGuestRuntime *rt, IrixGuestAddr out,
                              uint32_t count, const char *value,
                              uint32_t *required_out)
{
    size_t len = strlen(value);
    size_t required = len + 1u;
    size_t n;
    char *dst;

    if (required_out)
        *required_out = (uint32_t)required;
    if (!count)
        return 0;

    dst = (char *)irix_guest_ptr(rt, out, count);
    if (!dst) {
        errno = EFAULT;
        return -1;
    }

    n = required <= count ? required : count;
    if (n)
        memcpy(dst, value, n);
    if (required > count)
        dst[count - 1u] = '\0';
    return 0;
}

static int compat_range_overlaps(IrixGuestRuntime *rt,
                                 IrixGuestAddr addr, uint32_t len)
{
    uint64_t lo = addr;
    uint64_t hi = lo + len;
    size_t i;

    for (i = 0; i < rt->memory.count; ++i) {
        IrixGuestSegment *seg = &rt->memory.segments[i];
        uint64_t slo = seg->guest_base;
        uint64_t shi = slo + seg->size;
        if (lo < shi && hi > slo)
            return 1;
    }
    return 0;
}

static IrixGuestAddr compat_find_mmap_addr(IrixGuestRuntime *rt, uint32_t len)
{
    const uint32_t page = 16384u;
    IrixGuestAddr addr = 0x20000000u;
    uint32_t rounded = (len + page - 1u) & ~(page - 1u);

    while (addr < 0x2f000000u) {
        if (!compat_range_overlaps(rt, addr, rounded))
            return addr;
        addr += page;
    }
    return 0;
}

void irix_compat_mmap(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const uint32_t IRIX_MAP_SHARED = 0x1u;
    const uint32_t IRIX_MAP_PRIVATE = 0x2u;
    const uint32_t IRIX_MAP_TYPE = 0xfu;
    const uint32_t IRIX_MAP_FIXED = 0x10u;
    const uint32_t page = 16384u;
    IrixGuestAddr requested = (IrixGuestAddr)(uint32_t)cpu->gpr[4];
    uint32_t len = (uint32_t)cpu->gpr[5];
    uint32_t prot = (uint32_t)cpu->gpr[6];
    uint32_t flags = (uint32_t)cpu->gpr[7];
    int fd = (int)(int32_t)cpu->gpr[8];
    uint32_t off = (uint32_t)cpu->gpr[9];
    IrixGuestAddr addr;
    uint32_t rounded;
    void *dst;
    ssize_t got;

    if (!len) {
        errno = ENXIO;
        compat_result(cpu, -1);
        return;
    }
    if (off & (page - 1u)) {
        errno = EINVAL;
        compat_result(cpu, -1);
        return;
    }
    if ((flags & IRIX_MAP_TYPE) != IRIX_MAP_SHARED &&
        (flags & IRIX_MAP_TYPE) != IRIX_MAP_PRIVATE) {
        errno = EINVAL;
        compat_result(cpu, -1);
        return;
    }

    rounded = (len + page - 1u) & ~(page - 1u);
    if (flags & IRIX_MAP_FIXED) {
        if (!requested || (requested & (page - 1u))) {
            errno = EINVAL;
            compat_result(cpu, -1);
            return;
        }
        addr = requested;
    } else if (requested && !(requested & (page - 1u)) &&
               !compat_range_overlaps(rt, requested, rounded)) {
        addr = requested;
    } else {
        addr = compat_find_mmap_addr(rt, rounded);
    }

    if (!addr || compat_range_overlaps(rt, addr, rounded)) {
        errno = ENOMEM;
        compat_result(cpu, -1);
        return;
    }
    if (irix_guest_map(rt, addr, rounded, prot) != 0) {
        errno = ENOMEM;
        compat_result(cpu, -1);
        return;
    }

    dst = irix_guest_ptr(rt, addr, rounded);
    if (!dst) {
        errno = EFAULT;
        compat_result(cpu, -1);
        return;
    }

    /*
     * /dev/zero and other zero-length special mappings are already satisfied
     * by irix_guest_map()'s zero-filled allocation.  For regular file
     * mappings, populate the requested bytes from the descriptor.
     */
    if (fd >= 0) {
        got = pread(fd, dst, len, (off_t)off);
        if (got < 0) {
            compat_result(cpu, -1);
            return;
        }
        if ((uint32_t)got < len)
            memset((unsigned char *)dst + got, 0, len - (uint32_t)got);
    }

    fprintf(stderr,
            "IRIX mmap: addr=0x%08x len=0x%x prot=0x%x flags=0x%x "
            "fd=%d off=0x%x -> 0x%08x\n",
            requested, len, prot, flags, fd, off, addr);
    compat_result(cpu, (int64_t)(uint32_t)addr);
}

void irix_compat_munmap(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr addr = (IrixGuestAddr)(uint32_t)cpu->gpr[4];
    uint32_t len = (uint32_t)cpu->gpr[5];
    size_t i;

    if (!len || (addr & 0x3fffu)) {
        errno = EINVAL;
        compat_result(cpu, -1);
        return;
    }

    for (i = 0; i < rt->memory.count; ++i) {
        IrixGuestSegment *seg = &rt->memory.segments[i];
        if (seg->guest_base == addr && seg->size == len) {
            free(seg->host_base);
            memmove(seg, seg + 1,
                    (rt->memory.count - i - 1u) * sizeof(*seg));
            --rt->memory.count;
            compat_result(cpu, 0);
            return;
        }
    }

    /*
     * Full VMA splitting is not yet needed by startup.  Report unsupported
     * partial unmaps rather than silently corrupting the guest map.
     */
    errno = EINVAL;
    compat_result(cpu, -1);
}

void irix_compat_mprotect(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr addr = (IrixGuestAddr)(uint32_t)cpu->gpr[4];
    uint32_t len = (uint32_t)cpu->gpr[5];
    unsigned prot = (unsigned)(uint32_t)cpu->gpr[6];
    uint64_t lo = addr, hi = lo + len;
    size_t i;
    int touched = 0;

    if (!len || (addr & 0x3fffu)) {
        errno = EINVAL;
        compat_result(cpu, -1);
        return;
    }

    for (i = 0; i < rt->memory.count; ++i) {
        IrixGuestSegment *seg = &rt->memory.segments[i];
        uint64_t slo = seg->guest_base, shi = slo + seg->size;
        if (lo <= slo && hi >= shi) {
            seg->flags = prot;
            touched = 1;
        }
    }

    if (!touched) {
        errno = ENOMEM;
        compat_result(cpu, -1);
        return;
    }
    compat_result(cpu, 0);
}

void irix_compat_prctl(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    uint32_t option = (uint32_t)cpu->gpr[4];
    long n;

    (void)rt;
    switch (option) {
    case 1u: /* PR_MAXPROCS */
    case 5u: /* PR_MAXPPROCS */
        n = sysconf(_SC_CHILD_MAX);
        if (n < 1) n = 1;
        compat_result(cpu, (int64_t)n);
        return;

    case 4u: /* PR_GETSTACKSIZE */
        /* Match the 1 MiB N32 stack currently constructed by AppRunner. */
        compat_result(cpu, 0x00100000);
        return;

    case 13u: /* PR_GETSHMASK */
        /* No sproc share-group attributes are active in the host process. */
        compat_result(cpu, 0);
        return;

    case 14u: /* PR_GETNSHARE */
        /*
         * The translated process is currently single-threaded/single-member.
         * libc's __exit() uses this as the compatibility fallback when
         * PR_LASTSHEXIT is unavailable.
         */
        compat_result(cpu, 1);
        return;

    case 22u: /* PR_LASTSHEXIT */
        /*
         * IRIX 6.5.3+ asks whether this is the last member of an sproc share
         * group before running process-wide atexit/rld shutdown handlers.
         * We currently model one guest process/member, so the answer is yes.
         */
        compat_result(cpu, 1);
        return;

    default:
        fprintf(stderr,
                "UNIMPLEMENTED IRIX prctl option %u pc=0x%08x "
                "a1-a3=%08x,%08x,%08x\n",
                option, (uint32_t)cpu->pc,
                (uint32_t)cpu->gpr[5], (uint32_t)cpu->gpr[6],
                (uint32_t)cpu->gpr[7]);
        errno = EINVAL;
        compat_result(cpu, -1);
        return;
    }
}

void irix_compat_systeminfo(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    uint32_t cmd = (uint32_t)cpu->gpr[4];
    IrixGuestAddr out = (IrixGuestAddr)(uint32_t)cpu->gpr[5];
    uint32_t count = (uint32_t)cpu->gpr[6];
    struct utsname u;
    char scratch[64];
    const char *value = 0;
    uint32_t required = 0;
    long ncpu;

    /*
     * IRIX 6.5.7 systeminfo(2), from irix/kern/os/name.c and
     * irix/kern/sys/systeminfo.h.  The syscall returns the full string size,
     * including the terminating NUL, even when the caller's buffer truncates.
     */
    memset(&u, 0, sizeof(u));
    (void)uname(&u);

    switch (cmd) {
    case 1u:   /* SI_SYSNAME */
        value = "IRIX";
        break;
    case 2u:   /* SI_HOSTNAME */
        value = u.nodename[0] ? u.nodename : "IRIS";
        break;
    case 3u:   /* SI_RELEASE */
        value = "6.5";
        break;
    case 4u:   /* SI_VERSION */
        value = "6.5.7";
        break;
    case 5u:   /* SI_MACHINE */
        value = "IP30";
        break;
    case 6u:   /* SI_ARCHITECTURE */
        value = "mips";
        break;
    case 7u:   /* SI_HW_SERIAL */
    case 111u: /* _MIPS_SI_SERIAL */
        value = "0";
        break;
    case 8u:   /* SI_HW_PROVIDER */
        value = "sgi";
        break;
    case 10u:  /* SI_INITTAB_NAME */
        value = "/etc/inittab";
        break;
    case 100u: /* _MIPS_SI_VENDOR */
    case 101u: /* _MIPS_SI_OS_PROVIDER */
        value = "Silicon Graphics, Inc.";
        break;
    case 102u: /* _MIPS_SI_OS_NAME */
        value = "IRIX";
        break;
    case 103u: /* _MIPS_SI_HW_NAME */
        value = "IP30";
        break;
    case 104u: /* _MIPS_SI_NUM_PROCESSORS */
    case 110u: /* _MIPS_SI_AVAIL_PROCESSORS */
        ncpu = sysconf(_SC_NPROCESSORS_ONLN);
        if (ncpu < 1) ncpu = 1;
        snprintf(scratch, sizeof(scratch), "%ld", ncpu);
        value = scratch;
        break;
    case 105u: /* _MIPS_SI_HOSTID */
        value = "0";
        break;
    case 106u: /* _MIPS_SI_OSREL_MAJ */
        value = "6";
        break;
    case 107u: /* _MIPS_SI_OSREL_MIN */
        value = "5";
        break;
    case 108u: /* _MIPS_SI_OSREL_PATCH */
        value = "0";
        break;
    case 109u: /* _MIPS_SI_PROCESSORS */
        /*
         * rld asks this immediately after mapping itself.  Present a stable
         * MIPS-IV R10000 identity instead of leaking the x86 host CPU into
         * IRIX runtime selection.
         */
        value = "R10000 2.6";
        break;
    default:
        fprintf(stderr,
                "UNIMPLEMENTED IRIX sysinfo command %u pc=0x%08x "
                "buf=%08x count=%u\n",
                cmd, (uint32_t)cpu->pc, out, count);
        errno = EINVAL;
        compat_result(cpu, -1);
        return;
    }

    if (compat_copy_string(rt, out, count, value, &required) != 0) {
        compat_result(cpu, -1);
        return;
    }

    fprintf(stderr, "IRIX sysinfo: cmd=%u value=\"%s\" size=%u\n",
            cmd, value, required);
    compat_result(cpu, (int64_t)required);
}

void irix_compat_sysmp(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    uint32_t cmd = (uint32_t)cpu->gpr[4];
    long n;

    (void)rt;
    switch (cmd) {
    case 1u: /* MP_NPROCS */
        n = sysconf(_SC_NPROCESSORS_CONF);
        if (n < 1) n = 1;
        compat_result(cpu, (int64_t)n);
        return;

    case 2u: /* MP_NAPROCS */
        n = sysconf(_SC_NPROCESSORS_ONLN);
        if (n < 1) n = 1;
        compat_result(cpu, (int64_t)n);
        return;

    case 14u: /* MP_PGSIZE */
        compat_result(cpu, 16384);
        return;

    default:
        fprintf(stderr,
                "UNIMPLEMENTED IRIX sysmp command %u pc=0x%08x "
                "a1-a3=%08x,%08x,%08x\n",
                cmd, (uint32_t)cpu->pc,
                (uint32_t)cpu->gpr[5], (uint32_t)cpu->gpr[6],
                (uint32_t)cpu->gpr[7]);
        errno = EINVAL;
        compat_result(cpu, -1);
        return;
    }
}
