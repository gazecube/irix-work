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
#include <sys/times.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <utmpx.h>

#define IRIX_FD_SETSIZE 1024
#define IRIX_FDSET_BYTES 128u
#define IRIX_SGINAP_HZ 100L
#define IRIX_SIGSET_WORDS 4u
#define IRIX_SIGSET_BYTES (IRIX_SIGSET_WORDS * 4u)
#define IRIX_SIGACTION_BYTES 32u
int irix_signal_install(int irix_sig, uint32_t handler);
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
        (void)irix_signal_install(signo, next.handler);
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
    case 14: case 6: case 7:        /* F_GETLK, F_SETLK, F_SETLKW */
    case 33: case 34: case 35: {    /* F_GETLK64, F_SETLK64, F_SETLKW64 */
        /* N32 struct flock: short type, short whence, off_t start @8,
         * off_t len @16, long sysid @24, pid_t pid @28.  SVR4 lock types
         * RDLCK=1 WRLCK=2 UNLCK=3 map onto Linux 0/1/2. */
        struct flock fl;
        int op = (cmd == 14 || cmd == 33) ? F_GETLK
               : (cmd == 6 || cmd == 34) ? F_SETLK : F_SETLKW;
        uint16_t t = irix_guest_read16be(rt, arg);
        memset(&fl, 0, sizeof(fl));
        fl.l_type = t == 1 ? F_RDLCK : t == 2 ? F_WRLCK : F_UNLCK;
        fl.l_whence = (short)irix_guest_read16be(rt, arg + 2u);
        fl.l_start = (off_t)(int64_t)irix_guest_read64be(rt, arg + 8u);
        fl.l_len = (off_t)(int64_t)irix_guest_read64be(rt, arg + 16u);
        rc = fcntl(fd, op, &fl);
        if (rc == 0 && op == F_GETLK) {
            irix_guest_write16be(rt, arg, fl.l_type == F_RDLCK ? 1u
                                 : fl.l_type == F_WRLCK ? 2u : 3u);
            irix_guest_write64be(rt, arg + 8u, (uint64_t)fl.l_start);
            irix_guest_write64be(rt, arg + 16u, (uint64_t)fl.l_len);
            irix_guest_write32be(rt, arg + 28u, (uint32_t)fl.l_pid);
        }
        break;
    }
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
    cpu->gpr[2] = out;
}

/* Account enumeration for the graphical login (clogin lists users). */
static void bridge_setpwent(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)rt;
    setpwent();
    cpu->gpr[2] = 0;
}

static void bridge_endpwent(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)rt;
    endpwent();
    cpu->gpr[2] = 0;
}

static void bridge_getpwent(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    struct passwd *pw = getpwent();
    cpu->gpr[2] = copy_guest_passwd_n32(rt, pw);
}

