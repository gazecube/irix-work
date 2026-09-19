#include "IrixGuest.h"

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
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

uint64_t irix_guest_read64be(IrixGuestRuntime *rt, IrixGuestAddr addr)
{
    uint32_t hi = irix_guest_read32be(rt, addr);
    uint32_t lo = irix_guest_read32be(rt, addr + 4);
    return ((uint64_t)hi << 32) | lo;
}

void irix_guest_write8(IrixGuestRuntime *rt, IrixGuestAddr addr, uint8_t value)
{
    uint8_t *p = (uint8_t *)irix_guest_ptr(rt, addr, 1);
    if (!p) { memory_fault(rt, addr); return; }
    *p = value;
}

void irix_guest_write16be(IrixGuestRuntime *rt, IrixGuestAddr addr, uint16_t value)
{
    uint8_t *p = (uint8_t *)irix_guest_ptr(rt, addr, 2);
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

int irix_guest_register_function(IrixGuestRuntime *rt, IrixGuestAddr address,
                                 IrixGuestFunction function, const char *name)
{
    IrixGuestFunctionEntry *entry;
    if (!function || rt->function_count >= IRIX_GUEST_MAX_FUNCTIONS)
        return -1;
    entry = &rt->functions[rt->function_count++];
    entry->address = address;
    entry->function = function;
    entry->name = name;
    return 0;
}

IrixGuestFunctionEntry *irix_guest_find_function(IrixGuestRuntime *rt, IrixGuestAddr address)
{
    size_t i;
    for (i = 0; i < rt->function_count; ++i)
        if (rt->functions[i].address == address)
            return &rt->functions[i];
    return 0;
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

    /* Prefer the extracted IRIX filesystem when that path exists there.
       Otherwise leave ordinary host paths such as /tmp available. */
    if (stat(buf, &st) == 0)
        return buf;
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

static int irix_syssgi_elfmap(IrixGuestRuntime *rt, int fd,
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
        if (memsz > filesz)
            memset((unsigned char *)dst + filesz, 0, memsz - filesz);

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
    case 1020: /* getpid */
        syscall_result(cpu, (int64_t)getpid());
        return;
    case 1024: /* getuid */
        syscall_result(cpu, (int64_t)getuid());
        return;
    case 1040: { /* syssgi */
        uint32_t request = (uint32_t)cpu->gpr[4];
        if (request == 68u) { /* SGI_ELFMAP */
            int fd = (int)(int32_t)cpu->gpr[5];
            IrixGuestAddr phdr = (IrixGuestAddr)(uint32_t)cpu->gpr[6];
            uint32_t count = (uint32_t)cpu->gpr[7];
            IrixGuestAddr first = 0;
            if (irix_syssgi_elfmap(rt, fd, phdr, count, &first) != 0) {
                syscall_result(cpu, -1);
                return;
            }
            syscall_result(cpu, (int64_t)(uint32_t)first);
            return;
        }
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
    case 1047: /* getgid */
        syscall_result(cpu, (int64_t)getgid());
        return;
    case 1056: { /* sysmp(cmd, ...) */
        uint32_t cmd = (uint32_t)cpu->gpr[4];
        long n;
        switch (cmd) {
        case 1u: /* MP_NPROCS */
            n = sysconf(_SC_NPROCESSORS_CONF);
            if (n < 1) n = 1;
            syscall_result(cpu, (int64_t)n);
            return;
        case 2u: /* MP_NAPROCS */
            n = sysconf(_SC_NPROCESSORS_ONLN);
            if (n < 1) n = 1;
            syscall_result(cpu, (int64_t)n);
            return;
        case 14u: /* MP_PGSIZE */
            syscall_result(cpu, 4096);
            return;
        default:
            fprintf(stderr,
                    "UNIMPLEMENTED IRIX sysmp command %u pc=0x%08x "
                    "a1-a3=%08x,%08x,%08x\n",
                    cmd, (uint32_t)cpu->pc,
                    (uint32_t)cpu->gpr[5], (uint32_t)cpu->gpr[6],
                    (uint32_t)cpu->gpr[7]);
            errno = EINVAL;
            syscall_result(cpu, -1);
            return;
        }
    }
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

int irix_guest_call(IrixGuestRuntime *rt, IrixGuestCpu *cpu, IrixGuestAddr address)
{
    IrixGuestFunctionEntry *entry = irix_guest_find_function(rt, address);
    if (!entry) {
        IrixGuestAddr s4 = (IrixGuestAddr)(uint32_t)cpu->gpr[20];

        if (try_trivial_stripped_ctor(rt, cpu, address))
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
        uint64_t caller_gp = cpu->gpr[28];
        IrixGuestAddr caller_pc = cpu->pc;
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
        cpu->pc = address;
        rt->current_pc = address;
        /* N32 PIC entry convention: t9 contains the callee address. */
        cpu->gpr[25] = (uint64_t)(int64_t)(int32_t)address;
        entry->function(rt, cpu);
        /* $gp is callee-saved in the N32 ABI. Preserve the caller's value
           across all translated/native guest calls, including bridge entries
           that do not execute a guest epilogue themselves. */
        cpu->gpr[28] = caller_gp;
    }
    cpu->gpr[0] = 0;
    return rt->trapped ? -1 : 0;
}
