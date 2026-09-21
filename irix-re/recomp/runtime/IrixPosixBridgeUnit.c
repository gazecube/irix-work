#define irix_posix_bridge_lookup irix_posix_bridge_lookup_base
#include "IrixPosixBridge.c"
#include "IrixCompat.h"
#undef irix_posix_bridge_lookup

#include <fcntl.h>
#include <grp.h>
#include <netdb.h>
#include <pwd.h>
#include <signal.h>
#include <sched.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <utmpx.h>

#define IRIX_FD_SETSIZE 1024
#define IRIX_FDSET_BYTES 128u
#define IRIX_SGINAP_HZ 100L
#define IRIX_SIGSET_WORDS 4u
#define IRIX_SIGSET_BYTES (IRIX_SIGSET_WORDS * 4u)
#define IRIX_SIGACTION_BYTES 32u
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
    uint32_t flags;
    uint32_t handler;
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
    out->flags = irix_guest_read32be(rt, addr + 0u);
    out->handler = irix_guest_read32be(rt, addr + 4u);
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
    irix_guest_write32be(rt, addr + 0u, in->flags);
    irix_guest_write32be(rt, addr + 4u, in->handler);
    for (i = 0; i < IRIX_SIGSET_WORDS; ++i)
        irix_guest_write32be(rt, addr + 8u + (IrixGuestAddr)i * 4u, in->mask[i]);
    irix_guest_write32be(rt, addr + 24u, 0u);
    irix_guest_write32be(rt, addr + 28u, 0u);
    return rt->trapped ? -1 : 0;
}

static sigset_t g_irix_held_signals;
static int g_irix_held_signals_initialized;

static void ensure_irix_held_signals(void)
{
    if (!g_irix_held_signals_initialized) {
        sigemptyset(&g_irix_held_signals);
        g_irix_held_signals_initialized = 1;
    }
}

/*
 * SVR4 sighold()/sigrelse() manipulate the calling process signal mask.
 * Keep a host mask in sync so code using the legacy interfaces gets the
 * expected blocking semantics without exposing host sigset_t to the guest.
 */
static void bridge_sighold(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    int signo = (int)(int32_t)cpu->gpr[4];
    unsigned word;
    uint32_t mask;

    (void)rt;
    if (irix_sig_bit(signo, &word, &mask) != 0) {
        errno = EINVAL;
        ret_i32(cpu, -1);
        return;
    }
    ensure_irix_held_signals();
    if (sigaddset(&g_irix_held_signals, signo) != 0 ||
        sigprocmask(SIG_BLOCK, &g_irix_held_signals, NULL) != 0) {
        ret_i32(cpu, -1);
        return;
    }
    ret_i32(cpu, 0);
}

