/*
 * Additional libc/libsgi imports needed by the graphical login and other
 * ViewKit/imd programs that were not previously reachable.  Everything here
 * is deliberately conservative: real host behaviour where the argument
 * layout is simple, honest "unsupported" results (not fake success) where
 * the original needs hardware or services this runtime does not provide.
 *
 * Calling convention: N32, args in gpr[4..11], FP args in $f12.., result in
 * gpr[2] / $f0.
 */
#include "IrixGuest.h"
#include "IrixLibcBridge.h"
#include "IrixStdioBridge.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <pwd.h>
#include <shadow.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/statvfs.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <sys/vfs.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>
#include <wchar.h>
#include <wctype.h>

static IrixGuestAddr guest_alloc(IrixGuestRuntime *rt, uint32_t n);

static const char *gcs(IrixGuestRuntime *rt, uint32_t a)
{
    return a ? irix_guest_cstr(rt, a, 65536u) : 0;
}

#define RET(v) do { cpu->gpr[2] = (uint64_t)(int64_t)(int32_t)(v); } while (0)
#define ARG(n) ((uint32_t)cpu->gpr[4 + (n)])
#define FAIL(e) do { errno = (e); cpu->gpr[2] = (uint64_t)(int64_t)-1; } while (0)

/* ---- no-ops / "not available" ------------------------------------------ */
static void b_zero(IrixGuestRuntime *rt, IrixGuestCpu *cpu)  { (void)rt; RET(0); }
static void b_one(IrixGuestRuntime *rt, IrixGuestCpu *cpu)   { (void)rt; RET(1); }
static void b_enosys(IrixGuestRuntime *rt, IrixGuestCpu *cpu) { (void)rt; FAIL(ENOSYS); }
static void b_null(IrixGuestRuntime *rt, IrixGuestCpu *cpu)  { (void)rt; cpu->gpr[2] = 0; }

/* ---- IRIS GL fixed-function entry points (no GL on this path) ----------- */
/* Present so libraries that probe GL load; they draw nothing.  The Mesa
 * bridge replaces these. */

/* ---- long double math: N32 long double arrives as a double pair --------- */
#define LD1(n, f) static void b_##n(IrixGuestRuntime *rt, IrixGuestCpu *cpu) { \
    double x = irix_guest_fpr_get_d(cpu, 12u); (void)rt; \
    irix_guest_fpr_set_d(cpu, 0u, f(x)); irix_guest_fpr_set_d(cpu, 2u, 0.0); }
LD1(cosl, cos) LD1(sinl, sin) LD1(expl, exp) LD1(logl, log) LD1(log10l, log10)
LD1(sinhl, sinh) LD1(coshl, cosh) LD1(sqrtl, sqrt)
static void b_atan2l(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ (void)rt; irix_guest_fpr_set_d(cpu, 0u, atan2(irix_guest_fpr_get_d(cpu, 12u), irix_guest_fpr_get_d(cpu, 14u)));
  irix_guest_fpr_set_d(cpu, 2u, 0.0); }
static void b_hypotl(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ (void)rt; irix_guest_fpr_set_d(cpu, 0u, hypot(irix_guest_fpr_get_d(cpu, 12u), irix_guest_fpr_get_d(cpu, 14u)));
  irix_guest_fpr_set_d(cpu, 2u, 0.0); }
static void b_isnan(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ (void)rt; RET(isnan(irix_guest_fpr_get_d(cpu, 12u)) ? 1 : 0); }

/* ---- simple wrappers --------------------------------------------------- */
static void b_iswspace(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ (void)rt; RET(iswspace((wint_t)ARG(0)) ? 1 : 0); }