static void bridge_getpwnam(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *name = guest_cstr(rt, (uint32_t)cpu->gpr[4]);
    struct passwd *pw = name ? getpwnam(name) : 0;
    IrixGuestAddr out = copy_guest_passwd_n32(rt, pw);
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

/* N32 passes the first eight argument slots in a0-a7 and later slots in
   eight-byte stack slots at the caller's sp.  Keep the variadic execle
   decoder in guest address space until each argument has been validated. */
static uint32_t guest_n32_arg32(IrixGuestRuntime *rt, IrixGuestCpu *cpu,
                                unsigned index)
{
    IrixGuestAddr sp;
    IrixGuestAddr slot;
    uint64_t value;

    if (index < 8u)
        return (uint32_t)cpu->gpr[4u + index];

    sp = (IrixGuestAddr)(uint32_t)cpu->gpr[29];
    slot = sp + (IrixGuestAddr)(index - 8u) * 8u;
    if (slot < sp || !irix_guest_ptr(rt, slot, 8u))
        return 0u;
    value = irix_guest_read64be(rt, slot);
    return (uint32_t)value;
}

static char **guest_exec_string_vector(IrixGuestRuntime *rt,
                                       IrixGuestAddr vector_addr)
{
    size_t count = 0u, i;
    char **vector;

    if (!vector_addr) {
        vector = (char **)calloc(1u, sizeof(*vector));
        if (!vector) errno = ENOMEM;
        return vector;
    }

    for (;;) {
        IrixGuestAddr slot = vector_addr + (IrixGuestAddr)(count * 4u);
        uint32_t string_addr;
        if (slot < vector_addr || !irix_guest_ptr(rt, slot, 4u)) {
            errno = EFAULT;
            return NULL;
        }
        string_addr = irix_guest_read32be(rt, slot);
        if (rt->trapped) {
            errno = EFAULT;
            return NULL;
        }
        if (!string_addr) break;
        if (!guest_cstr(rt, string_addr)) {
            errno = EFAULT;
            return NULL;
        }
        if (++count >= 4096u) {
            errno = E2BIG;
            return NULL;
        }
    }

    vector = (char **)calloc(count + 1u, sizeof(*vector));
    if (!vector) {
        errno = ENOMEM;
        return NULL;
    }
    for (i = 0; i < count; ++i) {
        IrixGuestAddr string_addr = irix_guest_read32be(
            rt, vector_addr + (IrixGuestAddr)(i * 4u));
        vector[i] = (char *)guest_cstr(rt, string_addr);
        if (!vector[i]) {
            free(vector);
            errno = EFAULT;
            return NULL;
        }
    }
    return vector;
}

static void bridge_execvp(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *file = guest_cstr(rt, (uint32_t)cpu->gpr[4]);
    IrixGuestAddr argv_addr = (uint32_t)cpu->gpr[5];
    char **argv = NULL;
    size_t argc = 0, i;
    int saved_errno;

    if (!file || !argv_addr) {
        errno = EFAULT;
        ret_i32(cpu, -1);
        return;
    }

    /*
     * IRIX N32 argv is an array of big-endian 32-bit guest pointers.
     * This call is reached in xwsh's post-fork child after the PTY has been
     * installed on stdio, so a successful host exec preserves exactly that
     * fd/session state.  This is the process-boundary implementation until
     * exec replacement can load another translated IRIX image in-process.
     */
    for (;;) {
        IrixGuestAddr slot = argv_addr + (IrixGuestAddr)(argc * 4u);
        uint32_t argp;
        if (!irix_guest_ptr(rt, slot, 4u)) {
            errno = EFAULT;
            ret_i32(cpu, -1);
            return;
        }
        argp = irix_guest_read32be(rt, slot);
        if (rt->trapped) {
            errno = EFAULT;
            ret_i32(cpu, -1);
            return;
        }
        if (!argp) break;
        if (!guest_cstr(rt, argp)) {
            errno = EFAULT;
            ret_i32(cpu, -1);
            return;
        }
        if (++argc >= 4096u) {
            errno = E2BIG;
            ret_i32(cpu, -1);
            return;
        }
    }

    argv = (char **)calloc(argc + 1u, sizeof(*argv));
    if (!argv) {
        ret_i32(cpu, -1);
        return;
    }
    for (i = 0; i < argc; ++i) {
        uint32_t argp = irix_guest_read32be(
            rt, argv_addr + (IrixGuestAddr)(i * 4u));
        argv[i] = (char *)guest_cstr(rt, argp);
    }


    if (file[0] == '/')
        irix_guest_execve(rt, file, argv, NULL);
    else
        execvp(file, argv);


    saved_errno = errno;
    free(argv);
    errno = saved_errno;
    ret_i32(cpu, -1);
}

static void bridge_execl(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *path = guest_cstr(rt, (uint32_t)cpu->gpr[4]);
    char **argv = calloc(4097u, sizeof(*argv));
    unsigned index;
    int saved_errno;
    if (!path || !argv) {
        free(argv);
        errno = path ? ENOMEM : EFAULT;
        ret_i32(cpu, -1);
        return;
    }
    for (index = 1u; index < 4096u; ++index) {
        uint32_t arg = guest_n32_arg32(rt, cpu, index);
        if (rt->trapped) { errno = EFAULT; goto failed; }
        if (!arg) break;
        argv[index - 1u] = (char *)guest_cstr(rt, arg);
        if (!argv[index - 1u]) { errno = EFAULT; goto failed; }
    }
    if (index == 4096u) { errno = E2BIG; goto failed; }
    irix_guest_execve(rt, path, argv, NULL);
failed:
    saved_errno = errno;
    free(argv);
    errno = saved_errno;
    ret_i32(cpu, -1);
}

static void bridge_execle(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *path = guest_cstr(rt, (uint32_t)cpu->gpr[4]);
    char **argv = NULL;
    char **envp = NULL;
    unsigned index;
    size_t argc = 0u;
    uint32_t value;
    int saved_errno;

    if (!path) {
        errno = EFAULT;
        ret_i32(cpu, -1);
        return;
    }

    argv = (char **)calloc(4097u, sizeof(*argv));
    if (!argv) {
        errno = ENOMEM;
        ret_i32(cpu, -1);
        return;
    }

    /* execle(path, arg0, ..., NULL, envp): the environment pointer occupies
       the argument slot immediately after the terminating NULL. */
    for (index = 1u; index < 4096u; ++index) {
        value = guest_n32_arg32(rt, cpu, index);
        if (rt->trapped) {
            errno = EFAULT;
            goto execle_failed;
        }
        if (!value) {
            uint32_t envp_addr = guest_n32_arg32(rt, cpu, index + 1u);
            if (rt->trapped) {
                errno = EFAULT;
                goto execle_failed;
            }
            envp = guest_exec_string_vector(rt, envp_addr);
            if (!envp) goto execle_failed;
            break;
        }
        argv[argc] = (char *)guest_cstr(rt, value);
        if (!argv[argc]) {
            errno = EFAULT;
            goto execle_failed;
        }
        if (++argc >= 4096u) {
            errno = E2BIG;
            goto execle_failed;
        }
    }
    if (!envp) {
        errno = E2BIG;
        goto execle_failed;
    }

    irix_guest_execve(rt, path, argv, envp);

execle_failed:
    saved_errno = errno;
    free(envp);
    free(argv);
    errno = saved_errno;
    ret_i32(cpu, -1);
}

static void bridge_exit(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    uint32_t status = (uint32_t)cpu->gpr[4] & 0xffu;

    /* Match the SYS_exit path so AppRunner reports the guest exit cleanly. */
    cpu->gpr[2] = status;
    cpu->gpr[7] = 0;
    rt->fault_addr = status;
    irix_guest_trap(rt, cpu, 0xeeee23ffu);
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
    case 1: return SOCK_DGRAM;
    case 2: return SOCK_STREAM;
#ifdef SOCK_RAW
    case 4: return SOCK_RAW;
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

static void bridge_socketpair(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    int irix_domain = (int)(int32_t)cpu->gpr[4];
    int irix_type = (int)(int32_t)cpu->gpr[5];
    int protocol = (int)(int32_t)cpu->gpr[6];
    IrixGuestAddr guest_fds = (uint32_t)cpu->gpr[7];
    int fds[2];
    int domain = host_socket_domain_from_irix(irix_domain);
    int type = host_socket_type_from_irix(irix_type);
    int rc;

    if (domain < 0 || type < 0) {
        errno = EAFNOSUPPORT;
        ret_i32(cpu, -1);
        return;
    }
    if (!guest_fds || !irix_guest_ptr(rt, guest_fds, 8u)) {
        errno = EFAULT;
        ret_i32(cpu, -1);
        return;
    }

    rc = socketpair(domain, type, protocol, fds);
    if (rc == 0) {
        irix_guest_write32be(rt, guest_fds, (uint32_t)fds[0]);
        irix_guest_write32be(rt, guest_fds + 4u, (uint32_t)fds[1]);
        if (rt->trapped) {
            close(fds[0]);
            close(fds[1]);
            ret_i32(cpu, -1);
            return;
        }
    }
    ret_i32(cpu, rc);
}

static char g_irix_dlerror_message[512];

static void bridge_dlopen(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr guest_path = (uint32_t)cpu->gpr[4];
    const char *path = guest_path ? irix_guest_cstr(rt, guest_path, 4096u) : NULL;

    /* IRIX DSOs are N32 guest images and cannot be handed to the host loader.
       Components in the active application bundle are already registered by
       the runner; an unbundled runtime load must fail as dlopen normally does
       for an unavailable object, rather than trapping the entire guest. */
    snprintf(g_irix_dlerror_message, sizeof(g_irix_dlerror_message),
             "IRIX N32 dynamic loading is unavailable for %s",
             path ? path : "<invalid path>");
    
    ret_i32(cpu, 0);
}

static void bridge_dlclose(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)rt;
    (void)cpu;
    snprintf(g_irix_dlerror_message, sizeof(g_irix_dlerror_message),
             "dlclose called for an unavailable guest DSO");
    ret_i32(cpu, -1);
}

static void bridge_dlerror(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr message = copy_guest_tmp_string(rt, g_irix_dlerror_message);
    g_irix_dlerror_message[0] = '\0';
    ret_i32(cpu, (int32_t)message);
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

        {
            int rc = connect(fd, (struct sockaddr *)&sun, sizeof(sun));
            ret_i32(cpu, rc);
        }
        return;
    }

    if (family == 2u) {
        struct sockaddr_in sin;
        uint32_t address;
        uint16_t port;

        if (len < 16u) {
            errno = EINVAL;
            ret_i32(cpu, -1);
            return;
        }
        port = irix_guest_read16be(rt, addr + 2u);
        address = irix_guest_read32be(rt, addr + 4u);
        if (rt->trapped) {
            ret_i32(cpu, -1);
            return;
        }
        memset(&sin, 0, sizeof(sin));
        sin.sin_family = AF_INET;
        sin.sin_port = htons(port);
        sin.sin_addr.s_addr = htonl(address);
        {
            int rc = connect(fd, (struct sockaddr *)&sin, sizeof(sin));
            ret_i32(cpu, rc);
        }
        return;
    }

    errno = EAFNOSUPPORT;
    fprintf(stderr, "IRIX connect bridge: unsupported family=%u fd=%d len=%u\n",
            family, fd, len);
    ret_i32(cpu, -1);
}