static void bridge_sigrelse(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    int signo = (int)(int32_t)cpu->gpr[4];
    unsigned word;
    uint32_t mask;
    sigset_t one;

    (void)rt;
    if (irix_sig_bit(signo, &word, &mask) != 0) {
        errno = EINVAL;
        ret_i32(cpu, -1);
        return;
    }
    ensure_irix_held_signals();
    sigemptyset(&one);
    if (sigaddset(&one, signo) != 0 ||
        sigprocmask(SIG_UNBLOCK, &one, NULL) != 0) {
        ret_i32(cpu, -1);
        return;
    }
    sigdelset(&g_irix_held_signals, signo);
    ret_i32(cpu, 0);
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

static void bridge_ioctl(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    int fd = (int)(int32_t)cpu->gpr[4];
    uint32_t cmd = (uint32_t)cpu->gpr[5];
    IrixGuestAddr arg = (uint32_t)cpu->gpr[6];
    void *p;

    /*
     * Direct libc ioctl imports bypass the raw syscall dispatcher.  Marshal
     * the pointer-bearing IRIX tty controls that xwsh uses here; scalar
     * controls can be forwarded unchanged to the IRIX spty module.
     */
    switch (cmd) {
    case 0x80087467u: /* IRIX TIOCSWINSZ */
    case 0x40087468u: /* IRIX TIOCGWINSZ */
        p = arg ? irix_guest_ptr(rt, arg, 8u) : NULL;
        if (!p) {
            errno = EFAULT;
            ret_i32(cpu, -1);
            return;
        }
        ret_i32(cpu, ioctl(fd, (unsigned long)cmd, p));
        return;
    default:
        ret_i32(cpu, ioctl(fd, (unsigned long)cmd,
                           (unsigned long)(uint32_t)arg));
        return;
    }
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

static IrixGuestAddr copy_guest_tmp_string(IrixGuestRuntime *rt, const char *src)
{
    IrixGuestAddr out;
    size_t n;
    void *dst;

    if (!src) return 0;
    n = strlen(src) + 1u;
    if (n > UINT32_MAX) return 0;
    out = tmp_alloc((uint32_t)n);
    if (!out) return 0;
    dst = irix_guest_ptr(rt, out, n);
    if (!dst) return 0;
    memcpy(dst, src, n);
    return out;
}

/* IRIX N32 struct passwd is six 32-bit words:
   name, passwd, uid, gid, gecos, dir, shell. Keep a small persistent guest
   cache because the non-reentrant getpw* interfaces return static storage. */
static IrixGuestAddr copy_guest_passwd_n32(IrixGuestRuntime *rt, const struct passwd *pw)
{
    IrixGuestAddr out, fields[7];
    if (!pw) return 0;

    /* IRIX passwd(4) has two SVR4 fields that Linux lacks:
         name, passwd, uid, gid, age, comment, gecos, dir, shell
       N32 therefore occupies 36 bytes, not the Linux-shaped 28 bytes. */
    out = tmp_alloc(36u);
    fields[0] = copy_guest_tmp_string(rt, pw->pw_name ? pw->pw_name : "");
    fields[1] = copy_guest_tmp_string(rt, pw->pw_passwd ? pw->pw_passwd : "");
    fields[2] = copy_guest_tmp_string(rt, ""); /* pw_age */
    fields[3] = copy_guest_tmp_string(rt, ""); /* pw_comment */
    fields[4] = copy_guest_tmp_string(rt, pw->pw_gecos ? pw->pw_gecos : "");
    fields[5] = copy_guest_tmp_string(rt, pw->pw_dir ? pw->pw_dir : "");
    fields[6] = copy_guest_tmp_string(rt, pw->pw_shell ? pw->pw_shell : "");
    if (!out || !fields[0] || !fields[1] || !fields[2] || !fields[3] ||
        !fields[4] || !fields[5] || !fields[6]) return 0;
    irix_guest_write32be(rt, out + 0u, fields[0]);
    irix_guest_write32be(rt, out + 4u, fields[1]);
    irix_guest_write32be(rt, out + 8u, (uint32_t)pw->pw_uid);
    irix_guest_write32be(rt, out + 12u, (uint32_t)pw->pw_gid);
    irix_guest_write32be(rt, out + 16u, fields[2]);
    irix_guest_write32be(rt, out + 20u, fields[3]);
    irix_guest_write32be(rt, out + 24u, fields[4]);
    irix_guest_write32be(rt, out + 28u, fields[5]);
    irix_guest_write32be(rt, out + 32u, fields[6]);
    return rt->trapped ? 0 : out;
}

static void bridge_getpwuid(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    uid_t uid = (uid_t)(uint32_t)cpu->gpr[4];
    struct passwd *pw = getpwuid(uid);
    IrixGuestAddr out = copy_guest_passwd_n32(rt, pw);
    fprintf(stderr, "IRIX getpwuid: uid=%lu host=%s guest=0x%08x\n",
            (unsigned long)uid, pw && pw->pw_name ? pw->pw_name : "(null)",
            (unsigned)out);
    cpu->gpr[2] = out;
}

static void bridge_getpwnam(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *name = guest_cstr(rt, (uint32_t)cpu->gpr[4]);
    struct passwd *pw = name ? getpwnam(name) : 0;
    IrixGuestAddr out = copy_guest_passwd_n32(rt, pw);
    fprintf(stderr, "IRIX getpwnam: name=%s host=%s guest=0x%08x\n",
            name ? name : "(null)", pw && pw->pw_name ? pw->pw_name : "(null)",
            (unsigned)out);
    cpu->gpr[2] = out;
}

static int write_guest_passwd_r(IrixGuestRuntime *rt, IrixGuestAddr out,
                                IrixGuestAddr buf, uint32_t buflen,
                                const struct passwd *pw)
{
    const char *src[7];
    IrixGuestAddr ptr[7];
    uint32_t used = 0;
    unsigned i;
    if (!out || !buf || !pw || !irix_guest_ptr(rt, out, 36u) ||
        !irix_guest_ptr(rt, buf, buflen))
        return EFAULT;
    src[0] = pw->pw_name ? pw->pw_name : "";
    src[1] = pw->pw_passwd ? pw->pw_passwd : "";
    src[2] = ""; /* IRIX pw_age */
    src[3] = ""; /* IRIX pw_comment */
    src[4] = pw->pw_gecos ? pw->pw_gecos : "";
    src[5] = pw->pw_dir ? pw->pw_dir : "";
    src[6] = pw->pw_shell ? pw->pw_shell : "";
    for (i = 0; i < 7u; ++i) {
        size_t n = strlen(src[i]) + 1u;
        void *dst;
        if (n > UINT32_MAX || used > buflen || n > (size_t)(buflen - used))
            return ERANGE;
        ptr[i] = buf + used;
        dst = irix_guest_ptr(rt, ptr[i], n);
        if (!dst) return EFAULT;
        memcpy(dst, src[i], n);
        used += (uint32_t)n;
    }
    irix_guest_write32be(rt, out + 0u, ptr[0]);
    irix_guest_write32be(rt, out + 4u, ptr[1]);
    irix_guest_write32be(rt, out + 8u, (uint32_t)pw->pw_uid);
    irix_guest_write32be(rt, out + 12u, (uint32_t)pw->pw_gid);
    irix_guest_write32be(rt, out + 16u, ptr[2]);
    irix_guest_write32be(rt, out + 20u, ptr[3]);
    irix_guest_write32be(rt, out + 24u, ptr[4]);
    irix_guest_write32be(rt, out + 28u, ptr[5]);
    irix_guest_write32be(rt, out + 32u, ptr[6]);
    return rt->trapped ? EFAULT : 0;
}

static void bridge_getpwuid_r(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    uid_t uid = (uid_t)(uint32_t)cpu->gpr[4];
    IrixGuestAddr pwd = (uint32_t)cpu->gpr[5];
    IrixGuestAddr buf = (uint32_t)cpu->gpr[6];
    uint32_t buflen = (uint32_t)cpu->gpr[7];
    IrixGuestAddr resultp = (uint32_t)cpu->gpr[8];
    struct passwd *pw = getpwuid(uid);
    int rc = 0;
    if (!resultp || !irix_guest_ptr(rt, resultp, 4u)) rc = EFAULT;
    else if (!pw) irix_guest_write32be(rt, resultp, 0u);
    else {
        rc = write_guest_passwd_r(rt, pwd, buf, buflen, pw);
        irix_guest_write32be(rt, resultp, rc ? 0u : pwd);
    }
    fprintf(stderr, "IRIX getpwuid_r: uid=%lu pwd=%08x buf=%08x len=%u resultp=%08x host=%s rc=%d\n",
            (unsigned long)uid, (unsigned)pwd, (unsigned)buf, buflen,
            (unsigned)resultp, pw && pw->pw_name ? pw->pw_name : "(null)", rc);
    ret_i32(cpu, rc);
}

static void bridge_getpwnam_r(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *name = guest_cstr(rt, (uint32_t)cpu->gpr[4]);
    IrixGuestAddr pwd = (uint32_t)cpu->gpr[5];
    IrixGuestAddr buf = (uint32_t)cpu->gpr[6];
    uint32_t buflen = (uint32_t)cpu->gpr[7];
    IrixGuestAddr resultp = (uint32_t)cpu->gpr[8];
    struct passwd *pw = name ? getpwnam(name) : 0;
    int rc = 0;
    if (!resultp || !irix_guest_ptr(rt, resultp, 4u)) rc = EFAULT;
    else if (!pw) irix_guest_write32be(rt, resultp, 0u);
    else {
        rc = write_guest_passwd_r(rt, pwd, buf, buflen, pw);
        irix_guest_write32be(rt, resultp, rc ? 0u : pwd);
    }
    fprintf(stderr, "IRIX getpwnam_r: name=%s pwd=%08x buf=%08x len=%u resultp=%08x host=%s rc=%d\n",
            name ? name : "(null)", (unsigned)pwd, (unsigned)buf, buflen,
            (unsigned)resultp, pw && pw->pw_name ? pw->pw_name : "(null)", rc);
    ret_i32(cpu, rc);
}

static void bridge_getuid(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)rt;
    cpu->gpr[2] = (uint32_t)getuid();
}

static void bridge_geteuid(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)rt;
    cpu->gpr[2] = (uint32_t)geteuid();
}