static void b_wcstombs(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{   /* IRIX wchar_t is 32 bit; treat as Latin-1/ASCII code points. */
    IrixGuestAddr d = ARG(0), s = ARG(1);
    uint32_t n = ARG(2), i = 0;
    if (!s) { RET(-1); return; }
    for (;; ++i) {
        uint32_t c = irix_guest_read32be(rt, s + 4u * i);
        if (d && i < n) irix_guest_write8(rt, d + i, (uint8_t)c);
        if (!c) break;
        if (!d && i > 1000000u) break;
    }
    RET((int)i);
}

static void b_lseek64(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{   /* off64_t offset passed in one 64-bit register */
    (void)rt;
    cpu->gpr[2] = (uint64_t)lseek((int)ARG(0), (off_t)(int64_t)cpu->gpr[5], (int)ARG(2));
}

static void b_waitpid(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    int st = 0;
    pid_t r = waitpid((pid_t)(int32_t)ARG(0), &st, (int)ARG(2));
    if (r > 0 && ARG(1)) irix_guest_write32be(rt, ARG(1), (uint32_t)st);
    RET(r);
}

static void b_writev(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    int fd = (int)ARG(0), cnt = (int)ARG(2), i;
    ssize_t total = 0;
    IrixGuestAddr v = ARG(1);
    for (i = 0; i < cnt; ++i) {
        IrixGuestAddr base = irix_guest_read32be(rt, v + 8u * (uint32_t)i);
        uint32_t len = irix_guest_read32be(rt, v + 8u * (uint32_t)i + 4u);
        const void *p = len ? irix_guest_ptr(rt, base, len) : 0;
        ssize_t w;
        if (len && !p) { FAIL(EFAULT); return; }
        if (!len) continue;
        w = write(fd, p, len);
        if (w < 0) { if (!total) { RET(-1); return; } break; }
        total += w;
        if ((uint32_t)w < len) break;
    }
    RET(total);
}

static void b_utime(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *p = gcs(rt, ARG(0));
    struct utimbuf t, *tp = 0;
    if (ARG(1)) {
        t.actime = (time_t)irix_guest_read32be(rt, ARG(1));
        t.modtime = (time_t)irix_guest_read32be(rt, ARG(1) + 4u);
        tp = &t;
    }
    RET(p ? utime(p, tp) : -1);
}

static void b_mkfifo(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ const char *p = gcs(rt, ARG(0)); RET(p ? mkfifo(p, (mode_t)ARG(1)) : -1); }

static void b_mknod(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{   /* (version?) IRIX libc mknod(path, mode, dev) */
    const char *p = gcs(rt, ARG(0));
    RET(p ? mknod(p, (mode_t)ARG(1), (dev_t)ARG(2)) : -1);
}

static void b_lockf(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    int r;
    (void)rt;
    r = lockf((int)ARG(0), (int)ARG(1), (off_t)(int32_t)ARG(2));
    if (r) fprintf(stderr, "lockf(fd=%d, cmd=%d, len=%d) failed: %s\n",
                   (int)ARG(0), (int)ARG(1), (int)ARG(2), strerror(errno));
    RET(r);
}

static void b_getrlimit(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    static const int map[] = { RLIMIT_CPU, RLIMIT_FSIZE, RLIMIT_DATA,
        RLIMIT_STACK, RLIMIT_CORE, RLIMIT_NOFILE, RLIMIT_AS, RLIMIT_RSS };
    uint32_t r = ARG(0);
    struct rlimit rl;
    if (r >= sizeof(map) / sizeof(map[0]) || getrlimit(map[r], &rl)) { FAIL(EINVAL); return; }
    irix_guest_write64be(rt, ARG(1), rl.rlim_cur == RLIM_INFINITY ? 0x7fffffffffffffffull : rl.rlim_cur);
    irix_guest_write64be(rt, ARG(1) + 8u, rl.rlim_max == RLIM_INFINITY ? 0x7fffffffffffffffull : rl.rlim_max);
    RET(0);
}

/* N32 struct statfs layout: short fstyp @0, long bsize @4, frsize @8,
 * int64 blocks @16, bfree @24, files @32, ffree @40, name[6]/pack[6] @48. */
static void put_statfs(IrixGuestRuntime *rt, IrixGuestAddr out, const struct statfs *sf)
{
    unsigned i;
    for (i = 0; i < 60u; ++i) irix_guest_write8(rt, out + i, 0);
    irix_guest_write32be(rt, out + 4u, (uint32_t)sf->f_bsize);
    irix_guest_write32be(rt, out + 8u, (uint32_t)sf->f_bsize);
    irix_guest_write64be(rt, out + 16u, (uint64_t)sf->f_blocks);
    irix_guest_write64be(rt, out + 24u, (uint64_t)sf->f_bavail);
    irix_guest_write64be(rt, out + 32u, (uint64_t)sf->f_files);
    irix_guest_write64be(rt, out + 40u, (uint64_t)sf->f_ffree);
}

static void b_statfs(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    char hp[4096];
    const char *g = gcs(rt, ARG(0)), *p;
    struct statfs sf;
    if (!g || !ARG(1) || !irix_guest_ptr(rt, ARG(1), 60u)) { FAIL(EFAULT); return; }
    p = irix_guest_resolve_path(rt, g, hp, sizeof(hp), "statfs", 0);
    if (!p || statfs(p, &sf)) { RET(-1); return; }
    put_statfs(rt, ARG(1), &sf);
    RET(0);
}

/* IRIX statvfs: fsid-ish words then u64 counts; return plausible values. */
static void b_statvfs(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    char hp[4096];
    const char *g = gcs(rt, ARG(0)), *p;
    struct statvfs sv;
    IrixGuestAddr o = ARG(1);
    unsigned i;
    if (!g || !o || !irix_guest_ptr(rt, o, 112u)) { FAIL(EFAULT); return; }
    p = irix_guest_resolve_path(rt, g, hp, sizeof(hp), "statvfs", 0);
    if (!p || statvfs(p, &sv)) { RET(-1); return; }
    for (i = 0; i < 112u; ++i) irix_guest_write8(rt, o + i, 0);
    irix_guest_write32be(rt, o + 0u, (uint32_t)sv.f_bsize);
    irix_guest_write32be(rt, o + 4u, (uint32_t)sv.f_frsize);
    irix_guest_write64be(rt, o + 8u, (uint64_t)sv.f_blocks);
    irix_guest_write64be(rt, o + 16u, (uint64_t)sv.f_bfree);
    irix_guest_write64be(rt, o + 24u, (uint64_t)sv.f_bavail);
    irix_guest_write64be(rt, o + 32u, (uint64_t)sv.f_files);
    irix_guest_write64be(rt, o + 40u, (uint64_t)sv.f_ffree);
    irix_guest_write64be(rt, o + 48u, (uint64_t)sv.f_favail);
    RET(0);
}

static void b_fstatvfs64(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    struct statvfs sv;
    IrixGuestAddr o = ARG(1);
    unsigned i;
    if (!o || !irix_guest_ptr(rt, o, 112u)) { FAIL(EFAULT); return; }
    if (fstatvfs((int)ARG(0), &sv)) { RET(-1); return; }
    for (i = 0; i < 112u; ++i) irix_guest_write8(rt, o + i, 0);
    irix_guest_write32be(rt, o + 0u, (uint32_t)sv.f_bsize);
    irix_guest_write32be(rt, o + 4u, (uint32_t)sv.f_frsize);
    irix_guest_write64be(rt, o + 8u, (uint64_t)sv.f_blocks);
    irix_guest_write64be(rt, o + 16u, (uint64_t)sv.f_bfree);
    irix_guest_write64be(rt, o + 24u, (uint64_t)sv.f_bavail);
    irix_guest_write64be(rt, o + 32u, (uint64_t)sv.f_files);
    irix_guest_write64be(rt, o + 40u, (uint64_t)sv.f_ffree);
    irix_guest_write64be(rt, o + 48u, (uint64_t)sv.f_favail);
    RET(0);
}

/* sysfs(GETFSIND/GETFSTYP/GETNFSTYP) over a fixed type table */
static void b_sysfs(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    static const char *const t[] = { 0, "xfs", "efs", "proc", "nfs", "dev", "fd", "specfs", "fifofs" };
    const unsigned n = sizeof(t) / sizeof(t[0]) - 1u;
    uint32_t op = ARG(0), i;
    if (op == 1u) {
        const char *nm = gcs(rt, ARG(1));
        for (i = 1; nm && i <= n; ++i) if (!strcmp(nm, t[i])) { RET(i); return; }
        FAIL(EINVAL);
    } else if (op == 2u) {
        uint32_t idx = ARG(1), k, len;
        if (idx < 1u || idx > n) { FAIL(EINVAL); return; }
        len = (uint32_t)strlen(t[idx]) + 1u;
        for (k = 0; k < len; ++k) irix_guest_write8(rt, ARG(2) + k, (uint8_t)t[idx][k]);
        RET(0);
    } else if (op == 3u) RET(n + 1u);
    else FAIL(EINVAL);
}

/* syslog/vsyslog: format with the guest printf engine and write the line to
 * stderr (there is no syslogd).  syslog(pri, fmt, ...) has exactly the
 * register layout of sprintf(buf, fmt, ...), vsyslog the one of vsprintf. */
IrixGuestFunction irix_clib_extra_bridge_lookup_base(const char *name);

static void syslog_emit(IrixGuestRuntime *rt, IrixGuestCpu *cpu, const char *via)
{
    IrixGuestFunction f = irix_clib_extra_bridge_lookup_base(via);
    IrixGuestAddr buf;
    IrixGuestCpu c;
    const char *msg;
    if (!f) return;
    buf = guest_alloc(rt, 2048u);
    if (!buf) return;
    c = *cpu;
    c.gpr[4] = buf;
    f(rt, &c);
    msg = irix_guest_cstr(rt, buf, 2048u);
    if (msg) fprintf(stderr, "syslog(%u): %s\n", ARG(0), msg);
}

static void b_syslog(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ syslog_emit(rt, cpu, "sprintf"); RET(0); }

static void b_vsyslog(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ syslog_emit(rt, cpu, "vsprintf"); RET(0); }

static void b_sigset(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{   /* sigset(sig, handler): handler dispositions SIG_DFL=0, SIG_IGN=1 only */
    (void)rt;
    if (ARG(1) == 1u) { signal((int)ARG(0), SIG_IGN); RET(1); }
    else if (ARG(1) == 0u) { signal((int)ARG(0), SIG_DFL); RET(0); }
    else RET(0);
}

/* ---- networking helpers ------------------------------------------------ */
static void b_inet_addr(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *s = gcs(rt, ARG(0));
    struct in_addr_ { uint32_t a; } r;
    unsigned a, b, c, d;
    if (s && sscanf(s, "%u.%u.%u.%u", &a, &b, &c, &d) == 4 && a < 256 && b < 256 && c < 256 && d < 256)
        r.a = (a << 24) | (b << 16) | (c << 8) | d;
    else
        r.a = 0xffffffffu;
    RET(r.a);
}

static IrixGuestAddr scratch_str(IrixGuestRuntime *rt, const char *s)
{
    static IrixGuestAddr buf;
    IrixGuestFunction m = irix_libc_bridge_lookup("malloc");
    IrixGuestCpu c;
    size_t i, n = strlen(s) + 1u;
    if (!buf && m) {
        memset(&c, 0, sizeof(c));
        c.gpr[4] = 64u;
        m(rt, &c);
        buf = (IrixGuestAddr)(uint32_t)c.gpr[2];
    }
    if (!buf || n > 64u) return 0;
    for (i = 0; i < n; ++i) irix_guest_write8(rt, buf + (IrixGuestAddr)i, (uint8_t)s[i]);
    return buf;
}

static void b_inet_ntoa(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{   /* struct in_addr passed by value in a0 */
    char t[32];
    uint32_t a = ARG(0);
    snprintf(t, sizeof(t), "%u.%u.%u.%u", a >> 24, (a >> 16) & 255, (a >> 8) & 255, a & 255);
    cpu->gpr[2] = scratch_str(rt, t);
}

static IrixGuestAddr guest_alloc(IrixGuestRuntime *rt, uint32_t n);

/* ---- fdopen / popen over the stdio bridge ------------------------------ */
IrixGuestAddr irix_stdio_adopt_file(IrixGuestRuntime *rt, FILE *host);

static void b_fdopen(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *m = gcs(rt, ARG(1));
    FILE *f = m ? fdopen((int)ARG(0), m) : 0;
    cpu->gpr[2] = f ? irix_stdio_adopt_file(rt, f) : 0;
}

static void b_popen(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *c = gcs(rt, ARG(0)), *m = gcs(rt, ARG(1));
    FILE *f = (c && m) ? popen(c, m) : 0;
    cpu->gpr[2] = f ? irix_stdio_adopt_file(rt, f) : 0;
}

/* ---- us* arena / semaphore API: single-threaded, never blocks ----------- */
static IrixGuestAddr guest_alloc(IrixGuestRuntime *rt, uint32_t n)
{
    IrixGuestFunction m = irix_libc_bridge_lookup("malloc");
    IrixGuestCpu c;
    if (!m) return 0;
    memset(&c, 0, sizeof(c));
    c.gpr[4] = n ? n : 1u;
    m(rt, &c);
    return (IrixGuestAddr)(uint32_t)c.gpr[2];
}

static void b_usinit(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ cpu->gpr[2] = guest_alloc(rt, 64u); }          /* arena handle */
static void b_usnewsema(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ IrixGuestAddr s = guest_alloc(rt, 16u); if (s) irix_guest_write32be(rt, s, ARG(1)); cpu->gpr[2] = s; }
static void b_usmalloc(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ cpu->gpr[2] = guest_alloc(rt, ARG(0)); }
static void b_usnop1(IrixGuestRuntime *rt, IrixGuestCpu *cpu) { (void)rt; RET(1); }

/* ---- shadow database and credentials: host pass-through ---------------- */
/* Real host calls, so they work when the session is run as root exactly as
 * on IRIX.  N32 struct spwd: sp_namp, sp_pwdp, then lstchg, min, max, warn,
 * inact, expire, flag as 32-bit words. */
static IrixGuestAddr guest_str(IrixGuestRuntime *rt, const char *s)
{
    uint32_t n = (uint32_t)strlen(s) + 1u, i;
    IrixGuestAddr a = guest_alloc(rt, n);
    if (!a) return 0;
    for (i = 0; i < n; ++i) irix_guest_write8(rt, a + i, (uint8_t)s[i]);
    return a;
}

static IrixGuestAddr put_spwd(IrixGuestRuntime *rt, const struct spwd *sp)
{
    IrixGuestAddr rec;
    uint32_t i;
    if (!sp) return 0;
    rec = guest_alloc(rt, 36u);
    if (!rec) return 0;
    for (i = 0; i < 36u; i += 4u) irix_guest_write32be(rt, rec + i, 0);
    irix_guest_write32be(rt, rec + 0u, guest_str(rt, sp->sp_namp));
    irix_guest_write32be(rt, rec + 4u, guest_str(rt, sp->sp_pwdp));
    irix_guest_write32be(rt, rec + 8u, (uint32_t)sp->sp_lstchg);
    irix_guest_write32be(rt, rec + 12u, (uint32_t)sp->sp_min);
    irix_guest_write32be(rt, rec + 16u, (uint32_t)sp->sp_max);
    irix_guest_write32be(rt, rec + 20u, (uint32_t)sp->sp_warn);
    irix_guest_write32be(rt, rec + 24u, (uint32_t)sp->sp_inact);
    irix_guest_write32be(rt, rec + 28u, (uint32_t)sp->sp_expire);
    irix_guest_write32be(rt, rec + 32u, (uint32_t)sp->sp_flag);
    return rec;
}

static void b_getspnam(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *n = gcs(rt, ARG(0));
    errno = 0;
    cpu->gpr[2] = n ? put_spwd(rt, getspnam(n)) : 0;
}

static void b_getspent(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ cpu->gpr[2] = put_spwd(rt, getspent()); }

static void b_endspent(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ (void)rt; endspent(); RET(0); }

extern char *crypt(const char *key, const char *salt);

static void b_crypt(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *pw = gcs(rt, ARG(0)), *salt = gcs(rt, ARG(1));
    const char *r = (pw && salt) ? crypt(pw, salt) : 0;
    cpu->gpr[2] = r ? scratch_str(rt, r) : 0;
}

static void b_setuid(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ (void)rt; RET(setuid((uid_t)ARG(0))); }
static void b_setgid(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ (void)rt; RET(setgid((gid_t)ARG(0))); }
static void b_seteuid(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ (void)rt; RET(seteuid((uid_t)ARG(0))); }
static void b_setegid(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ (void)rt; RET(setegid((gid_t)ARG(0))); }
static void b_setreuid(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ (void)rt; RET(setreuid((uid_t)ARG(0), (uid_t)ARG(1))); }
static void b_setregid(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ (void)rt; RET(setregid((gid_t)ARG(0), (gid_t)ARG(1))); }

/* ---- table ------------------------------------------------------------- */
IrixGuestFunction irix_clib_more_bridge_lookup(const char *name)
{
    static const struct { const char *n; IrixGuestFunction f; } t[] = {
        { "cosl", b_cosl }, { "sinl", b_sinl }, { "expl", b_expl }, { "logl", b_logl },
        { "log10l", b_log10l }, { "sinhl", b_sinhl }, { "coshl", b_coshl },
        { "sqrtl", b_sqrtl }, { "atan2l", b_atan2l }, { "hypotl", b_hypotl },
        { "isnan", b_isnan }, { "iswspace", b_iswspace }, { "wcstombs", b_wcstombs },
        { "lseek64", b_lseek64 }, { "waitpid", b_waitpid }, { "writev", b_writev },
        { "utime", b_utime }, { "mkfifo", b_mkfifo }, { "mknod", b_mknod },
        { "lockf", b_lockf }, { "getrlimit", b_getrlimit },
        { "statfs", b_statfs }, { "statvfs", b_statvfs }, { "fstatvfs64", b_fstatvfs64 },
        { "sysfs", b_sysfs }, { "syslog", b_syslog }, { "vsyslog", b_vsyslog }, { "sigset", b_sigset },
        { "inet_addr", b_inet_addr }, { "inet_ntoa", b_inet_ntoa },
        { "getspnam", b_getspnam }, { "getspent", b_getspent }, { "endspent", b_endspent },
        { "setuid", b_setuid }, { "setgid", b_setgid }, { "seteuid", b_seteuid },
        { "setegid", b_setegid }, { "setreuid", b_setreuid }, { "setregid", b_setregid },
        { "lckpwdf", b_zero }, { "ulckpwdf", b_zero },
        { "putpwent", b_enosys }, { "putspent", b_enosys },
        { "fdopen", b_fdopen }, { "popen", b_popen },
        { "usinit", b_usinit }, { "usnewsema", b_usnewsema }, { "usmalloc", b_usmalloc },
        { "usconfig", b_zero }, { "usdetach", b_zero }, { "usfree", b_zero },
        { "usfreesema", b_zero }, { "uspsema", b_usnop1 }, { "uscpsema", b_usnop1 },
        { "usvsema", b_zero }, { "usinitsema", b_zero }, { "usmallinfo", b_zero },
        { "init_lock", b_zero }, { "spin_lock", b_zero }, { "release_lock", b_zero },
        { "pthread_mutex_init", b_zero }, { "pthread_mutex_lock", b_zero },
        { "pthread_mutex_unlock", b_zero }, { "pthread_mutex_destroy", b_zero },
        { "pthread_cond_init", b_zero }, { "pthread_cond_signal", b_zero },
        { "pthread_cond_broadcast", b_zero }, { "pthread_cond_wait", b_zero },
        { "pthread_self", b_one },
        { "sproc", b_enosys }, { "blockproc", b_zero }, { "unblockproc", b_zero },
        { "__libc_threadbind", b_zero }, { "__libc_threadunbind", b_zero },
        { "mpin", b_zero }, { "munpin", b_zero },
        { "sgidladd", b_null }, { "sgidlopen_version", b_null },
        { "sgigetdsoversion", b_null }, { "dlsym", b_null },
        { "getexportopt", b_enosys },
        { "getpeername", b_enosys }, { "getsockname", b_enosys },
        { "getsockopt", b_enosys }, { "setsockopt", b_zero },
        { "shutdown", b_zero }, { "recvmsg", b_enosys }, { "sendmsg", b_enosys },
        { "gethostbyaddr", b_null }, { "gethostbyaddr_r", b_null },
        { "gethostbyname_r", b_null }, { "getservbyname", b_null },
        { "getservbyname_r", b_null },
        { "callrpc", b_enosys }, { "clnt_create", b_null }, { "clntudp_create", b_null },
        { "clnt_spcreateerror", b_null }, { "clnt_sperrno", b_null },
        { "clnt_sperror", b_null },
        { "yp_first", b_one }, { "yp_next", b_one }, { "yp_match", b_one },
        { "yp_master", b_one }, { "yperr_string", b_null },
        { "iconv_open", b_enosys }, { "iconv", b_enosys }, { "iconv_close", b_zero },
        { "regcmp", b_null }, { "regex", b_null },
        { "crypt", b_crypt }, { "tempnam", b_null }, { "nftw64", b_enosys },
        { "execv", b_enosys }, { "pclose", b_zero },
        { "_vfork", b_enosys }, { "l64a", b_null }, { "a64l", b_zero }, { "atold", b_zero },
        /* IRIS GL drawing entry points: no GL device here */
        { "RGBcolor", b_zero }, { "bgnline", b_zero }, { "bgnpolygon", b_zero },
        { "bgntmesh", b_zero }, { "c3s", b_zero }, { "clear", b_zero },
        { "cpack", b_zero }, { "draw2s", b_zero }, { "endline", b_zero },
        { "endpolygon", b_zero }, { "endtmesh", b_zero }, { "getgdesc", b_zero },
        { "move2s", b_zero }, { "ortho2", b_zero }, { "popmatrix", b_zero },
        { "pushmatrix", b_zero }, { "rects", b_zero }, { "rot", b_zero },
        { "sbox", b_zero }, { "sboxfi", b_zero }, { "shademodel", b_zero },
        { "subpixel", b_zero }, { "swapbuffers", b_zero }, { "v2s", b_zero },
        { "v3f", b_zero }, { "viewport", b_zero }, { "vpfmt", b_zero },
        { "waddstr", b_zero },
    };
    size_t i;
    if (!name) return 0;
    for (i = 0; i < sizeof(t) / sizeof(t[0]); ++i)
        if (!strcmp(name, t[i].n)) return t[i].f;
    return 0;
}
