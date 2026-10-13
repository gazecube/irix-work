#define _GNU_SOURCE 1
#include <sys/mman.h>
#include "IrixCompat.h"
#include "IrixSyssgiNumbers.h"
#include "IrixKernelInfo.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
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

        if (filesz) {
            got = pread(fd, dst, filesz, (off_t)offset);
            if (got < 0)
                return -1;
            if ((uint32_t)got != filesz) {
                errno = ENOEXEC;
                return -1;
            }
        }
        if (memsz > filesz) {
            memset((unsigned char *)dst + filesz, 0, memsz - filesz);
        }

        if (!first)
            first = vaddr;


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
    case SGI_SETPGID: /* SGI_SETPGID */
        result = (int64_t)setpgid((pid_t)(int32_t)cpu->gpr[5],
                                 (pid_t)(int32_t)cpu->gpr[6]);
        compat_result(cpu, result);
        return;

    case SGI_SYSCONF: { /* SGI_SYSCONF */
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
        case 25: /* _SC_ACL: no IRIX ACL extension in this runtime. */
        case 26: /* _SC_AUDIT */
        case 27: /* _SC_INF */
        case 28: /* _SC_MAC */
        case 29: /* _SC_CAP: this runtime exposes no IRIX capabilities. */
        case 30: /* _SC_IP_SECOPTS */
            value = 0;
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

    case SGI_SETGROUPS: { /* SGI_SETGROUPS */
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

    case SGI_GETGROUPS: {
        int capacity = (int)(int32_t)cpu->gpr[5];
        IrixGuestAddr list_addr = (IrixGuestAddr)(uint32_t)cpu->gpr[6];
        gid_t *groups;
        int count, i;
        if (capacity < 0 || capacity > 65536) {
            errno = EINVAL;
            compat_result(cpu, -1);
            return;
        }
        groups = calloc((size_t)(capacity ? capacity : 1), sizeof(*groups));
        if (!groups) {
            errno = ENOMEM;
            compat_result(cpu, -1);
            return;
        }
        count = getgroups(capacity, groups);
        if (count >= 0 && capacity) {
            if (!list_addr || !irix_guest_ptr(rt, list_addr, (size_t)count * 4u)) {
                errno = EFAULT;
                count = -1;
            } else for (i = 0; i < count; ++i)
                irix_guest_write32be(rt, list_addr + (IrixGuestAddr)i * 4u,
                                     (uint32_t)groups[i]);
        }
        free(groups);
        compat_result(cpu, count);
        return;
    }

    case SGI_SSYNC: /* SGI_SSYNC */
        sync();
        compat_result(cpu, 0);
        return;

    case SGI_GETPGID: /* SGI_GETPGID: SVR4 getpgid(pid) */
        result = (int64_t)getpgid((pid_t)(int32_t)cpu->gpr[5]);
        compat_result(cpu, result);
        return;

    case SGI_GETSID: /* SGI_GETSID */
        result = (int64_t)getsid((pid_t)(int32_t)cpu->gpr[5]);
        compat_result(cpu, result);
        return;

    case SGI_FDHI: { /* SGI_FDHI returns the highest open descriptor. */
        int fd, highest = -1;
        for (fd = 0; fd < 65536; ++fd)
            if (fcntl(fd, F_GETFD) >= 0 || errno != EBADF)
                highest = fd;
        compat_result(cpu, highest);
        return;
    }

    case SGI_ELFMAP: { /* SGI_ELFMAP */
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

    case SGI_RXEV_GET: { /* SGI_RXEV_GET */
        /*
         * SGI syssgi.c: tell libc/rld whether loader-control environment
         * variables (_RLD_ROOT, _RLD_PATH, LD_LIBRARY_PATH, ...) are unsafe.
         * They are unsafe after an effective/real uid or gid transition, or
         * when capabilities are present.  Our guest personality does not
         * currently expose IRIX capabilities, so only the credential test
         * applies.  arg1==0 permits IRIX's real-uid-root exception.
         */
        uid_t ruid = getuid();
        uid_t euid = geteuid();
        gid_t rgid = getgid();
        gid_t egid = getegid();
        int disallow = (euid != ruid || egid != rgid);

        if (ruid == 0 && (uint32_t)cpu->gpr[5] == 0u)
            disallow = 0;
        compat_result(cpu, disallow ? 1 : 0);
        return;
    }

    case SGI_INVENT: { /* hardware inventory (libc getinvent) */
        uint32_t sub = (uint32_t)cpu->gpr[5];
        const uint32_t isize = 24u;   /* N32 inventory_t */
        if (sub == 1u) {              /* SGI_INV_SIZEOF */
            compat_result(cpu, (int64_t)isize);
            return;
        }
        if (sub == 2u) {              /* SGI_INV_READ(buf, bytes) */
            IrixGuestAddr buf = (IrixGuestAddr)(uint32_t)cpu->gpr[6];
            uint32_t bytes = (uint32_t)cpu->gpr[7];
            IrixKinfoInvent recs[32];
            size_t have = irix_kinfo_inventory(recs, 32u), i;
            uint32_t written = 0;
            for (i = 0; i < have && bytes - written >= isize; ++i) {
                IrixGuestAddr a = buf + written;
                if (!irix_guest_ptr(rt, a, isize)) {
                    errno = EFAULT;
                    compat_result(cpu, -1);
                    return;
                }
                irix_guest_write32be(rt, a + 0u, 0u);   /* inv_next */
                irix_guest_write32be(rt, a + 4u, (uint32_t)recs[i].inv_class);
                irix_guest_write32be(rt, a + 8u, (uint32_t)recs[i].inv_type);
                irix_guest_write32be(rt, a + 12u, (uint32_t)recs[i].inv_controller);
                irix_guest_write32be(rt, a + 16u, (uint32_t)recs[i].inv_unit);
                irix_guest_write32be(rt, a + 20u, (uint32_t)recs[i].inv_state);
                written += isize;
            }
            compat_result(cpu, (int64_t)written);
            return;
        }
        errno = EINVAL;
        compat_result(cpu, -1);
        return;
    }

    case 152: { /* SGI_CONST(sub, buf, len, *outlen): system sizing constants */
        uint32_t sub = (uint32_t)cpu->gpr[5];
        IrixGuestAddr buf = (IrixGuestAddr)(uint32_t)cpu->gpr[6];
        uint32_t len = (uint32_t)cpu->gpr[7];
        IrixGuestAddr outlen = (IrixGuestAddr)(uint32_t)cpu->gpr[8];
        uint32_t vals[3];
        uint32_t nwords = 0, bytes, i;

        switch (sub) {
        case 3u: /* SGICONST_PAGESZ: struct pageconst */
            vals[0] = 16384u;   /* p_pagesz  */
            vals[1] = 14u;      /* p_pnumshft */
            vals[2] = 16384u;   /* p_nbpp    */
            nwords = 3;
            break;
        case 4u: /* SGICONST_PARAM: struct paramconst */
            vals[0] = 1u;       /* p_usize    */
            vals[1] = 1u;       /* p_extusize */
            nwords = 2;
            break;
        case 5u: /* SGICONST_SCACHE_SETASSOC: *(int *) buf */
            if (!irix_guest_ptr(rt, buf, 4u)) {
                errno = EFAULT; compat_result(cpu, -1); return;
            }
            irix_guest_write32be(rt, buf, (uint32_t)irix_kinfo_scache_assoc());
            compat_result(cpu, 0);
            return;
        case 6u: /* SGICONST_SCACHE_SIZE: *(int *) buf */
            if (!irix_guest_ptr(rt, buf, 4u)) {
                errno = EFAULT; compat_result(cpu, -1); return;
            }
            irix_guest_write32be(rt, buf, (uint32_t)irix_kinfo_scache_bytes());
            compat_result(cpu, 0);
            return;
        default:
            errno = EINVAL;
            compat_result(cpu, -1);
            return;
        }
        bytes = nwords * 4u;
        if (bytes > len) bytes = len & ~3u;
        if (bytes && !irix_guest_ptr(rt, buf, bytes)) {
            errno = EFAULT; compat_result(cpu, -1); return;
        }
        for (i = 0; i < bytes / 4u; ++i)
            irix_guest_write32be(rt, buf + i * 4u, vals[i]);
        if (outlen && irix_guest_ptr(rt, outlen, 4u))
            irix_guest_write32be(rt, outlen, bytes);
        compat_result(cpu, 0);
        return;
    }

    case 102: /* SGI_GET_EVCONF: Everest configuration; not an Everest */
        errno = EINVAL;
        compat_result(cpu, -1);
        return;

    case SGI_TOSSTSAVE: /* SGI_TOSSTSAVE */
        compat_result(cpu, 0);
        return;

    case SGI_NOHANG: /* SGI_NOHANG: disable NFS client hangs */
        /* Linux has no process-wide equivalent to IRIX's NFS no-hang flag. */
        compat_result(cpu, 0);
        return;

    case SGI_USE_FP_BCOPY: /* SGI_USE_FP_BCOPY / SGI_FP_BCOPY */
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

    (void)0;
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

    case 12u: /* PR_TERMCHILD: host child lifetime is tied to this runner. */
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

    case SGI_SYSCONF: /* PR_LASTSHEXIT */
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
    case 6u:   /* SI_ARCHITECTURE */
        value = irix_kinfo_machine();
        break;
    case 7u:   /* SI_HW_SERIAL */
    case 111u: /* _MIPS_SI_SERIAL */
        value = "0";
        break;
    case 8u:   /* SI_HW_PROVIDER */
        value = "unknown";
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
        value = irix_kinfo_machine();
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
    case SGI_TOSSTSAVE: /* _MIPS_SI_OSREL_PATCH */
        value = "0";
        break;
    case 109u: /* _MIPS_SI_PROCESSORS */
        /*
         * rld asks this immediately after mapping itself to pick the ISA
         * directory (mips3/mips4) of the libraries.  The *guest code* is
         * MIPS-IV, so this is the instruction-set profile it executes, not
         * a claim about the hardware (see SI_MACHINE).
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

    compat_result(cpu, (int64_t)required);
}

/* sysget(name, buf, buflen, flags, cookie): IRIX 6.5 kernel statistics.
 * Returns 0 or an errno value (positive) like the kernel; the caller turns
 * that into a -1/errno return. */
int irix_compat_sysget(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    enum { SGT_INFO = 0x1, SGT_READ = 0x2, SGT_WRITE = 0x4, SGT_SUM = 0x8,
           SGT_CPUS = 0x10, SGT_NODES = 0x20, SGT_STAT = 0x40,
           SC_DONE = 2, COOKIE_BYTES = 76, KSYM_OFF = 14, SGT_KSYM = 28 };
    uint32_t name = (uint32_t)cpu->gpr[4];
    IrixGuestAddr buf = (IrixGuestAddr)(uint32_t)cpu->gpr[5];
    uint32_t buflen = (uint32_t)cpu->gpr[6];
    uint32_t flags = (uint32_t)cpu->gpr[7];
    IrixGuestAddr cookie = (IrixGuestAddr)(uint32_t)cpu->gpr[8];
    int ncpu = irix_kinfo_nprocs();
    int percpu = 0, size = 0, num = 1, i;
    uint32_t words[1280];
    size_t nwords;

    if (name < 1u || name > 33u) return EINVAL;
    if (!irix_guest_ptr(rt, cookie, COOKIE_BYTES)) return EFAULT;
    if (irix_guest_read32be(rt, cookie) == SC_DONE) return EINVAL;
    if (!flags) return EINVAL;

    switch (name) {
    case 1: case 3: case 4: case 32: case 5: case 6: case 9: case 13:
    case 14: case 20:
        percpu = 1;                            /* SA_PERCPU objects */
        /* fall through */
    case 2: case 8: case 25: case 26:
        size = (int)irix_kinfo_sa_size((int)name);
        break;
    case SGT_KSYM: {
        char sym[64];
        const char *s = irix_guest_cstr(rt, cookie + KSYM_OFF, 60u);
        if (!s) return EFAULT;
        snprintf(sym, sizeof(sym), "%s", s);
        if (irix_kinfo_ksym_info(sym, &size, &num) != 0) return ENOENT;
        if (flags & (SGT_CPUS | SGT_NODES)) return EINVAL;
        if (flags & SGT_WRITE) return ENOTSUP;
        if (flags & SGT_INFO) {
            if (buflen < 12u || !irix_guest_ptr(rt, buf, 12u)) return EFAULT;
            irix_guest_write32be(rt, buf, (uint32_t)size);
            irix_guest_write32be(rt, buf + 4u, (uint32_t)num);
            irix_guest_write32be(rt, buf + 8u, (uint32_t)num);
        } else if (flags & SGT_READ) {
            uint32_t want = (uint32_t)size * (uint32_t)num;
            nwords = want / 4u;
            if (nwords > 1024u) return EINVAL;
            if (buflen < want || !irix_guest_ptr(rt, buf, want)) return EFAULT;
            if (irix_kinfo_ksym_get(sym, words, nwords) != 0) return ENOENT;
            for (i = 0; (size_t)i < nwords; ++i)
                irix_guest_write32be(rt, buf + (IrixGuestAddr)(i * 4), words[i]);
        } else {
            return EINVAL;
        }
        irix_guest_write32be(rt, cookie, SC_DONE);
        return 0;
    }
    default:
        return ENOENT;                         /* not provided here */
    }
    if (!size) return ENOENT;

    if ((flags & (SGT_CPUS | SGT_NODES)) && !percpu) return EINVAL;
    if (flags & SGT_NODES) return ENOTSUP;
    if (flags & SGT_WRITE) return ENOTSUP;

    if (flags & SGT_INFO) {
        int n = (flags & SGT_CPUS) ? ncpu : 1;
        if (buflen < 12u || !irix_guest_ptr(rt, buf, 12u)) return EFAULT;
        irix_guest_write32be(rt, buf, (uint32_t)size);
        irix_guest_write32be(rt, buf + 4u, (uint32_t)n);
        irix_guest_write32be(rt, buf + 8u, (uint32_t)n);
    } else if (flags & SGT_READ) {
        int n = (flags & SGT_CPUS) ? ncpu : 1;
        uint32_t want = (uint32_t)size * (uint32_t)n;
        int c;
        if (buflen < want || !irix_guest_ptr(rt, buf, want)) return EFAULT;
        for (c = 0; c < n; ++c) {
            int which = (flags & SGT_CPUS) ? c : -1;
            if (irix_kinfo_sa_get((int)(name == 32 ? 32 : (int)name),
                                  which, words, (size_t)size / 4u) != 0)
                return EINVAL;
            for (i = 0; i < size / 4; ++i)
                irix_guest_write32be(rt, buf + (IrixGuestAddr)(c * size + i * 4),
                                     words[i]);
        }
    } else {
        return EINVAL;
    }
    irix_guest_write32be(rt, cookie, SC_DONE);
    return 0;
}


/*
 * Synthetic kernel memory for programs that read /dev/kmem at addresses they
 * obtain from sysmp(MP_KERNADDR, MPKA_*) (gr_osview).  The image is small and
 * served from a memfd; sysmp returns the offset into it (small and positive,
 * so the syscall layer does not mistake it for an error).
 */
#define KMEM_IMAGE_SIZE 0x10000u
#define KMEM_K0BASE     0x80000000u
#define KMEM_OFF_PHYSMEM 0x100u
#define KMEM_OFF_AVENRUN 0x300u
#define KMEM_OFF_VAR     0x1000u
#define KMEM_OFF_BUF     0x2000u
#define KMEM_OFF_GENERIC 0x400u

int irix_kmem_addr(int sel)
{
    switch (sel) {
    case 1:  return (int)KMEM_OFF_BUF;       /* MPKA_BUF */
    case 2:  return (int)KMEM_OFF_VAR;       /* MPKA_VAR */
    case 19: return (int)KMEM_OFF_AVENRUN;
    case 20: return (int)KMEM_OFF_PHYSMEM;
    default:
        if (sel > 0 && sel < 64)
            return (int)(KMEM_OFF_GENERIC + 16u * (uint32_t)sel);
        return -1;
    }
}

static void kmem_put32(unsigned char *img, uint32_t off, uint32_t v)
{
    img[off] = (unsigned char)(v >> 24); img[off + 1] = (unsigned char)(v >> 16);
    img[off + 2] = (unsigned char)(v >> 8); img[off + 3] = (unsigned char)v;
}

/* Returns a read-only fd holding the synthetic kernel image, or -1. */
int irix_kmem_open(void)
{
    unsigned char *img = calloc(1, KMEM_IMAGE_SIZE);
    long pages = sysconf(_SC_PHYS_PAGES), psz = sysconf(_SC_PAGESIZE);
    uint64_t bytes;
    int fd;
    if (!img) return -1;
    bytes = (pages > 0 && psz > 0) ? (uint64_t)pages * (uint64_t)psz : 1ULL << 30;
    kmem_put32(img, KMEM_OFF_PHYSMEM, (uint32_t)(bytes / 16384u));
    kmem_put32(img, KMEM_OFF_VAR, 1000u);            /* v_buf: buffer count */
    fd = memfd_create("irix-kmem", 0);
    if (fd >= 0) {
        if (write(fd, img, KMEM_IMAGE_SIZE) != (ssize_t)KMEM_IMAGE_SIZE) {
            close(fd); fd = -1;
        } else {
            lseek(fd, 0, SEEK_SET);
        }
    }
    free(img);
    return fd;
}

void irix_compat_sysmp(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    uint32_t cmd = (uint32_t)cpu->gpr[4];
    long n;

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

    case 13u: /* MP_SCHED(...): scheduler control (e.g. realtime priority) */
        compat_result(cpu, 0);
        return;

    case 8u: { /* MP_KERNADDR(sel): address of a kernel structure */
        int a = irix_kmem_addr((int)(int32_t)cpu->gpr[5]);
        if (a == -1) errno = EINVAL;
        compat_result(cpu, (int64_t)a);
        return;
    }

    case 14u: /* MP_PGSIZE */
        compat_result(cpu, 16384);
        return;

    case 42u: /* MP_NUMNODES: one node */
        compat_result(cpu, 1);
        return;

    case 21u: { /* MP_STAT(struct pda_stat *): processor status */
        IrixGuestAddr buf = (IrixGuestAddr)(uint32_t)cpu->gpr[5];
        int ncpu = irix_kinfo_nprocs(), i;
        for (i = 0; i < ncpu; ++i) {
            IrixGuestAddr a = buf + (IrixGuestAddr)(i * 8);
            /* PDAF_MASTER|PDAF_CLOCK on cpu 0, PDAF_ENABLED everywhere */
            uint32_t flags = 0x4u | (i == 0 ? 0x3u : 0u);
            if (!irix_guest_ptr(rt, a, 8u)) {
                errno = EFAULT;
                compat_result(cpu, -1);
                return;
            }
            irix_guest_write32be(rt, a, (uint32_t)i);
            irix_guest_write32be(rt, a + 4u, flags);
        }
        compat_result(cpu, 0);
        return;
    }

    case 9u: { /* MP_SASZ(sub): size of a system-activity record */
        size_t sz = irix_kinfo_sa_size((int)(int32_t)cpu->gpr[5]);
        if (!sz) {
            errno = EINVAL;
            compat_result(cpu, -1);
            return;
        }
        compat_result(cpu, (int64_t)sz);
        return;
    }

    case 10u:   /* MP_SAGET(sub, buf, size): summed over all processors */
    case 15u: { /* MP_SAGET1(sub, buf, size, cpu): one processor */
        int sub = (int)(int32_t)cpu->gpr[5];
        IrixGuestAddr buf = (IrixGuestAddr)(uint32_t)cpu->gpr[6];
        uint32_t size = (uint32_t)cpu->gpr[7];
        int which = cmd == 15u ? (int)(int32_t)cpu->gpr[8] : -1;
        size_t need = irix_kinfo_sa_size(sub), i;
        uint32_t words[1280];
        if (!need && sub == 14) {
            /* MPSA_TCPIPSTATS (struct kna): no kernel network counters
             * here yet; an all-zero record of the size asked for. */
            uint32_t zlen = size > 65536u ? 65536u : size;
            uint32_t z;
            if (!irix_guest_ptr(rt, buf, zlen)) {
                errno = EFAULT;
                compat_result(cpu, -1);
                return;
            }
            for (z = 0; z + 4u <= zlen; z += 4u)
                irix_guest_write32be(rt, buf + z, 0u);
            compat_result(cpu, 0);
            return;
        }
        if (!need || need > sizeof(words) || size < need) {
            errno = need ? EINVAL : EINVAL;
            compat_result(cpu, -1);
            return;
        }
        if (cmd == 15u && which < 0) {
            errno = EINVAL;
            compat_result(cpu, -1);
            return;
        }
        if (irix_kinfo_sa_get(sub, which, words, need / 4u) != 0) {
            errno = EINVAL;
            compat_result(cpu, -1);
            return;
        }
        if (!irix_guest_ptr(rt, buf, need)) {
            errno = EFAULT;
            compat_result(cpu, -1);
            return;
        }
        for (i = 0; i < need / 4u; ++i)
            irix_guest_write32be(rt, buf + (IrixGuestAddr)(i * 4u), words[i]);
        compat_result(cpu, 0);
        return;
    }

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
