#define irix_posix_bridge_lookup irix_posix_bridge_lookup_base
#include "IrixPosixBridge.c"
#undef irix_posix_bridge_lookup

#include <sched.h>
#include <sys/select.h>
#include <sys/time.h>

#define IRIX_FD_SETSIZE 1024
#define IRIX_FDSET_BYTES 128u
#define IRIX_SGINAP_HZ 100L

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
    if (!strcmp(name, "select")) return bridge_select;
    if (!strcmp(name, "sginap")) return bridge_sginap;
    return 0;
}