static int irix_unix_sockaddr(IrixGuestRuntime *rt, IrixGuestAddr addr,
                              uint32_t len, struct sockaddr_un *sun)
{
    uint32_t path_len;
    const char *src;

    if (!addr || len < 2u || !irix_guest_ptr(rt, addr, len)) {
        errno = EFAULT;
        return -1;
    }
    if (irix_guest_read16be(rt, addr) != 1u) {
        errno = EAFNOSUPPORT;
        return -1;
    }

    memset(sun, 0, sizeof(*sun));
    sun->sun_family = AF_UNIX;
    path_len = len - 2u;
    if (path_len >= sizeof(sun->sun_path))
        path_len = sizeof(sun->sun_path) - 1u;
    src = path_len ? (const char *)irix_guest_ptr(rt, addr + 2u, path_len) : NULL;
    if (path_len && !src) {
        errno = EFAULT;
        return -1;
    }
    if (path_len)
        memcpy(sun->sun_path, src, path_len);
    sun->sun_path[sizeof(sun->sun_path) - 1u] = '\0';
    return 0;
}

static void bridge_bind(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    int fd = (int)(int32_t)cpu->gpr[4];
    IrixGuestAddr addr = (uint32_t)cpu->gpr[5];
    uint32_t len = (uint32_t)cpu->gpr[6];
    struct sockaddr_un sun;

    if (irix_unix_sockaddr(rt, addr, len, &sun) != 0) {
        fprintf(stderr, "IRIX bind bridge: invalid addr=%08x len=%u errno=%d\n",
                addr, len, errno);
        ret_i32(cpu, -1);
        return;
    }
    {
        int rc = bind(fd, (struct sockaddr *)&sun, sizeof(sun));
        ret_i32(cpu, rc);
    }
}