static void bridge_getgid(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)rt;
    cpu->gpr[2] = (uint32_t)getgid();
}

static void bridge_getegid(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)rt;
    cpu->gpr[2] = (uint32_t)getegid();
}

static void bridge_initgroups(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *name = guest_cstr(rt, (uint32_t)cpu->gpr[4]);
    gid_t gid = (gid_t)(uint32_t)cpu->gpr[5];
    (void)rt;
    ret_i32(cpu, name ? initgroups(name, gid) : (errno = EFAULT, -1));
}

static void bridge_gethostbyname(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *name = guest_cstr(rt, (uint32_t)cpu->gpr[4]);
    struct hostent *host;
    IrixGuestAddr guest_host, guest_name, guest_aliases, guest_addrs;
    size_t alias_count = 0, addr_count = 0, i;

    cpu->gpr[2] = 0;
    if (!name) return;

    host = gethostbyname(name);
    if (!host) return;

    while (host->h_aliases && host->h_aliases[alias_count])
        ++alias_count;
    while (host->h_addr_list && host->h_addr_list[addr_count])
        ++addr_count;

    guest_host = tmp_alloc(20u);
    guest_aliases = tmp_alloc((uint32_t)((alias_count + 1u) * 4u));
    guest_addrs = tmp_alloc((uint32_t)((addr_count + 1u) * 4u));
    guest_name = copy_guest_tmp_string(rt, host->h_name ? host->h_name : name);
    if (!guest_host || !guest_aliases || !guest_addrs || !guest_name)
        return;

    for (i = 0; i < alias_count; ++i) {
        IrixGuestAddr a = copy_guest_tmp_string(rt, host->h_aliases[i]);
        if (!a) return;
        irix_guest_write32be(rt, guest_aliases + (IrixGuestAddr)i * 4u, a);
    }
    irix_guest_write32be(rt, guest_aliases + (IrixGuestAddr)alias_count * 4u, 0u);

    for (i = 0; i < addr_count; ++i) {
        IrixGuestAddr a;
        void *dst;
        if (host->h_length <= 0) return;
        a = tmp_alloc((uint32_t)host->h_length);
        if (!a) return;
        dst = irix_guest_ptr(rt, a, (size_t)host->h_length);
        if (!dst) return;
        memcpy(dst, host->h_addr_list[i], (size_t)host->h_length);
        irix_guest_write32be(rt, guest_addrs + (IrixGuestAddr)i * 4u, a);
    }
    irix_guest_write32be(rt, guest_addrs + (IrixGuestAddr)addr_count * 4u, 0u);

    irix_guest_write32be(rt, guest_host + 0u, guest_name);
    irix_guest_write32be(rt, guest_host + 4u, guest_aliases);
    irix_guest_write32be(rt, guest_host + 8u, (uint32_t)host->h_addrtype);
    irix_guest_write32be(rt, guest_host + 12u, (uint32_t)host->h_length);
    irix_guest_write32be(rt, guest_host + 16u, guest_addrs);

    if (!rt->trapped)
        cpu->gpr[2] = guest_host;
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

static void bridge_realpath(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr path_addr = (uint32_t)cpu->gpr[4];
    IrixGuestAddr out_addr = (uint32_t)cpu->gpr[5];
    const char *path = guest_cstr(rt, path_addr);
    char resolved[PATH_MAX];
    char *dst;
    size_t n;

    cpu->gpr[2] = 0;
    if (!path || !out_addr) {
        errno = EFAULT;
        return;
    }

    if (!realpath(path, resolved))
        return;

    n = strlen(resolved) + 1u;
    dst = (char *)irix_guest_ptr(rt, out_addr, n);
    if (!dst) {
        errno = EFAULT;
        return;
    }
    memcpy(dst, resolved, n);
    cpu->gpr[2] = out_addr;
}

static void bridge_execl(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *path = guest_cstr(rt, (uint32_t)cpu->gpr[4]);
    const char *arg0 = guest_cstr(rt, (uint32_t)cpu->gpr[5]);
    const char *arg1 = guest_cstr(rt, (uint32_t)cpu->gpr[6]);

    /*
     * Guest IRIX executables cannot be handed to the Linux kernel.
     * Until guest exec replacement is wired to translated app bundles,
     * fail exactly as an exec of an unsupported binary would.
     */
    fprintf(stderr, "IRIX execl: guest exec unavailable: path=%s arg0=%s arg1=%s\n",
            path ? path : "(invalid)",
            arg0 ? arg0 : "(null)",
            arg1 ? arg1 : "(null)");
    errno = path ? ENOEXEC : EFAULT;
    ret_i32(cpu, -1);
}

static void bridge_exit(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    int status = (int)(int32_t)cpu->gpr[4];
    (void)rt;
    exit(status);
}

static int host_socket_domain_from_irix(int domain)
{
    switch (domain) {
    case 1: return AF_UNIX;
#ifdef AF_INET
    case 2: return AF_INET;
#endif
    default: return -1;
    }
}

static int host_socket_type_from_irix(int type)
{
    switch (type) {
    case 1: return SOCK_STREAM;
    case 2: return SOCK_DGRAM;
#ifdef SOCK_RAW
    case 3: return SOCK_RAW;
#endif
#ifdef SOCK_RDM
    case 5: return SOCK_RDM;
#endif
#ifdef SOCK_SEQPACKET
    case 6: return SOCK_SEQPACKET;
#endif
    default: return -1;
    }
}

static void bridge_socket(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    int irix_domain = (int)(int32_t)cpu->gpr[4];
    int irix_type = (int)(int32_t)cpu->gpr[5];
    int protocol = (int)(int32_t)cpu->gpr[6];
    int domain = host_socket_domain_from_irix(irix_domain);
    int type = host_socket_type_from_irix(irix_type);
    int rc;

    (void)rt;
    if (domain < 0 || type < 0) {
        errno = EAFNOSUPPORT;
        ret_i32(cpu, -1);
        return;
    }

    rc = socket(domain, type, protocol);
    ret_i32(cpu, rc);
}

static void bridge_connect(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    int fd = (int)(int32_t)cpu->gpr[4];
    IrixGuestAddr addr = (uint32_t)cpu->gpr[5];
    uint32_t len = (uint32_t)cpu->gpr[6];
    uint16_t family;

    if (!addr || len < 2u || !irix_guest_ptr(rt, addr, len)) {
        errno = EFAULT;
        ret_i32(cpu, -1);
        return;
    }

    family = irix_guest_read16be(rt, addr);
    if (rt->trapped) {
        ret_i32(cpu, -1);
        return;
    }

    if (family == 1u) {
        struct sockaddr_un sun;
        uint32_t path_len;

        memset(&sun, 0, sizeof(sun));
        sun.sun_family = AF_UNIX;

        path_len = len > 2u ? len - 2u : 0u;
        if (path_len > 108u)
            path_len = 108u;
        if (path_len >= sizeof(sun.sun_path))
            path_len = sizeof(sun.sun_path) - 1u;

        if (path_len) {
            const char *src = (const char *)irix_guest_ptr(rt, addr + 2u, path_len);
            if (!src) {
                errno = EFAULT;
                ret_i32(cpu, -1);
                return;
            }
            memcpy(sun.sun_path, src, path_len);
            sun.sun_path[sizeof(sun.sun_path) - 1u] = '\0';
        }

        ret_i32(cpu, connect(fd, (struct sockaddr *)&sun, sizeof(sun)));
        return;
    }

    errno = EAFNOSUPPORT;
    ret_i32(cpu, -1);
}

static void bridge_fork(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    pid_t pid;
    (void)rt;

    pid = fork();
    ret_i32(cpu, pid < 0 ? -1 : (int)pid);
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

#define IRIX_UTMPX_SIZE 372u
static IrixGuestAddr g_irix_utmpx_result;

static int read_irix_utmpx(IrixGuestRuntime *rt, IrixGuestAddr addr, struct utmpx *u)
{
    if (!addr || !u || !irix_guest_ptr(rt, addr, IRIX_UTMPX_SIZE))
        return -1;
    memset(u, 0, sizeof(*u));
    memcpy(u->ut_user, irix_guest_ptr(rt, addr + 0u, 32u), 32u);
    memcpy(u->ut_id, irix_guest_ptr(rt, addr + 32u, 4u), 4u);
    memcpy(u->ut_line, irix_guest_ptr(rt, addr + 36u, 32u), 32u);
    u->ut_pid = (pid_t)(int32_t)irix_guest_read32be(rt, addr + 68u);
    u->ut_type = (short)(int16_t)irix_guest_read16be(rt, addr + 72u);
    u->ut_exit.__e_termination = (short)(int16_t)irix_guest_read16be(rt, addr + 74u);
    u->ut_exit.__e_exit = (short)(int16_t)irix_guest_read16be(rt, addr + 76u);
    u->ut_tv.tv_sec = (int32_t)irix_guest_read32be(rt, addr + 80u);
    u->ut_tv.tv_usec = (int32_t)irix_guest_read32be(rt, addr + 84u);
#ifdef __linux__
    u->ut_session = (int32_t)irix_guest_read32be(rt, addr + 88u);
#endif
    memcpy(u->ut_host, irix_guest_ptr(rt, addr + 114u, 257u),
           sizeof(u->ut_host) < 257u ? sizeof(u->ut_host) : 257u);
    return rt->trapped ? -1 : 0;
}

static int write_irix_utmpx(IrixGuestRuntime *rt, IrixGuestAddr addr, const struct utmpx *u)
{
    size_t hostlen;
    if (!addr || !u || !irix_guest_ptr(rt, addr, IRIX_UTMPX_SIZE))
        return -1;
    memset(irix_guest_ptr(rt, addr, IRIX_UTMPX_SIZE), 0, IRIX_UTMPX_SIZE);
    memcpy(irix_guest_ptr(rt, addr + 0u, 32u), u->ut_user, 32u);
    memcpy(irix_guest_ptr(rt, addr + 32u, 4u), u->ut_id, 4u);
    memcpy(irix_guest_ptr(rt, addr + 36u, 32u), u->ut_line, 32u);
    irix_guest_write32be(rt, addr + 68u, (uint32_t)(int32_t)u->ut_pid);
    irix_guest_write16be(rt, addr + 72u, (uint16_t)u->ut_type);
    irix_guest_write16be(rt, addr + 74u, (uint16_t)u->ut_exit.__e_termination);
    irix_guest_write16be(rt, addr + 76u, (uint16_t)u->ut_exit.__e_exit);
    irix_guest_write32be(rt, addr + 80u, (uint32_t)(int32_t)u->ut_tv.tv_sec);
    irix_guest_write32be(rt, addr + 84u, (uint32_t)(int32_t)u->ut_tv.tv_usec);
#ifdef __linux__
    irix_guest_write32be(rt, addr + 88u, (uint32_t)(int32_t)u->ut_session);
#endif
    hostlen = sizeof(u->ut_host);
    if (hostlen > 257u) hostlen = 257u;
    memcpy(irix_guest_ptr(rt, addr + 114u, hostlen), u->ut_host, hostlen);
    irix_guest_write16be(rt, addr + 112u,
        (uint16_t)(strnlen(u->ut_host, hostlen) + (hostlen ? 1u : 0u)));
    return rt->trapped ? -1 : 0;
}

static IrixGuestAddr cache_irix_utmpx(IrixGuestRuntime *rt, const struct utmpx *u)
{
    if (!u) return 0;
    if (!g_irix_utmpx_result)
        g_irix_utmpx_result = tmp_alloc(IRIX_UTMPX_SIZE);
    if (!g_irix_utmpx_result ||
        write_irix_utmpx(rt, g_irix_utmpx_result, u) != 0)
        return 0;
    return g_irix_utmpx_result;
}

static void bridge_setutxent(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)rt; setutxent(); ret_i32(cpu, 0);
}

static void bridge_endutxent(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)rt; endutxent(); ret_i32(cpu, 0);
}

