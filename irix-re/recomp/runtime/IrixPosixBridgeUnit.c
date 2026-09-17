#define irix_posix_bridge_lookup irix_posix_bridge_lookup_base
#include "IrixPosixBridge.c"
#undef irix_posix_bridge_lookup

#include <sched.h>
#include <sys/select.h>
#include <sys/time.h>

#define IRIX_FD_SETSIZE 1024
#define IRIX_FDSET_BYTES 128u
#define IRIX_SGINAP_HZ 100L
#define IRIX_SIGSET_WORDS 4u
#define IRIX_SIGSET_BYTES (IRIX_SIGSET_WORDS * 4u)
#define IRIX_NSIG 128

static int valid_irix_sigset(IrixGuestRuntime *rt, IrixGuestAddr addr)
{
    return addr && irix_guest_ptr(rt, addr, IRIX_SIGSET_BYTES) != 0;
}

static void bridge_sigemptyset(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr addr = (uint32_t)cpu->gpr[4];
    unsigned i;
    if (!valid_irix_sigset(rt, addr)) {
        errno = EFAULT;
        ret_i32(cpu, -1);
        return;
    }
    for (i = 0; i < IRIX_SIGSET_WORDS; ++i)
        irix_guest_write32be(rt, addr + (IrixGuestAddr)i * 4u, 0u);
    ret_i32(cpu, rt->trapped ? -1 : 0);
}

static void bridge_sigfillset(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr addr = (uint32_t)cpu->gpr[4];
    unsigned i;
    if (!valid_irix_sigset(rt, addr)) {
        errno = EFAULT;
        ret_i32(cpu, -1);
        return;
    }
    for (i = 0; i < IRIX_SIGSET_WORDS; ++i)
        irix_guest_write32be(rt, addr + (IrixGuestAddr)i * 4u, 0xffffffffu);
    ret_i32(cpu, rt->trapped ? -1 : 0);
}

static int irix_sig_bit(int signo, unsigned *word, uint32_t *mask)
{
    unsigned bit;
    if (signo <= 0 || signo > IRIX_NSIG) return -1;
    bit = (unsigned)(signo - 1);
    *word = bit / 32u;
    *mask = 1u << (bit & 31u);
    return 0;
}

static void bridge_sigaddset(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr addr = (uint32_t)cpu->gpr[4];
    int signo = (int)(int32_t)cpu->gpr[5];
    unsigned word;
    uint32_t mask, bits;
    if (!valid_irix_sigset(rt, addr)) {
        errno = EFAULT;
        ret_i32(cpu, -1);
        return;
    }
    if (irix_sig_bit(signo, &word, &mask) != 0) {
        errno = EINVAL;
        ret_i32(cpu, -1);
        return;
    }
    bits = irix_guest_read32be(rt, addr + (IrixGuestAddr)word * 4u);
    irix_guest_write32be(rt, addr + (IrixGuestAddr)word * 4u, bits | mask);
    ret_i32(cpu, rt->trapped ? -1 : 0);
}

static void bridge_sigdelset(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr addr = (uint32_t)cpu->gpr[4];
    int signo = (int)(int32_t)cpu->gpr[5];
    unsigned word;
    uint32_t mask, bits;
    if (!valid_irix_sigset(rt, addr)) {
        errno = EFAULT;
        ret_i32(cpu, -1);
        return;
    }
    if (irix_sig_bit(signo, &word, &mask) != 0) {
        errno = EINVAL;
        ret_i32(cpu, -1);
        return;
    }
    bits = irix_guest_read32be(rt, addr + (IrixGuestAddr)word * 4u);
    irix_guest_write32be(rt, addr + (IrixGuestAddr)word * 4u, bits & ~mask);
    ret_i32(cpu, rt->trapped ? -1 : 0);
}

static void bridge_sigismember(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr addr = (uint32_t)cpu->gpr[4];
    int signo = (int)(int32_t)cpu->gpr[5];
    unsigned word;
    uint32_t mask, bits;
    if (!valid_irix_sigset(rt, addr)) {
        errno = EFAULT;
        ret_i32(cpu, -1);
        return;
    }
    if (irix_sig_bit(signo, &word, &mask) != 0) {
        errno = EINVAL;
        ret_i32(cpu, -1);
        return;
    }
    bits = irix_guest_read32be(rt, addr + (IrixGuestAddr)word * 4u);
    if (rt->trapped) {
        ret_i32(cpu, -1);
        return;
    }
    ret_i32(cpu, (bits & mask) ? 1 : 0);
}

static int read_irix_fdset(IrixGuestRuntime *rt, IrixGuestAddr addr,
                           int nfds, fd_set *out)
{
    int fd;
    if (!out) return -1;
    FD_ZERO(out);
    if (!addr) return 0;
    if (!irix_guest_ptr(rt, addr, IRIX_FDSET_BYTES)) return -1;
    for (fd = 0; fd < nfds && fd < IRIX_FD_SETSIZE; ++fd) {
        uint32_t word = irix_guest_read32be(rt,
            addr + (IrixGuestAddr)((unsigned)fd / 32u) * 4u);
        if (rt->trapped) return -1;
        if (word & (1u << ((unsigned)fd & 31u)))
            FD_SET(fd, out);
    }
    return 0;
}

