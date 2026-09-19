#include "IrixCompat.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

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