static void bridge_getutxent(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    cpu->gpr[2] = cache_irix_utmpx(rt, getutxent());
}

static void bridge_getutxid(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    struct utmpx key;
    if (read_irix_utmpx(rt, (uint32_t)cpu->gpr[4], &key) != 0) {
        errno = EFAULT; cpu->gpr[2] = 0; return;
    }
    cpu->gpr[2] = cache_irix_utmpx(rt, getutxid(&key));
}

static void bridge_getutxline(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    struct utmpx key;
    if (read_irix_utmpx(rt, (uint32_t)cpu->gpr[4], &key) != 0) {
        errno = EFAULT; cpu->gpr[2] = 0; return;
    }
    cpu->gpr[2] = cache_irix_utmpx(rt, getutxline(&key));
}

static void bridge_pututxline(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    struct utmpx ent;
    if (read_irix_utmpx(rt, (uint32_t)cpu->gpr[4], &ent) != 0) {
        errno = EFAULT; cpu->gpr[2] = 0; return;
    }
    cpu->gpr[2] = cache_irix_utmpx(rt, pututxline(&ent));
}

static void bridge_utmpxname(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *name = guest_cstr(rt, (uint32_t)cpu->gpr[4]);
    if (!name) { errno = EFAULT; ret_i32(cpu, 0); return; }
    ret_i32(cpu, utmpxname(name));
}

