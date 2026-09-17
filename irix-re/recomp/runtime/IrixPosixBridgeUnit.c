#define irix_posix_bridge_lookup irix_posix_bridge_lookup_base
#include "IrixPosixBridge.c"
#undef irix_posix_bridge_lookup

#include <fcntl.h>
#include <sched.h>
#include <sys/select.h>
#include <sys/time.h>

#define IRIX_FD_SETSIZE 1024
#define IRIX_FDSET_BYTES 128u
#define IRIX_SGINAP_HZ 100L
#define IRIX_SIGSET_WORDS 4u
#define IRIX_SIGSET_BYTES (IRIX_SIGSET_WORDS * 4u)
#define IRIX_SIGACTION_BYTES 24u
#define IRIX_NSIG 128

#define IRIX_F_DUPFD 0
#define IRIX_F_GETFD 1
#define IRIX_F_SETFD 2
#define IRIX_F_GETFL 3
#define IRIX_F_SETFL 4

#define IRIX_FD_CLOEXEC 0x0001u
#define IRIX_O_ACCMODE  0x0003u
#define IRIX_O_APPEND   0x0008u
#define IRIX_O_SYNC     0x0010u
#define IRIX_O_ASYNC    0x0040u
#define IRIX_O_NONBLOCK 0x0080u

typedef struct IrixSigActionState {
    uint32_t handler;
    uint32_t flags;
    uint32_t mask[IRIX_SIGSET_WORDS];
} IrixSigActionState;

static IrixSigActionState g_irix_sigactions[IRIX_NSIG + 1];

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

static int read_irix_sigaction(IrixGuestRuntime *rt, IrixGuestAddr addr,
                               IrixSigActionState *out)
{
    unsigned i;
    if (!addr || !out || !irix_guest_ptr(rt, addr, IRIX_SIGACTION_BYTES))
        return -1;
    out->handler = irix_guest_read32be(rt, addr + 0u);
    out->flags = irix_guest_read32be(rt, addr + 4u);
    for (i = 0; i < IRIX_SIGSET_WORDS; ++i)
        out->mask[i] = irix_guest_read32be(rt, addr + 8u + (IrixGuestAddr)i * 4u);
    return rt->trapped ? -1 : 0;
}

static int write_irix_sigaction(IrixGuestRuntime *rt, IrixGuestAddr addr,
                                const IrixSigActionState *in)
{
    unsigned i;
    if (!addr || !in || !irix_guest_ptr(rt, addr, IRIX_SIGACTION_BYTES))
        return -1;
    irix_guest_write32be(rt, addr + 0u, in->handler);
    irix_guest_write32be(rt, addr + 4u, in->flags);
    for (i = 0; i < IRIX_SIGSET_WORDS; ++i)
        irix_guest_write32be(rt, addr + 8u + (IrixGuestAddr)i * 4u, in->mask[i]);
    return rt->trapped ? -1 : 0;
}

static void bridge_sigaction(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    int signo = (int)(int32_t)cpu->gpr[4];
    IrixGuestAddr act_addr = (uint32_t)cpu->gpr[5];
    IrixGuestAddr old_addr = (uint32_t)cpu->gpr[6];
    IrixSigActionState next;

    if (signo <= 0 || signo > IRIX_NSIG) {
        errno = EINVAL;
        ret_i32(cpu, -1);
        return;
    }
    if (old_addr && write_irix_sigaction(rt, old_addr, &g_irix_sigactions[signo]) != 0) {
        errno = EFAULT;
        ret_i32(cpu, -1);
        return;
    }
    if (act_addr) {
        if (read_irix_sigaction(rt, act_addr, &next) != 0) {
            errno = EFAULT;
            ret_i32(cpu, -1);
            return;
        }
        g_irix_sigactions[signo] = next;
    }
    ret_i32(cpu, 0);
}

static int host_status_flags_from_irix(uint32_t flags)
{
    int out = 0;

    switch (flags & IRIX_O_ACCMODE) {
    case 1u: out |= O_WRONLY; break;
    case 2u: out |= O_RDWR; break;
    default: out |= O_RDONLY; break;
    }
    if (flags & IRIX_O_APPEND) out |= O_APPEND;
#ifdef O_NONBLOCK
    if (flags & IRIX_O_NONBLOCK) out |= O_NONBLOCK;
#endif
#ifdef O_ASYNC
    if (flags & IRIX_O_ASYNC) out |= O_ASYNC;
#endif
#ifdef O_SYNC
    if (flags & IRIX_O_SYNC) out |= O_SYNC;
#endif
    return out;
}