static void bridge_listen(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    int fd = (int)(int32_t)cpu->gpr[4];
    int backlog = (int)(int32_t)cpu->gpr[5];
    (void)rt;
    {
        int rc = listen(fd, backlog);
        ret_i32(cpu, rc);
    }
}

static void bridge_accept(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    int fd = (int)(int32_t)cpu->gpr[4];
    IrixGuestAddr addr = (uint32_t)cpu->gpr[5];
    IrixGuestAddr len_addr = (uint32_t)cpu->gpr[6];
    socklen_t host_len = sizeof(struct sockaddr_un);
    struct sockaddr_un sun;
    int accepted;

    if (addr && !irix_guest_ptr(rt, addr, sizeof(struct sockaddr_un))) {
        errno = EFAULT;
        ret_i32(cpu, -1);
        return;
    }
    if (len_addr && !irix_guest_ptr(rt, len_addr, 4u)) {
        errno = EFAULT;
        ret_i32(cpu, -1);
        return;
    }
    accepted = accept(fd, addr ? (struct sockaddr *)&sun : NULL,
                      addr ? &host_len : NULL);
    if (accepted < 0) {
        ret_i32(cpu, -1);
        return;
    }
    if (addr) {
        uint32_t guest_len = irix_guest_read32be(rt, len_addr);
        uint32_t path_len = (uint32_t)host_len > 2u ? (uint32_t)host_len - 2u : 0u;
        uint32_t copy_len;
        if (path_len > sizeof(sun.sun_path)) path_len = sizeof(sun.sun_path);
        copy_len = guest_len < sizeof(struct sockaddr_un) ? guest_len : sizeof(struct sockaddr_un);
        if (copy_len >= 2u) {
            irix_guest_write16be(rt, addr, 1u);
            if (copy_len > 2u) {
                uint32_t n = copy_len - 2u;
                if (n > path_len) n = path_len;
                memcpy(irix_guest_ptr(rt, addr + 2u, n), sun.sun_path, n);
            }
        }
        irix_guest_write32be(rt, len_addr, (uint32_t)host_len);
        if (rt->trapped) {
            close(accepted);
            ret_i32(cpu, -1);
            return;
        }
    }
    ret_i32(cpu, accepted);
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
    char *s = (char *)irix_guest_cstr(rt, (uint32_t)cpu->gpr[4], 65536u);
    (void)rt;
    ret_i32(cpu, s ? putenv(s) : (errno = EFAULT, -1));
}