static void bridge_updwtmpx(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *path = guest_cstr(rt, (uint32_t)cpu->gpr[4]);
    struct utmpx ent;

    if (!path || read_irix_utmpx(rt, (uint32_t)cpu->gpr[5], &ent) != 0) {
        errno = EFAULT;
        ret_i32(cpu, -1);
        return;
    }

    /*
     * updwtmpx() is a void interface on glibc/SVR4.  The guest pathname is
     * intentionally honored here; xwsh uses /var/adm/wtmpx just as IRIX
     * does, so any filesystem namespace redirection belongs at the path
     * personality layer rather than in the utmpx ABI translator.
     */
    updwtmpx(path, &ent);
    ret_i32(cpu, 0);
}

static void bridge_putenv(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    char *s = (char *)irix_guest_ptr(rt, (uint32_t)cpu->gpr[4], 1u);
    (void)rt;
    ret_i32(cpu, s ? putenv(s) : (errno = EFAULT, -1));
}

static void bridge_alarm(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)rt;
    cpu->gpr[2] = (uint32_t)alarm((unsigned)(uint32_t)cpu->gpr[4]);
}

static void bridge_kill(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)rt;
    ret_i32(cpu, kill((pid_t)(int32_t)cpu->gpr[4], (int)(int32_t)cpu->gpr[5]));
}

