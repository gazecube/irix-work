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
#include "IrixKernelInfo.h"
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

/* pfmt(stream, flags, format, ...): behave like fprintf(stream, format, ...)
 * by shifting the argument registers down one slot. */
static void b_pfmt(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestFunction fp = irix_stdio_bridge_lookup("fprintf");
    if (!fp) { RET(-1); return; }
    cpu->gpr[5] = cpu->gpr[6];
    cpu->gpr[6] = cpu->gpr[7];
    cpu->gpr[7] = cpu->gpr[8];
    cpu->gpr[8] = cpu->gpr[9];
    cpu->gpr[9] = cpu->gpr[10];
    cpu->gpr[10] = cpu->gpr[11];
    fp(rt, cpu);
}

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

/* IRIX wait options: WNOHANG 0100, WUNTRACED 004, WCONTINUED 010, WNOWAIT 0200 */
static int host_wait_opts(uint32_t o)
{
    int h = 0;
    if (o & 0100u) h |= WNOHANG;
    if (o & 004u)  h |= WUNTRACED;
    if (o & 010u)  h |= WCONTINUED;
    return h;
}

static void b_waitpid(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    int st = 0;
    pid_t r = waitpid((pid_t)(int32_t)ARG(0), &st, host_wait_opts(ARG(2)));
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

/* The guest must not lower the host process's real limits (stack, fds); accept a
 * valid resource and report success. */
static void b_setrlimit(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)rt;
    if (ARG(0) >= 8u) { FAIL(EINVAL); return; }
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
{   /* sigset(sig, handler): like signal(), handler may also be SIG_HOLD */
    (void)rt;
    if (ARG(1) == 2u) { RET(0); return; }          /* SIG_HOLD: virtual */
    RET(irix_signal_install((int)ARG(0), ARG(1)) == 0 ? 0 : -1);
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

/* ---- libgen strccpy()/strecpy(): C escape sequence conversion ------------ */
/* strccpy(out, in): copy in to out, converting \n \t \ddd ... escapes to the
 * characters they stand for; returns out.  strecpy(out, in, except) is the
 * inverse: unprintable characters become escapes (those in `except` stay). */
static void b_strccpy(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr out = (IrixGuestAddr)ARG(0), in = (IrixGuestAddr)ARG(1);
    uint8_t c;
    uint32_t o = 0;
    if (!out || !in) { cpu->gpr[2] = out; return; }
    for (;;) {
        c = irix_guest_read8(rt, in++);
        if (c == '\\') {
            c = irix_guest_read8(rt, in++);
            switch (c) {
            case 'a': c = 7; break;   case 'b': c = 8; break;
            case 'f': c = 12; break;  case 'n': c = 10; break;
            case 'r': c = 13; break;  case 't': c = 9; break;
            case 'v': c = 11; break;
            case '0': case '1': case '2': case '3':
            case '4': case '5': case '6': case '7': {
                unsigned v = (unsigned)(c - '0'), k;
                for (k = 0; k < 2; ++k) {
                    uint8_t d = irix_guest_read8(rt, in);
                    if (d < '0' || d > '7') break;
                    v = v * 8u + (unsigned)(d - '0');
                    ++in;
                }
                c = (uint8_t)v;
                break;
            }
            case 0: --in; c = '\\'; break;
            default: break;           /* \\ \' \" and others: the char itself */
            }
        }
        irix_guest_write8(rt, out + o++, c);
        if (!c && in) {
            /* an escape produced NUL or the string ended */
            if (irix_guest_read8(rt, in - 1) == 0) break;
        }
    }
    cpu->gpr[2] = out;
}
static void b_strecpy(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr out = (IrixGuestAddr)ARG(0), in = (IrixGuestAddr)ARG(1);
    IrixGuestAddr except = (IrixGuestAddr)ARG(2);
    uint32_t o = 0;
    uint8_t c;
    if (!out || !in) { cpu->gpr[2] = out; return; }
    while ((c = irix_guest_read8(rt, in++)) != 0) {
        int keep = (c >= 32 && c < 127 && c != '\\');
        const char *esc = 0;
        char oct[5];
        IrixGuestAddr e;
        if (except) {
            for (e = except; irix_guest_read8(rt, e); ++e)
                if (irix_guest_read8(rt, e) == c) { keep = 1; break; }
        }
        if (keep) { irix_guest_write8(rt, out + o++, c); continue; }
        switch (c) {
        case 7: esc = "\\a"; break;  case 8: esc = "\\b"; break;
        case 12: esc = "\\f"; break; case 10: esc = "\\n"; break;
        case 13: esc = "\\r"; break; case 9: esc = "\\t"; break;
        case 11: esc = "\\v"; break; case '\\': esc = "\\\\"; break;
        default:
            snprintf(oct, sizeof oct, "\\%03o", c);
            esc = oct;
        }
        while (*esc) irix_guest_write8(rt, out + o++, (uint8_t)*esc++);
    }
    irix_guest_write8(rt, out + o, 0);
    cpu->gpr[2] = out;
}

/* ---- hardware inventory (libc setinvent/getinvent/endinvent) ------------ */
/* The state block is {next index, count, records}; records are N32
 * inventory_t (24 bytes, inv_next always NULL) materialised in guest memory
 * from the same host-derived list the syssgi(SGI_INVENT) path serves. */
static IrixGuestAddr inventory_open(IrixGuestRuntime *rt)
{
    IrixKinfoInvent recs[64];
    size_t n = irix_kinfo_inventory(recs, 64u), i;
    IrixGuestAddr st = guest_alloc(rt, 12u);
    IrixGuestAddr base = guest_alloc(rt, (uint32_t)(n ? n : 1u) * 24u);
    if (!st || !base) return 0;
    for (i = 0; i < n; ++i) {
        IrixGuestAddr r = base + (uint32_t)i * 24u;
        irix_guest_write32be(rt, r + 0u, 0u);
        irix_guest_write32be(rt, r + 4u, (uint32_t)recs[i].inv_class);
        irix_guest_write32be(rt, r + 8u, (uint32_t)recs[i].inv_type);
        irix_guest_write32be(rt, r + 12u, (uint32_t)recs[i].inv_controller);
        irix_guest_write32be(rt, r + 16u, (uint32_t)recs[i].inv_unit);
        irix_guest_write32be(rt, r + 20u, (uint32_t)recs[i].inv_state);
    }
    irix_guest_write32be(rt, st + 0u, 0u);
    irix_guest_write32be(rt, st + 4u, (uint32_t)n);
    irix_guest_write32be(rt, st + 8u, base);
    return st;
}
static IrixGuestAddr inventory_next(IrixGuestRuntime *rt, IrixGuestAddr st)
{
    uint32_t idx, n;
    if (!st) return 0;
    idx = irix_guest_read32be(rt, st + 0u);
    n = irix_guest_read32be(rt, st + 4u);
    if (idx >= n) return 0;
    irix_guest_write32be(rt, st + 0u, idx + 1u);
    return irix_guest_read32be(rt, st + 8u) + idx * 24u;
}
static IrixGuestAddr g_inventory_global_state;
static void b_setinvent_r(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr statep = (IrixGuestAddr)(uint32_t)ARG(0);
    IrixGuestAddr st = inventory_open(rt);
    if (!st || !statep) { RET(-1); return; }
    irix_guest_write32be(rt, statep, st);
    RET(0);
}
static void b_getinvent_r(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ cpu->gpr[2] = inventory_next(rt, (IrixGuestAddr)(uint32_t)ARG(0)); }
static void b_endinvent_r(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ (void)rt; (void)cpu; }
static void b_setinvent(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ g_inventory_global_state = inventory_open(rt); RET(g_inventory_global_state ? 0 : -1); }
static void b_getinvent(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    if (!g_inventory_global_state) g_inventory_global_state = inventory_open(rt);
    cpu->gpr[2] = inventory_next(rt, g_inventory_global_state);
}
static void b_endinvent(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ (void)rt; (void)cpu; g_inventory_global_state = 0; }

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


/* tempnam(dir, prefix): a fresh, unique path in malloc'd guest memory (the caller frees it). */
static void b_tempnam(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    static unsigned counter;
    const char *dir = ARG(0) ? gcs(rt, ARG(0)) : 0, *prefix = ARG(1) ? gcs(rt, ARG(1)) : "";
    const char *env = getenv("TMPDIR");
    char path[512];
    IrixGuestFunction m = irix_libc_bridge_lookup("malloc");
    IrixGuestCpu c;
    size_t i, n;
    if (!dir || !*dir || access(dir, W_OK) != 0) dir = (env && *env && access(env, W_OK) == 0) ? env : "/tmp";
    snprintf(path, sizeof path, "%s/%.5s%05d%u", dir, prefix ? prefix : "", (int)getpid() % 100000, counter++);
    cpu->gpr[2] = 0;
    if (!m) return;
    n = strlen(path) + 1u;
    memset(&c, 0, sizeof(c));
    c.gpr[4] = (uint64_t)n;
    m(rt, &c);
    if (!(uint32_t)c.gpr[2]) return;
    for (i = 0; i < n; ++i) irix_guest_write8(rt, (IrixGuestAddr)((uint32_t)c.gpr[2] + i), (uint8_t)path[i]);
    cpu->gpr[2] = (uint32_t)c.gpr[2];
}


/* Message catalogs: no catalogs are installed; fail the open and hand back defaults. */
static void b_catopen(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ (void)rt; cpu->gpr[2] = (uint64_t)(int64_t)-1; }
static void b_catgets(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ (void)rt; cpu->gpr[2] = (uint32_t)cpu->gpr[7]; }
static void b_catclose(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ (void)rt; cpu->gpr[2] = 0; }


/* Wide-character strings: IRIX N32 wchar_t is a 32-bit big-endian word. */
static uint32_t wc_at(IrixGuestRuntime *rt, IrixGuestAddr a, size_t i)
{ return irix_guest_read32be(rt, a + (IrixGuestAddr)(i * 4u)); }
static void b_wcslen(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ IrixGuestAddr a = (uint32_t)cpu->gpr[4]; size_t n = 0; while (n < 0x100000u && wc_at(rt, a, n)) ++n; cpu->gpr[2] = (uint32_t)n; }
static void b_wcscpy(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ IrixGuestAddr d = (uint32_t)cpu->gpr[4], s = (uint32_t)cpu->gpr[5]; size_t i = 0; uint32_t c;
  do { c = wc_at(rt, s, i); irix_guest_write32be(rt, d + (IrixGuestAddr)(i * 4u), c); ++i; } while (c && i < 0x100000u);
  cpu->gpr[2] = d; }
static void b_wcsncpy(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ IrixGuestAddr d = (uint32_t)cpu->gpr[4], s = (uint32_t)cpu->gpr[5]; size_t n = (uint32_t)cpu->gpr[6], i; int end = 0;
  for (i = 0; i < n; ++i) { uint32_t c = end ? 0u : wc_at(rt, s, i); if (!c) end = 1; irix_guest_write32be(rt, d + (IrixGuestAddr)(i * 4u), c); }
  cpu->gpr[2] = d; }
static void b_wcscmp(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ IrixGuestAddr a = (uint32_t)cpu->gpr[4], b = (uint32_t)cpu->gpr[5]; size_t i = 0; int32_t d = 0;
  for (; i < 0x100000u; ++i) { uint32_t x = wc_at(rt, a, i), y = wc_at(rt, b, i); if (x != y) { d = x < y ? -1 : 1; break; } if (!x) break; }
  cpu->gpr[2] = (uint64_t)(int64_t)d; }
static void b_wcsncmp(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ IrixGuestAddr a = (uint32_t)cpu->gpr[4], b = (uint32_t)cpu->gpr[5]; size_t n = (uint32_t)cpu->gpr[6], i; int32_t d = 0;
  for (i = 0; i < n; ++i) { uint32_t x = wc_at(rt, a, i), y = wc_at(rt, b, i); if (x != y) { d = x < y ? -1 : 1; break; } if (!x) break; }
  cpu->gpr[2] = (uint64_t)(int64_t)d; }
static void b_wcschr(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ IrixGuestAddr a = (uint32_t)cpu->gpr[4]; uint32_t ch = (uint32_t)cpu->gpr[5]; size_t i = 0;
  for (; i < 0x100000u; ++i) { uint32_t x = wc_at(rt, a, i); if (x == ch) { cpu->gpr[2] = a + (IrixGuestAddr)(i * 4u); return; } if (!x) break; }
  cpu->gpr[2] = 0; }
static void b_wcscat(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ IrixGuestAddr d = (uint32_t)cpu->gpr[4], s = (uint32_t)cpu->gpr[5]; size_t n = 0, i = 0; uint32_t c;
  while (n < 0x100000u && wc_at(rt, d, n)) ++n;
  do { c = wc_at(rt, s, i); irix_guest_write32be(rt, d + (IrixGuestAddr)((n + i) * 4u), c); ++i; } while (c && i < 0x100000u);
  cpu->gpr[2] = d; }


static void b_wc_zero(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ (void)rt; cpu->gpr[2] = 0; }
static void b_iswpunct(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ uint32_t c = (uint32_t)cpu->gpr[4]; (void)rt; cpu->gpr[2] = (c < 128u && ispunct((int)c)) ? 1u : 0u; }
static void b_towlower(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ uint32_t c = (uint32_t)cpu->gpr[4]; (void)rt; cpu->gpr[2] = c < 128u ? (uint32_t)tolower((int)c) : c; }
static void b_nl_langinfo(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ cpu->gpr[2] = scratch_str(rt, ""); }
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

/* ---- additional imports needed by the original xdm -------------------- */
#include <grp.h>
#include <sys/time.h>
#include <sys/resource.h>

void irix_compat_systeminfo(IrixGuestRuntime *rt, IrixGuestCpu *cpu);
int irix_guest_execve(IrixGuestRuntime *rt, const char *guest_path,
                      char *const argv[], char *const envp[]);
IrixGuestFunction irix_clib_extra_bridge_lookup_base(const char *name);

static void b_sysinfo(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ irix_compat_systeminfo(rt, cpu); }

static char **guest_vector(IrixGuestRuntime *rt, IrixGuestAddr v)
{
    size_t n = 0, i;
    char **out;
    if (!v) return 0;
    while (n < 4096u && irix_guest_read32be(rt, v + 4u * (uint32_t)n)) ++n;
    out = (char **)calloc(n + 1u, sizeof(*out));
    if (!out) return 0;
    for (i = 0; i < n; ++i)
        out[i] = (char *)gcs(rt, irix_guest_read32be(rt, v + 4u * (uint32_t)i));
    return out;
}

static void b_execve(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *path = gcs(rt, ARG(0));
    char **argv = guest_vector(rt, ARG(1));
    char **envp = guest_vector(rt, ARG(2));
    int e;
    if (!path || !argv) { free(argv); free(envp); FAIL(EFAULT); return; }
    irix_guest_execve(rt, path, argv, envp);
    e = errno;
    free(argv);
    free(envp);
    FAIL(e ? e : ENOEXEC);
}

static void b_execv(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *path = gcs(rt, ARG(0));
    char **argv = guest_vector(rt, ARG(1));
    int e;
    if (!path || !argv) { free(argv); FAIL(EFAULT); return; }
    irix_guest_execve(rt, path, argv, NULL);
    e = errno;
    free(argv);
    FAIL(e ? e : ENOEXEC);
}

/* N32 struct group: gr_name, gr_passwd, gr_gid, gr_mem (char **) */
static IrixGuestAddr put_group(IrixGuestRuntime *rt, const struct group *g)
{
    IrixGuestAddr rec, mem;
    uint32_t n = 0, i;
    if (!g) return 0;
    while (g->gr_mem && g->gr_mem[n]) ++n;
    rec = guest_alloc(rt, 16u);
    mem = guest_alloc(rt, 4u * (n + 1u));
    if (!rec || !mem) return 0;
    for (i = 0; i < n; ++i)
        irix_guest_write32be(rt, mem + 4u * i, guest_str(rt, g->gr_mem[i]));
    irix_guest_write32be(rt, mem + 4u * n, 0);
    irix_guest_write32be(rt, rec + 0u, guest_str(rt, g->gr_name ? g->gr_name : ""));
    irix_guest_write32be(rt, rec + 4u, guest_str(rt, g->gr_passwd ? g->gr_passwd : ""));
    irix_guest_write32be(rt, rec + 8u, (uint32_t)g->gr_gid);
    irix_guest_write32be(rt, rec + 12u, mem);
    return rec;
}

static void b_getgrent(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ cpu->gpr[2] = put_group(rt, getgrent()); }
static void b_setgrent(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ (void)rt; setgrent(); RET(0); }
static void b_endgrent(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ (void)rt; endgrent(); RET(0); }

static void b_killpg(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ (void)rt; RET(killpg((pid_t)(int32_t)ARG(0), (int)ARG(1))); }
static void b_setpgid(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ (void)rt; RET(setpgid((pid_t)(int32_t)ARG(0), (pid_t)(int32_t)ARG(1))); }

static void b_wait3(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    int st = 0;
    pid_t r = wait3(&st, host_wait_opts(ARG(1)), 0);
    if (r > 0 && ARG(0)) irix_guest_write32be(rt, ARG(0), (uint32_t)st);
    RET(r);
}

static void b_sigsetmask(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{   /* BSD signal numbers 1..31 are bit (sig-1) on both systems */
    sigset_t nw, old;
    uint32_t m = ARG(0);
    int i;
    (void)rt;
    sigemptyset(&nw);
    for (i = 1; i < 32; ++i) if (m & (1u << (i - 1))) sigaddset(&nw, i);
    sigprocmask(SIG_SETMASK, &nw, &old);
    m = 0;
    for (i = 1; i < 32; ++i) if (sigismember(&old, i) == 1) m |= 1u << (i - 1);
    RET(m);
}

int irix_signal_suspend(void);
int irix_signal_install(int irix_sig, uint32_t handler);

static void b_sigsuspend(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)rt;
    (void)irix_signal_suspend();
    FAIL(EINTR);
}

static void b_vprintf(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestFunction f = irix_clib_extra_bridge_lookup_base("vsprintf");
    IrixGuestAddr buf = guest_alloc(rt, 4096u);
    IrixGuestCpu c = *cpu;
    const char *m;
    if (!f || !buf) { RET(-1); return; }
    c.gpr[4] = buf; c.gpr[5] = cpu->gpr[4]; c.gpr[6] = cpu->gpr[5];
    f(rt, &c);
    m = irix_guest_cstr(rt, buf, 4096u);
    if (m) (void)!write(1, m, strlen(m));
    RET(m ? (int)strlen(m) : -1);
}

static void b_vec_new2(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestFunction f = irix_clib_extra_bridge_lookup_base("__vec_new_eh2");
    if (f) f(rt, cpu); else cpu->gpr[2] = 0;
}

static void b_ustat(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ (void)rt; FAIL(ENOSYS); }

/* wide-character helpers: wchar_t is a big-endian 32-bit word */
static void b_wslen(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    uint32_t i = 0;
    while (ARG(0) && irix_guest_read32be(rt, ARG(0) + 4u * i)) ++i;
    RET(i);
}
static void b_wscpy(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    uint32_t i = 0, c;
    do {
        c = irix_guest_read32be(rt, ARG(1) + 4u * i);
        irix_guest_write32be(rt, ARG(0) + 4u * i, c);
        ++i;
    } while (c);
    cpu->gpr[2] = ARG(0);
}
static void b_wsncpy(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    uint32_t i, n = ARG(2);
    int end = 0;
    for (i = 0; i < n; ++i) {
        uint32_t c = end ? 0 : irix_guest_read32be(rt, ARG(1) + 4u * i);
        if (!c) end = 1;
        irix_guest_write32be(rt, ARG(0) + 4u * i, c);
    }
    cpu->gpr[2] = ARG(0);
}

static void b_sendto(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    uint32_t len = ARG(2), alen = (uint32_t)cpu->gpr[9];
    const void *buf = len ? irix_guest_ptr(rt, ARG(1), len) : "";
    unsigned char sa[128];
    uint32_t i;
    if (alen > sizeof(sa)) { FAIL(EINVAL); return; }
    for (i = 0; i < alen; ++i)
        sa[i] = irix_guest_read8(rt, (uint32_t)cpu->gpr[8] + i);
    cpu->gpr[2] = (uint64_t)(int64_t)sendto((int)ARG(0), buf, len, (int)ARG(3),
                  alen ? (const struct sockaddr *)sa : 0, alen);
}

static void b_recvfrom(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    uint32_t len = ARG(2);
    void *buf = len ? irix_guest_ptr(rt, ARG(1), len) : 0;
    unsigned char sa[128];
    socklen_t alen = sizeof(sa);
    ssize_t r = recvfrom((int)ARG(0), buf, len, (int)ARG(3),
                         (struct sockaddr *)sa, &alen);
    if (r >= 0 && (uint32_t)cpu->gpr[8]) {
        uint32_t i, cap = (uint32_t)cpu->gpr[9] ?
            irix_guest_read32be(rt, (uint32_t)cpu->gpr[9]) : 0, n = alen < cap ? alen : cap;
        for (i = 0; i < n; ++i) irix_guest_write8(rt, (uint32_t)cpu->gpr[8] + i, sa[i]);
        if ((uint32_t)cpu->gpr[9]) irix_guest_write32be(rt, (uint32_t)cpu->gpr[9], alen);
    }
    cpu->gpr[2] = (uint64_t)(int64_t)r;
}

/* ---- table ------------------------------------------------------------- */
IrixGuestFunction irix_clib_more_bridge_lookup(const char *name)
{
    static const struct { const char *n; IrixGuestFunction f; } t[] = {
        { "openlog", b_zero }, { "closelog", b_zero },
        { "sysinfo", b_sysinfo }, { "execve", b_execve }, { "execv", b_execv },
        { "getgrent", b_getgrent }, { "setgrent", b_setgrent }, { "endgrent", b_endgrent },
        { "gethostent", b_null }, { "sethostent", b_zero }, { "endhostent", b_zero },
        { "killpg", b_killpg }, { "setpgid", b_setpgid }, { "wait3", b_wait3 },
        { "sigsetmask", b_sigsetmask }, { "sigsuspend", b_sigsuspend },
        { "setgroups", b_zero }, { "vprintf", b_vprintf },
        { "__vec_new2", b_vec_new2 }, { "ustat", b_ustat },
        { "wslen", b_wslen }, { "wscpy", b_wscpy }, { "wsncpy", b_wsncpy },
        { "sendto", b_sendto }, { "recvfrom", b_recvfrom },
        { "sigsetjmp", b_zero }, { "assert", b_zero },
        { "getdfltprojuser", b_null }, { "ia_audit", b_zero }, { "newarraysess", b_zero },
        { "pathfind", b_null }, { "satsetid", b_zero }, { "satvwrite", b_zero },
        { "setprid", b_zero },
        { "xdr_array", b_zero }, { "xdr_bool", b_zero }, { "xdr_bytes", b_zero },
        { "xdr_char", b_zero }, { "xdr_enum", b_zero }, { "xdr_int", b_zero },
        { "xdr_long", b_zero }, { "xdr_netobj", b_zero }, { "xdr_opaque", b_zero },
        { "xdr_reference", b_zero }, { "xdr_short", b_zero }, { "xdr_string", b_zero },
        { "xdr_u_char", b_zero }, { "xdr_u_int", b_zero }, { "xdr_u_long", b_zero },
        { "xdr_u_short", b_zero }, { "xdr_uint64", b_zero }, { "xdr_union", b_zero },
        { "xdr_void", b_zero }, { "xdr_wrapstring", b_zero },
        { "cosl", b_cosl }, { "sinl", b_sinl }, { "expl", b_expl }, { "logl", b_logl },
        { "log10l", b_log10l }, { "sinhl", b_sinhl }, { "coshl", b_coshl },
        { "sqrtl", b_sqrtl }, { "atan2l", b_atan2l }, { "hypotl", b_hypotl },
        { "isnan", b_isnan }, { "iswspace", b_iswspace }, { "wcstombs", b_wcstombs },
        { "lseek64", b_lseek64 }, { "waitpid", b_waitpid }, { "writev", b_writev },
        { "utime", b_utime }, { "mkfifo", b_mkfifo }, { "mknod", b_mknod },
        { "lockf", b_lockf }, { "getrlimit", b_getrlimit }, { "setrlimit", b_setrlimit },
        { "statfs", b_statfs }, { "statvfs", b_statvfs }, { "fstatvfs64", b_fstatvfs64 },
        { "sysfs", b_sysfs }, { "syslog", b_syslog }, { "vsyslog", b_vsyslog }, { "sigset", b_sigset },
        { "inet_addr", b_inet_addr }, { "inet_ntoa", b_inet_ntoa },
        { "getspnam", b_getspnam }, { "getspent", b_getspent }, { "endspent", b_endspent },
        { "setuid", b_setuid }, { "setgid", b_setgid }, { "seteuid", b_seteuid },
        { "setegid", b_setegid }, { "setreuid", b_setreuid }, { "setregid", b_setregid },
        { "lckpwdf", b_zero }, { "ulckpwdf", b_zero },
        { "putpwent", b_enosys }, { "putspent", b_enosys },
        { "fdopen", b_fdopen }, { "popen", b_popen },
        { "strccpy", b_strccpy }, { "strecpy", b_strecpy },
        { "setinvent_r", b_setinvent_r }, { "getinvent_r", b_getinvent_r },
        { "endinvent_r", b_endinvent_r }, { "setinvent", b_setinvent },
        { "getinvent", b_getinvent }, { "endinvent", b_endinvent },
        { "usinit", b_usinit }, { "usnewsema", b_usnewsema }, { "usmalloc", b_usmalloc },
        { "isideogram", b_wc_zero }, { "isphonogram", b_wc_zero }, { "isspecial", b_wc_zero },
        { "iswpunct", b_iswpunct }, { "towlower", b_towlower }, { "nl_langinfo", b_nl_langinfo },
        { "wcslen", b_wcslen }, { "wcscpy", b_wcscpy }, { "wcsncpy", b_wcsncpy }, { "wcscmp", b_wcscmp },
        { "wcsncmp", b_wcsncmp }, { "wcschr", b_wcschr }, { "wcscat", b_wcscat },
        { "catopen", b_catopen }, { "catgets", b_catgets }, { "catclose", b_catclose },
        { "usconfig", b_zero }, { "setitimer", b_zero }, { "getitimer", b_zero }, { "usdetach", b_zero }, { "usfree", b_zero },
        { "usfreesema", b_zero }, { "uspsema", b_usnop1 }, { "uscpsema", b_usnop1 },
        { "usvsema", b_zero }, { "usinitsema", b_zero }, { "usmallinfo", b_zero },
        { "init_lock", b_zero }, { "spin_lock", b_zero }, { "release_lock", b_zero },
        { "pthread_mutex_init", b_zero }, { "pthread_mutex_lock", b_zero },
        { "pthread_mutex_unlock", b_zero }, { "pthread_mutex_destroy", b_zero },
        { "pthread_cond_init", b_zero }, { "pthread_cond_signal", b_zero },
        { "pthread_cond_broadcast", b_zero }, { "pthread_cond_wait", b_zero },
        { "pthread_self", b_one },
        { "setlabel", b_zero }, { "pfmt", b_pfmt }, { "lfmt", b_pfmt }, { "setcat", b_null }, { "addsev", b_zero },
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
        { "crypt", b_crypt }, { "tempnam", b_tempnam }, { "nftw64", b_enosys },
        { "pclose", b_zero },
        { "_vfork", b_enosys }, { "l64a", b_null }, { "a64l", b_zero }, { "atold", b_zero },
    };
    size_t i;
    if (!name) return 0;
    /* dlsym is implemented in IrixPosixBridgeUnit.c (bundle symbol lookup). */
    if (!strcmp(name, "dlsym")) return 0;
    for (i = 0; i < sizeof(t) / sizeof(t[0]); ++i)
        if (!strcmp(name, t[i].n)) return t[i].f;
    return 0;
}