static uint32_t irix_status_flags_from_host(int flags)
{
    uint32_t out = 0;

    switch (flags & O_ACCMODE) {
    case O_WRONLY: out |= 1u; break;
    case O_RDWR: out |= 2u; break;
    default: break;
    }
    if (flags & O_APPEND) out |= IRIX_O_APPEND;
#ifdef O_NONBLOCK
    if (flags & O_NONBLOCK) out |= IRIX_O_NONBLOCK;
#endif
#ifdef O_ASYNC
    if (flags & O_ASYNC) out |= IRIX_O_ASYNC;
#endif
#ifdef O_SYNC
    if (flags & O_SYNC) out |= IRIX_O_SYNC;
#endif
    return out;
}

static void bridge_fcntl(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    int fd = (int)(int32_t)cpu->gpr[4];
    int cmd = (int)(int32_t)cpu->gpr[5];
    uint32_t arg = (uint32_t)cpu->gpr[6];
    int rc;

    (void)rt;
    errno = 0;

    switch (cmd) {
    case IRIX_F_DUPFD:
        rc = fcntl(fd, F_DUPFD, (int)arg);
        break;
    case IRIX_F_GETFD:
        rc = fcntl(fd, F_GETFD);
        if (rc >= 0)
            rc = (rc & FD_CLOEXEC) ? (int)IRIX_FD_CLOEXEC : 0;
        break;
    case IRIX_F_SETFD:
        rc = fcntl(fd, F_SETFD,
                   (arg & IRIX_FD_CLOEXEC) ? FD_CLOEXEC : 0);
        break;
    case IRIX_F_GETFL:
        rc = fcntl(fd, F_GETFL);
        if (rc >= 0)
            rc = (int)irix_status_flags_from_host(rc);
        break;
    case IRIX_F_SETFL:
        /*
         * IRIX and Linux use different bit values for status flags.
         * In particular IRIX O_NONBLOCK is 0x80 while Linux/x86 uses
         * another value, so raw passthrough is not ABI-correct.
         */
        rc = fcntl(fd, F_SETFL, host_status_flags_from_irix(arg));
        break;
    default:
        errno = EINVAL;
        rc = -1;
        break;
    }

    ret_i32(cpu, rc);
}

static void bridge_sgikopt(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr option_addr = (uint32_t)cpu->gpr[4];
    IrixGuestAddr buf_addr = (uint32_t)cpu->gpr[5];
    int buflen = (int)(int32_t)cpu->gpr[6];
    const char *option = guest_cstr(rt, option_addr);
    char envname[256];
    const char *value;
    char *buf;
    size_t n;

    if (!option || !buf_addr || buflen <= 0) {
        errno = EFAULT;
        ret_i32(cpu, -1);
        return;
    }

    if ((size_t)snprintf(envname, sizeof(envname), "IRIX_KOPT_%s", option) >= sizeof(envname)) {
        errno = EINVAL;
        ret_i32(cpu, -1);
        return;
    }

    value = getenv(envname);
    fprintf(stderr, "IRIX sgikopt: option=%s%s\n",
            option, value ? " (environment override)" : " (unset)");

    if (!value) {
        errno = EINVAL;
        ret_i32(cpu, -1);
        return;
    }

    buf = (char *)irix_guest_ptr(rt, buf_addr, (size_t)buflen);
    if (!buf) {
        errno = EFAULT;
        ret_i32(cpu, -1);
        return;
    }

    n = strlen(value);
    if (n >= (size_t)buflen)
        n = (size_t)buflen - 1u;
    memcpy(buf, value, n);
    buf[n] = '\0';
    ret_i32(cpu, 0);
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
    if (!strcmp(name, "sigaction")) return bridge_sigaction;
    if (!strcmp(name, "fcntl")) return bridge_fcntl;
    if (!strcmp(name, "sgikopt")) return bridge_sgikopt;
    if (!strcmp(name, "select")) return bridge_select;
    if (!strcmp(name, "sginap")) return bridge_sginap;
    return 0;
}