static int write_irix_fdset(IrixGuestRuntime *rt, IrixGuestAddr addr,
                            int nfds, const fd_set *in)
{
    unsigned word;
    if (!addr) return 0;
    if (!in || !irix_guest_ptr(rt, addr, IRIX_FDSET_BYTES)) return -1;
    for (word = 0; word < IRIX_FDSET_BYTES / 4u; ++word) {
        uint32_t bits = 0;
        unsigned bit;
        for (bit = 0; bit < 32u; ++bit) {
            int fd = (int)(word * 32u + bit);
            if (fd < nfds && fd < IRIX_FD_SETSIZE && FD_ISSET(fd, in))
                bits |= 1u << bit;
        }
        irix_guest_write32be(rt, addr + (IrixGuestAddr)word * 4u, bits);
    }
    return rt->trapped ? -1 : 0;
}

static void bridge_select(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    int nfds = (int)(int32_t)cpu->gpr[4];
    IrixGuestAddr raddr = (uint32_t)cpu->gpr[5];
    IrixGuestAddr waddr = (uint32_t)cpu->gpr[6];
    IrixGuestAddr eaddr = (uint32_t)cpu->gpr[7];
    IrixGuestAddr taddr = (uint32_t)cpu->gpr[8];
    fd_set rfds, wfds, efds;
    fd_set *rp = raddr ? &rfds : 0;
    fd_set *wp = waddr ? &wfds : 0;
    fd_set *ep = eaddr ? &efds : 0;
    struct timeval tv;
    struct timeval *tp = 0;
    int rc;

    if (nfds < 0 || nfds > IRIX_FD_SETSIZE || nfds > FD_SETSIZE) {
        errno = EINVAL;
        ret_i32(cpu, -1);
        return;
    }
    if ((rp && read_irix_fdset(rt, raddr, nfds, rp) != 0) ||
        (wp && read_irix_fdset(rt, waddr, nfds, wp) != 0) ||
        (ep && read_irix_fdset(rt, eaddr, nfds, ep) != 0)) {
        errno = EFAULT;
        ret_i32(cpu, -1);
        return;
    }

    if (taddr) {
        if (!irix_guest_ptr(rt, taddr, 8u)) {
            errno = EFAULT;
            ret_i32(cpu, -1);
            return;
        }
        tv.tv_sec = (time_t)(int32_t)irix_guest_read32be(rt, taddr + 0u);
        tv.tv_usec = (suseconds_t)(int32_t)irix_guest_read32be(rt, taddr + 4u);
        if (rt->trapped) {
            ret_i32(cpu, -1);
            return;
        }
        tp = &tv;
    }

    rc = select(nfds, rp, wp, ep, tp);
    if (rc >= 0) {
        if ((rp && write_irix_fdset(rt, raddr, nfds, rp) != 0) ||
            (wp && write_irix_fdset(rt, waddr, nfds, wp) != 0) ||
            (ep && write_irix_fdset(rt, eaddr, nfds, ep) != 0)) {
            errno = EFAULT;
            ret_i32(cpu, -1);
            return;
        }
        if (tp) {
            irix_guest_write32be(rt, taddr + 0u, (uint32_t)(int32_t)tv.tv_sec);
            irix_guest_write32be(rt, taddr + 4u, (uint32_t)(int32_t)tv.tv_usec);
            if (rt->trapped) {
                ret_i32(cpu, -1);
                return;
            }
        }
    }
    ret_i32(cpu, rc);
}

static void bridge_sginap(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    int32_t ticks = (int32_t)cpu->gpr[4];
    struct timespec req, rem;
    long remaining;
    (void)rt;

    if (ticks <= 0) {
        (void)sched_yield();
        ret_i32(cpu, 0);
        return;
    }

    req.tv_sec = ticks / IRIX_SGINAP_HZ;
    req.tv_nsec = (long)(ticks % IRIX_SGINAP_HZ) *
                  (1000000000L / IRIX_SGINAP_HZ);
    rem.tv_sec = 0;
    rem.tv_nsec = 0;

    if (nanosleep(&req, &rem) == 0) {
        ret_i32(cpu, 0);
        return;
    }

    if (errno == EINTR) {
        remaining = rem.tv_sec * IRIX_SGINAP_HZ;
        remaining += (rem.tv_nsec + (1000000000L / IRIX_SGINAP_HZ) - 1L) /
                     (1000000000L / IRIX_SGINAP_HZ);
        if (remaining < 0) remaining = 0;
        if (remaining > INT32_MAX) remaining = INT32_MAX;
        ret_i32(cpu, (int)remaining);
        return;
    }

    ret_i32(cpu, 0);
}

IrixGuestFunction irix_posix_bridge_lookup(const char *name)
{
    IrixGuestFunction fn = irix_posix_bridge_lookup_base(name);
    if (fn || !name) return fn;
    if (!strcmp(name, "sigemptyset")) return bridge_sigemptyset;
    if (!strcmp(name, "sigfillset")) return bridge_sigfillset;
    if (!strcmp(name, "sigaddset")) return bridge_sigaddset;
    if (!strcmp(name, "sigdelset")) return bridge_sigdelset;
    if (!strcmp(name, "sigismember")) return bridge_sigismember;
    if (!strcmp(name, "select")) return bridge_select;
    if (!strcmp(name, "sginap")) return bridge_sginap;
    return 0;
}