int irix_signal_install(int irix_sig, uint32_t handler);
int irix_signal_to_host(int irix_sig);
void irix_signal_bridge_init(void);

static void bridge_alarm(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)rt;
    cpu->gpr[2] = (uint32_t)alarm((unsigned)(uint32_t)cpu->gpr[4]);
}

static void bridge_kill(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    int sig = (int)(int32_t)cpu->gpr[5];
    ret_i32(cpu, kill((pid_t)(int32_t)cpu->gpr[4],
                      sig > 0 ? irix_signal_to_host(sig) : sig));
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

static void bridge_times(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr out = (uint32_t)cpu->gpr[4];
    struct tms t;
    clock_t rc;

    errno = 0;
    rc = times(&t);
    if (rc == (clock_t)-1) {
        ret_i32(cpu, -1);
        return;
    }

    /*
     * IRIX N32 struct tms is four 32-bit clock_t values.  xwsh passes a
     * guest stack object here, so marshal the host result explicitly rather
     * than exposing the host struct layout/endian order.
     */
    if (out) {
        if (!irix_guest_ptr(rt, out, 16u)) {
            errno = EFAULT;
            ret_i32(cpu, -1);
            return;
        }
        irix_guest_write32be(rt, out + 0u, (uint32_t)(int32_t)t.tms_utime);
        irix_guest_write32be(rt, out + 4u, (uint32_t)(int32_t)t.tms_stime);
        irix_guest_write32be(rt, out + 8u, (uint32_t)(int32_t)t.tms_cutime);
        irix_guest_write32be(rt, out + 12u, (uint32_t)(int32_t)t.tms_cstime);
        if (rt->trapped) {
            errno = EFAULT;
            ret_i32(cpu, -1);
            return;
        }
    }

    ret_i32(cpu, (int32_t)rc);
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

static void bridge_cftime(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr output = (IrixGuestAddr)(uint32_t)cpu->gpr[4];
    IrixGuestAddr format_addr = (IrixGuestAddr)(uint32_t)cpu->gpr[5];
    IrixGuestAddr clock_addr = (IrixGuestAddr)(uint32_t)cpu->gpr[6];
    const char *format = format_addr ? guest_cstr(rt, format_addr) : NULL;
    int64_t seconds;
    time_t host_seconds;
    struct tm local;
    char text[256];
    char host_format[256];
    size_t length;
    char *guest_output;
    size_t format_len, i, host_format_len = 0u;

    if (!output || !clock_addr || rt->trapped) {
        errno = EFAULT;
        ret_i32(cpu, -1);
        return;
    }

    /* IRIX N32 uses 32-bit long, and IRIX time_t is a long. */
    seconds = (int32_t)irix_guest_read32be(rt, clock_addr);
    if (rt->trapped) {
        errno = EFAULT;
        ret_i32(cpu, -1);
        return;
    }
    host_seconds = (time_t)seconds;
    if ((int64_t)host_seconds != seconds || !localtime_r(&host_seconds, &local)) {
        errno = EOVERFLOW;
        ret_i32(cpu, -1);
        return;
    }

    /* SGI cftime(3C): cftime(char *s, char *format, const time_t *clock). */
    if (!format || !*format) {
        format = getenv("CFTIME");
        if (!format || !*format)
            format = "%c";
    }
    /* SGI cftime reserves %C for date(1)-style output; map it to libc %c. */
    format_len = strlen(format);
    for (i = 0; i < format_len; ++i) {
        if (format[i] == '%' && i + 1u < format_len && format[i + 1u] == 'C') {
            if (host_format_len + 2u >= sizeof(host_format)) {
                errno = EOVERFLOW;
                ret_i32(cpu, -1);
                return;
            }
            host_format[host_format_len++] = '%';
            host_format[host_format_len++] = 'c';
            ++i;
        } else {
            if (host_format_len + 1u >= sizeof(host_format)) {
                errno = EOVERFLOW;
                ret_i32(cpu, -1);
                return;
            }
            host_format[host_format_len++] = format[i];
        }
    }
    host_format[host_format_len] = '\0';
    length = strftime(text, sizeof(text), host_format, &local);
    if (!length) {
        errno = EOVERFLOW;
        ret_i32(cpu, -1);
        return;
    }
    guest_output = (char *)irix_guest_ptr(rt, output, (IrixGuestAddr)length + 1u);
    if (!guest_output) {
        errno = EFAULT;
        ret_i32(cpu, -1);
        return;
    }
    memcpy(guest_output, text, length + 1u);
    ret_i32(cpu, (int)length);
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
    if (!strcmp(name, "getpwent")) return bridge_getpwent;
    if (!strcmp(name, "setpwent")) return bridge_setpwent;
    if (!strcmp(name, "endpwent")) return bridge_endpwent;
    if (!strcmp(name, "getpwuid_r")) return bridge_getpwuid_r;
    if (!strcmp(name, "getpwnam_r")) return bridge_getpwnam_r;
    if (!strcmp(name, "getuid")) return bridge_getuid;
    if (!strcmp(name, "geteuid")) return bridge_geteuid;
    if (!strcmp(name, "getgid")) return bridge_getgid;
    if (!strcmp(name, "getegid")) return bridge_getegid;
    if (!strcmp(name, "initgroups")) return bridge_initgroups;
    if (!strcmp(name, "realpath")) return bridge_realpath;
    if (!strcmp(name, "execvp")) return bridge_execvp;
    if (!strcmp(name, "execl")) return bridge_execl;
    if (!strcmp(name, "execle")) return bridge_execle;
    if (!strcmp(name, "exit")) return bridge_exit;
    if (!strcmp(name, "socket")) return bridge_socket;
    if (!strcmp(name, "socketpair")) return bridge_socketpair;
    if (!strcmp(name, "dlopen")) return bridge_dlopen;
    if (!strcmp(name, "dlclose")) return bridge_dlclose;
    if (!strcmp(name, "dlerror")) return bridge_dlerror;
    if (!strcmp(name, "connect")) return bridge_connect;
    if (!strcmp(name, "bind")) return bridge_bind;
    if (!strcmp(name, "listen")) return bridge_listen;
    if (!strcmp(name, "accept")) return bridge_accept;
    if (!strcmp(name, "fork")) return bridge_fork;
    if (!strcmp(name, "select")) return bridge_select;
    if (!strcmp(name, "times")) return bridge_times;
    if (!strcmp(name, "sginap")) return bridge_sginap;
    if (!strcmp(name, "cftime")) return bridge_cftime;
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