static void bridge_setsid(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)rt;
    ret_i32(cpu, (int)setsid());
}

static void bridge_gethostid(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)rt;
    cpu->gpr[2] = (uint32_t)gethostid();
}

static void bridge_system(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *cmd = guest_cstr(rt, (uint32_t)cpu->gpr[4]);
    ret_i32(cpu, system(cmd));
}

static void bridge_ffs(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    uint32_t v = (uint32_t)cpu->gpr[4];
    int bit = 0;
    (void)rt;
    while (v) { ++bit; if (v & 1u) break; v >>= 1; }
    ret_i32(cpu, bit);
}

static void bridge_mprotect_compat(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    irix_compat_mprotect(rt, cpu);
}

static void bridge_prctl_compat(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    irix_compat_prctl(rt, cpu);
}

static void bridge_syssgi_compat(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    irix_compat_syssgi(rt, cpu);
}

static void bridge_sysmp_compat(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    irix_compat_sysmp(rt, cpu);
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
    if (!strcmp(name, "sighold")) return bridge_sighold;
    if (!strcmp(name, "sigrelse")) return bridge_sigrelse;
    if (!strcmp(name, "ioctl")) return bridge_ioctl;
    if (!strcmp(name, "fcntl")) return bridge_fcntl;
    if (!strcmp(name, "sgikopt")) return bridge_sgikopt;
    if (!strcmp(name, "gethostbyname")) return bridge_gethostbyname;
    if (!strcmp(name, "getpwuid")) return bridge_getpwuid;
    if (!strcmp(name, "getpwnam")) return bridge_getpwnam;
    if (!strcmp(name, "getpwuid_r")) return bridge_getpwuid_r;
    if (!strcmp(name, "getpwnam_r")) return bridge_getpwnam_r;
    if (!strcmp(name, "getuid")) return bridge_getuid;
    if (!strcmp(name, "geteuid")) return bridge_geteuid;
    if (!strcmp(name, "getgid")) return bridge_getgid;
    if (!strcmp(name, "getegid")) return bridge_getegid;
    if (!strcmp(name, "initgroups")) return bridge_initgroups;
    if (!strcmp(name, "realpath")) return bridge_realpath;
    if (!strcmp(name, "execl")) return bridge_execl;
    if (!strcmp(name, "exit")) return bridge_exit;
    if (!strcmp(name, "socket")) return bridge_socket;
    if (!strcmp(name, "connect")) return bridge_connect;
    if (!strcmp(name, "fork")) return bridge_fork;
    if (!strcmp(name, "select")) return bridge_select;
    if (!strcmp(name, "sginap")) return bridge_sginap;
    if (!strcmp(name, "setutxent")) return bridge_setutxent;
    if (!strcmp(name, "endutxent")) return bridge_endutxent;
    if (!strcmp(name, "getutxent")) return bridge_getutxent;
    if (!strcmp(name, "getutxid")) return bridge_getutxid;
    if (!strcmp(name, "getutxline")) return bridge_getutxline;
    if (!strcmp(name, "pututxline")) return bridge_pututxline;
    if (!strcmp(name, "utmpxname")) return bridge_utmpxname;
    if (!strcmp(name, "updwtmpx")) return bridge_updwtmpx;
    if (!strcmp(name, "putenv")) return bridge_putenv;
    if (!strcmp(name, "alarm")) return bridge_alarm;
    if (!strcmp(name, "kill")) return bridge_kill;
    if (!strcmp(name, "setsid")) return bridge_setsid;
    if (!strcmp(name, "gethostid")) return bridge_gethostid;
    if (!strcmp(name, "system")) return bridge_system;
    if (!strcmp(name, "ffs")) return bridge_ffs;
    if (!strcmp(name, "mprotect")) return bridge_mprotect_compat;
    if (!strcmp(name, "prctl")) return bridge_prctl_compat;
    if (!strcmp(name, "syssgi")) return bridge_syssgi_compat;
    if (!strcmp(name, "sysmp")) return bridge_sysmp_compat;
    return 0;
}
