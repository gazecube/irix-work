/*
 * irixlibc32.c - the IRIX libc/libC bits that lifted N32 programs (lift_dso.py output, native i386) call, on top of glibc.
 *
 * Linked into the program together with the lifted code.  Names that differ between IRIX and glibc (struct layouts,
 * flag values, errno, signal numbers) are redefined in the lifted objects with objcopy --redefine-syms (map in
 * irixlibc32.map: open -> irix_open, ...), everything else binds to glibc directly.
 *
 * Calling convention of lifted code: plain cdecl; 64-bit values travel as big-endian word pairs (high word first) and
 * floating-point arguments/results through lift_fpa[]/lift_fpr (see lift_rt.h).
 *
 * Build: gcc -m32 -O1 -w -c irixlibc32.c
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <pwd.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/times.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

extern uint64_t lift_fpa[8];
extern uint64_t lift_fpr;

static void gap(const char *what)
{
    fprintf(stderr, "irixlibc32: %s is not implemented\n", what);
    abort();
}

/* ---- errno ------------------------------------------------------------------------------------------------ */
/* IRIX errno values match Linux for the ones programs test (EINTR 4, EAGAIN 11, ECHILD 10, ENOTTY 25, ...). */
int irix_errno;
#define SETERR() (irix_errno = errno)

/* ---- C++ runtime (libC): new/delete, RTTI stubs --------------------------------------------------------------- */
void *__nw__GUi(unsigned n) { void *p = malloc(n ? n : 1); if (!p) { fputs("out of memory\n", stderr); abort(); } return p; }
void *__nwa__GUi(unsigned n) { return __nw__GUi(n); }
void __dl__GPv(void *p) { free(p); }
void __dla__GPv(void *p) { free(p); }
void __pure_virtual_called(void) { fputs("pure virtual function called\n", stderr); abort(); }
void __stack_chk_fail_local(void) { fputs("stack smashing detected\n", stderr); abort(); }
void *__vtbl__9type_info[16];
char __T_9__nothrow[16];

/* ---- libc data -------------------------------------------------------------------------------------------- */
/* __libc_attr (<locale_attr.h>): the ctype macros read __libc_attr._ctype_tbl->_class[c + 1] (257 class words, then the
 * 257-entry lower/upper conversion tables).  Built for the C locale at startup. */
struct ictype {
    unsigned int _class[257];
    short _lower[257];
    short _upper[257];
    char _cswidth[7];
    char _fill[1];
};
static struct ictype ictype_tbl;
struct { struct ictype *_ctype_tbl; unsigned char _eucwidth[3], _scrwidth[3], _mb_cur_max; int _is_euc; void *_isset2, *_isset3, *_iscodeset;
         unsigned char _coll_as_cmp; void *_fill[15]; } __libc_attr;

__attribute__((constructor(101))) static void irixlibc_init(void)
{
    int c;
    for (c = 0; c < 256; c++) {
        unsigned f = 0;
        if (c >= 'A' && c <= 'Z') f = 0x1 | 0x4000 | 0x8000 | 0x40000000;
        else if (c >= 'a' && c <= 'z') f = 0x2 | 0x4000 | 0x8000 | 0x40000000;
        else if (c >= '0' && c <= '9') f = 0x4 | 0x8000 | 0x40000000;
        else if (c == ' ') f = 0x8 | 0x80000000u | 0x8000;
        else if (c == '\t') f = 0x8 | 0x80000000u | 0x20;
        else if (c == '\n' || c == '\v' || c == '\f' || c == '\r') f = 0x8 | 0x20;
        else if (c < 32 || c == 127) f = 0x20;
        else if (c < 127) f = 0x10 | 0x8000 | 0x40000000;
        if ((c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f') || (c >= '0' && c <= '9')) f |= 0x80;
        ictype_tbl._class[c + 1] = f;
        ictype_tbl._lower[c + 1] = (short)((c >= 'A' && c <= 'Z') ? c + 32 : c);
        ictype_tbl._upper[c + 1] = (short)((c >= 'a' && c <= 'z') ? c - 32 : c);
    }
    ictype_tbl._lower[0] = ictype_tbl._upper[0] = -1;
    ictype_tbl._cswidth[0] = 1;
    __libc_attr._ctype_tbl = &ictype_tbl;
    __libc_attr._mb_cur_max = 1;
}
int __us_rsthread_stdio;

/* ---- stdio: IRIX FILE is 16 bytes, getc/putc/feof/ferror/fileno are macros over its fields ------------------------- */
struct ifile { int _cnt; unsigned char *_ptr; unsigned char *_base; unsigned char _flag; unsigned char _file; unsigned char pad[2]; };
#define I_EOF 0020
#define I_ERR 0040
#define I_READ 0001
#define I_WRT 0002
struct ifile __iob[3] = { { 0, 0, 0, I_READ, 0 }, { 0, 0, 0, I_WRT, 1 }, { 0, 0, 0, I_WRT | 0004, 2 } };

struct ifile_x { struct ifile f; FILE *host; };      /* allocated by fopen; the host stream sits after the 16 bytes */

static FILE *host_of(struct ifile *f)
{
    if (f >= __iob && f < __iob + 3)
        return f == &__iob[0] ? stdin : f == &__iob[1] ? stdout : stderr;
    return ((struct ifile_x *)f)->host;
}

static void sync_flags(struct ifile *f, FILE *h)
{
    f->_cnt = 0;
    f->_flag &= ~(I_EOF | I_ERR);
    if (feof(h)) f->_flag |= I_EOF;
    if (ferror(h)) f->_flag |= I_ERR;
}

struct ifile *irix_fopen(const char *path, const char *mode)
{
    FILE *h = fopen(path, mode);
    struct ifile_x *x;
    if (!h) { SETERR(); return 0; }
    x = calloc(1, sizeof *x);
    x->host = h;
    x->f._file = (unsigned char)fileno(h);
    x->f._flag = (mode[0] == 'r' ? I_READ : I_WRT);
    return &x->f;
}

int irix_fclose(struct ifile *f)
{
    int r;
    if (!f) return -1;
    if (f >= __iob && f < __iob + 3) return fflush(host_of(f));
    r = fclose(host_of(f));
    SETERR();
    free(f);
    return r;
}

int __filbuf(struct ifile *f)
{
    FILE *h = host_of(f);
    int c = fgetc(h);
    sync_flags(f, h);
    return c;
}
int __semgetc(struct ifile *f) { return __filbuf(f); }

int __flsbuf(int c, struct ifile *f)
{
    FILE *h = host_of(f);
    int r = fputc(c & 0xff, h);
    sync_flags(f, h);
    return r == EOF ? EOF : (c & 0xff);
}
int __semputc(int c, struct ifile *f) { return __flsbuf(c, f); }

int irix_fgetc(struct ifile *f) { return __filbuf(f); }
int irix_fflush(struct ifile *f) { FILE *h; int r; if (!f) return fflush(0); h = host_of(f); r = fflush(h); sync_flags(f, h); return r; }
int irix_fputc(int c, struct ifile *f) { return __flsbuf(c, f); }
unsigned irix_fwrite(const void *b, unsigned sz, unsigned n, struct ifile *f) { FILE *h = host_of(f); unsigned r = fwrite(b, sz, n, h); sync_flags(f, h); return r; }
unsigned irix_fread(void *b, unsigned sz, unsigned n, struct ifile *f) { FILE *h = host_of(f); unsigned r = fread(b, sz, n, h); sync_flags(f, h); return r; }
int irix_ungetc(int c, struct ifile *f) { FILE *h = host_of(f); int r = ungetc(c, h); sync_flags(f, h); return r; }
char *irix_fgets(char *b, int n, struct ifile *f) { FILE *h = host_of(f); char *r = fgets(b, n, h); sync_flags(f, h); return r; }
int irix_fputs(const char *s, struct ifile *f) { FILE *h = host_of(f); int r = fputs(s, h); sync_flags(f, h); return r; }
int irix_fprintf(struct ifile *f, const char *fmt, ...)
{
    FILE *h = host_of(f);
    va_list ap;
    int r;
    va_start(ap, fmt);
    r = vfprintf(h, fmt, ap);
    va_end(ap);
    sync_flags(f, h);
    return r;
}
void irix_perror(const char *s)
{
    if (s && *s) fprintf(stderr, "%s: ", s);
    fprintf(stderr, "%s\n", strerror(irix_errno));
}
char *irix_strerror(int e) { return strerror(e); }

/* ---- printf family on IRIX N32 va_list: 8-byte slots, a 32-bit value sits in the low (second) word ------------------- */
static int n32_vformat(char *out, size_t cap, const char *fmt, const unsigned char *ap)
{
    size_t o = 0;
    char spec[64], tmp[512];
#define PUT(s) do { const char *s_ = (s); size_t l_ = strlen(s_); if (out && o < cap) { size_t c_ = l_ < cap - o ? l_ : cap - o; memcpy(out + o, s_, c_); } o += l_; } while (0)
    while (*fmt) {
        if (*fmt != '%') { char c[2] = { *fmt++, 0 }; PUT(c); continue; }
        {
            const char *p = fmt + 1;
            size_t sl = 0, sc = 0;
            int lcount = 0, isfloat = 0, ll = 0;
            spec[sl++] = '%';
            while (*p && strchr("-+ #0", *p)) spec[sl++] = *p++;
            if (*p == '*') { int w = *(int *)(ap + 4); ap += 8; sl += sprintf(spec + sl, "%d", w); p++; }
            else while (*p >= '0' && *p <= '9') spec[sl++] = *p++;
            if (*p == '.') {
                spec[sl++] = *p++;
                if (*p == '*') { int w = *(int *)(ap + 4); ap += 8; sl += sprintf(spec + sl, "%d", w); p++; }
                else while (*p >= '0' && *p <= '9') spec[sl++] = *p++;
            }
            while (*p == 'l' || *p == 'h' || *p == 'L' || *p == 'q') { if (*p == 'l' || *p == 'L' || *p == 'q') lcount++; p++; }
            ll = lcount >= 2;
            sc = *p;
            if (!sc) break;
            p++;
            if (sc == '%') { PUT("%"); fmt = p; continue; }
            if (strchr("diouxXc", sc)) {
                if (ll) {
                    long long v = ((long long)*(uint32_t *)ap << 32) | *(uint32_t *)(ap + 4);
                    ap += 8;
                    spec[sl++] = 'l'; spec[sl++] = 'l'; spec[sl++] = (char)sc; spec[sl] = 0;
                    snprintf(tmp, sizeof tmp, spec, v);
                } else {
                    int v = *(int *)(ap + 4);
                    ap += 8;
                    spec[sl++] = (char)sc; spec[sl] = 0;
                    snprintf(tmp, sizeof tmp, spec, v);
                }
            } else if (strchr("eEfgGaA", sc)) {
                double d;
                memcpy(&d, ap, 8);
                ap += 8;
                spec[sl++] = (char)sc; spec[sl] = 0;
                snprintf(tmp, sizeof tmp, spec, d);
            } else if (sc == 's') {
                const char *s = (const char *)(uintptr_t)*(uint32_t *)(ap + 4);
                ap += 8;
                spec[sl++] = 's'; spec[sl] = 0;
                snprintf(tmp, sizeof tmp, spec, s ? s : "(null)");
            } else if (sc == 'p') {
                void *v = (void *)(uintptr_t)*(uint32_t *)(ap + 4);
                ap += 8;
                spec[sl++] = 'p'; spec[sl] = 0;
                snprintf(tmp, sizeof tmp, spec, v);
            } else {
                snprintf(tmp, sizeof tmp, "%%%c", (int)sc);
            }
            PUT(tmp);
            (void)lcount; (void)isfloat;
            fmt = p;
        }
    }
    if (out && cap) out[o < cap ? o : cap - 1] = 0;
    return (int)o;
#undef PUT
}
int irix_vsprintf(char *b, const char *fmt, const unsigned char *ap) { return n32_vformat(b, (size_t)-1 >> 1, fmt, ap); }
int irix_vsnprintf(char *b, unsigned n, const char *fmt, const unsigned char *ap) { return n32_vformat(b, n, fmt, ap); }

/* ---- open/ioctl/stat/... --------------------------------------------------------------------------------------- */
static int open_flags(int f)
{
    int o = f & 3;
    if (f & 0x04) o |= O_NONBLOCK;
    if (f & 0x08) o |= O_APPEND;
    if (f & 0x10) o |= O_SYNC;
    if (f & 0x80) o |= O_NONBLOCK;
    if (f & 0x100) o |= O_CREAT;
    if (f & 0x200) o |= O_TRUNC;
    if (f & 0x400) o |= O_EXCL;
    if (f & 0x800) o |= O_NOCTTY;
    return o;
}
int irix_open(const char *p, int flags, int mode)
{
    int r = open(p, open_flags(flags), mode);
    SETERR();
    return r;
}

enum { IV_INTR = 0, IV_QUIT = 1, IV_ERASE = 2, IV_KILL = 3, IV_EOF = 4, IV_EOL = 5, IV_EOL2 = 6, IV_SWTCH = 7,
       IV_START = 8, IV_STOP = 9, IV_SUSP = 10, IV_DSUSP = 11, IV_REPRINT = 12, IV_DISCARD = 13, IV_WERASE = 14,
       IV_LNEXT = 15, IV_NCCS = 23 };

static uint32_t lflag_from_host(tcflag_t l)
{
    uint32_t o = l & (ISIG | ICANON | XCASE | ECHO | ECHOE | ECHOK | ECHONL | NOFLSH);
    if (l & IEXTEN) o |= 0000400u;
    if (l & TOSTOP) o |= 0001000u;
    return o;
}
static tcflag_t host_lflag(uint32_t l)
{
    tcflag_t o = (tcflag_t)(l & (ISIG | ICANON | XCASE | ECHO | ECHOE | ECHOK | ECHONL | NOFLSH));
    if (l & 0000400u) o |= IEXTEN;
    if (l & 0001000u) o |= TOSTOP;
    return o;
}

static int termio_get(int fd, unsigned char *a, int with_speeds, int with_line)
{
    struct termios t;
    uint8_t cc[IV_NCCS];
    unsigned o = 16, i;
    if (tcgetattr(fd, &t) != 0) return -1;
    memset(cc, 0, sizeof cc);
    cc[IV_INTR] = t.c_cc[VINTR]; cc[IV_QUIT] = t.c_cc[VQUIT]; cc[IV_ERASE] = t.c_cc[VERASE]; cc[IV_KILL] = t.c_cc[VKILL];
    if (t.c_lflag & ICANON) { cc[IV_EOF] = t.c_cc[VEOF]; cc[IV_EOL] = t.c_cc[VEOL]; }
    else { cc[IV_EOF] = t.c_cc[VMIN]; cc[IV_EOL] = t.c_cc[VTIME]; }
    cc[IV_EOL2] = t.c_cc[VEOL2]; cc[IV_SWTCH] = t.c_cc[VSWTC]; cc[IV_START] = t.c_cc[VSTART]; cc[IV_STOP] = t.c_cc[VSTOP];
    cc[IV_SUSP] = t.c_cc[VSUSP]; cc[IV_DSUSP] = 0x19; cc[IV_REPRINT] = t.c_cc[VREPRINT]; cc[IV_DISCARD] = t.c_cc[VDISCARD];
    cc[IV_WERASE] = t.c_cc[VWERASE]; cc[IV_LNEXT] = t.c_cc[VLNEXT];
    *(uint32_t *)(a + 0) = t.c_iflag & 0x7fffu;
    *(uint32_t *)(a + 4) = t.c_oflag & 0x7fffu;
    *(uint32_t *)(a + 8) = t.c_cflag & 0x7fffu;
    *(uint32_t *)(a + 12) = lflag_from_host(t.c_lflag);
    if (with_speeds) {
        uint32_t sp = t.c_cflag & 017u;
        if (!sp) sp = cfgetospeed(&t) & 017u;
        *(uint32_t *)(a + o) = sp; *(uint32_t *)(a + o + 4) = sp;
        o += 8;
    }
    if (with_line) a[o++] = t.c_line;
    for (i = 0; i < IV_NCCS; i++) a[o + i] = cc[i];
    return 0;
}

static int termio_set(int fd, const unsigned char *a, int with_speeds, int with_line, int action)
{
    struct termios t;
    uint8_t cc[IV_NCCS];
    uint32_t lflag;
    unsigned o = 16, i;
    if (tcgetattr(fd, &t) != 0) return -1;
    t.c_iflag = (t.c_iflag & ~0x7fffu) | (*(const uint32_t *)(a + 0) & 0x7fffu);
    t.c_oflag = (t.c_oflag & ~0x7fffu) | (*(const uint32_t *)(a + 4) & 0x7fffu);
    t.c_cflag = (t.c_cflag & ~0x7fffu) | (*(const uint32_t *)(a + 8) & 0x7fffu);
    lflag = *(const uint32_t *)(a + 12);
    t.c_lflag = (t.c_lflag & ~(ISIG | ICANON | XCASE | ECHO | ECHOE | ECHOK | ECHONL | NOFLSH | IEXTEN | TOSTOP)) | host_lflag(lflag);
    if (with_speeds) o = 24;
    if (with_line) o += 1;
    for (i = 0; i < IV_NCCS; i++) cc[i] = a[o + i];
    t.c_cc[VINTR] = cc[IV_INTR]; t.c_cc[VQUIT] = cc[IV_QUIT]; t.c_cc[VERASE] = cc[IV_ERASE]; t.c_cc[VKILL] = cc[IV_KILL];
    if (lflag & ICANON) { t.c_cc[VEOF] = cc[IV_EOF]; t.c_cc[VEOL] = cc[IV_EOL]; }
    else { t.c_cc[VMIN] = cc[IV_EOF]; t.c_cc[VTIME] = cc[IV_EOL]; }
    t.c_cc[VEOL2] = cc[IV_EOL2]; t.c_cc[VSWTC] = cc[IV_SWTCH]; t.c_cc[VSTART] = cc[IV_START]; t.c_cc[VSTOP] = cc[IV_STOP];
    t.c_cc[VSUSP] = cc[IV_SUSP]; t.c_cc[VREPRINT] = cc[IV_REPRINT]; t.c_cc[VDISCARD] = cc[IV_DISCARD];
    t.c_cc[VWERASE] = cc[IV_WERASE]; t.c_cc[VLNEXT] = cc[IV_LNEXT];
    return tcsetattr(fd, action, &t);
}

int irix_ioctl(int fd, unsigned cmd, void *arg)
{
    int r;
    switch (cmd) {
    case 0x5401u: case 0x54c9u: r = termio_get(fd, arg, cmd == 0x54c9u, 1); break;                /* TCGETA */
    case 0x5402u: case 0x54cau: r = termio_set(fd, arg, cmd == 0x54cau, 1, TCSANOW); break;       /* TCSETA */
    case 0x5403u: case 0x54cbu: r = termio_set(fd, arg, cmd == 0x54cbu, 1, TCSADRAIN); break;     /* TCSETAW */
    case 0x5404u: case 0x54ccu: r = termio_set(fd, arg, cmd == 0x54ccu, 1, TCSAFLUSH); break;     /* TCSETAF */
    case 0x540du: case 0x54d5u: r = termio_get(fd, arg, cmd == 0x54d5u, 0); break;                /* TCGETS */
    case 0x540eu: case 0x54d6u: r = termio_set(fd, arg, cmd == 0x54d6u, 0, TCSANOW); break;
    case 0x540fu: case 0x54d7u: r = termio_set(fd, arg, cmd == 0x54d7u, 0, TCSADRAIN); break;
    case 0x5410u: case 0x54d8u: r = termio_set(fd, arg, cmd == 0x54d8u, 0, TCSAFLUSH); break;
    case 0x5407u: r = tcflush(fd, (uintptr_t)arg == 0 ? TCIFLUSH : (uintptr_t)arg == 1 ? TCOFLUSH : TCIOFLUSH); break;
    case 0x5405u: r = (uintptr_t)arg ? tcdrain(fd) : tcsendbreak(fd, 0); break;                     /* TCSBRK */
    case 0x40087468u: {                                                                         /* TIOCGWINSZ */
        struct winsize ws;
        unsigned short *w = arg;
        if ((r = ioctl(fd, TIOCGWINSZ, &ws)) == 0) { w[0] = ws.ws_row; w[1] = ws.ws_col; w[2] = ws.ws_xpixel; w[3] = ws.ws_ypixel; }
        break;
    }
    case 0x80087467u: {                                                                         /* TIOCSWINSZ */
        struct winsize ws;
        unsigned short *w = arg;
        ws.ws_row = w[0]; ws.ws_col = w[1]; ws.ws_xpixel = w[2]; ws.ws_ypixel = w[3];
        r = ioctl(fd, TIOCSWINSZ, &ws);
        break;
    }
    default:
        fprintf(stderr, "irixlibc32: ioctl 0x%x on fd %d not translated\n", cmd, fd);
        errno = EINVAL;
        r = -1;
        break;
    }
    SETERR();
    return r;
}

static void put64(unsigned char *p, uint64_t v) { *(uint32_t *)p = (uint32_t)(v >> 32); *(uint32_t *)(p + 4) = (uint32_t)v; }
static int fill_stat(unsigned char *o, const struct stat *st)
{
    memset(o, 0, 152);
    *(uint32_t *)(o + 0) = (uint32_t)st->st_dev;
    put64(o + 16, st->st_ino);
    *(uint32_t *)(o + 24) = st->st_mode;
    *(uint32_t *)(o + 28) = st->st_nlink;
    *(uint32_t *)(o + 32) = st->st_uid;
    *(uint32_t *)(o + 36) = st->st_gid;
    *(uint32_t *)(o + 40) = (uint32_t)st->st_rdev;
    put64(o + 56, (uint64_t)st->st_size);
    *(uint32_t *)(o + 68) = (uint32_t)st->st_atim.tv_sec; *(uint32_t *)(o + 72) = (uint32_t)st->st_atim.tv_nsec;
    *(uint32_t *)(o + 76) = (uint32_t)st->st_mtim.tv_sec; *(uint32_t *)(o + 80) = (uint32_t)st->st_mtim.tv_nsec;
    *(uint32_t *)(o + 84) = (uint32_t)st->st_ctim.tv_sec; *(uint32_t *)(o + 88) = (uint32_t)st->st_ctim.tv_nsec;
    *(uint32_t *)(o + 92) = (uint32_t)st->st_blksize;
    put64(o + 96, (uint64_t)st->st_blocks);
    memcpy(o + 104, "efs", 4);
    return 0;
}
int irix_stat(const char *p, unsigned char *buf) { struct stat st; int r = stat(p, &st); SETERR(); return r ? r : fill_stat(buf, &st); }
int irix_fstat(int fd, unsigned char *buf) { struct stat st; int r = fstat(fd, &st); SETERR(); return r ? r : fill_stat(buf, &st); }

int irix_read(int fd, void *b, unsigned n) { int r = (int)read(fd, b, n); SETERR(); return r; }
int irix_write(int fd, const void *b, unsigned n) { int r = (int)write(fd, b, n); SETERR(); return r; }
int irix_close(int fd) { int r = close(fd); SETERR(); return r; }
int irix_dup(int fd) { int r = dup(fd); SETERR(); return r; }
int irix_access(const char *p, int m) { int r = access(p, m); SETERR(); return r; }
int irix_chmod(const char *p, unsigned m) { int r = chmod(p, m); SETERR(); return r; }
int irix_chown(const char *p, int u, int g) { int r = chown(p, u, g); SETERR(); return r; }
int irix_select(int n, fd_set *r, fd_set *w, fd_set *e, struct timeval *t) { int x = select(n, r, w, e, t); SETERR(); return x; }
int irix_wait(int *st) { int r = wait(st); SETERR(); return r; }
int irix_isatty(int fd) { int r = isatty(fd); SETERR(); return r; }
int irix_execvp(const char *f, char *const *argv) { int r = execvp(f, argv); SETERR(); return r; }

/* ---- process / pty ------------------------------------------------------------------------------------------- */
int getdtablehi(void) { long n = sysconf(_SC_OPEN_MAX); return n > 1024 ? 1024 : (int)n; }
int sginap(long ticks) { usleep((useconds_t)ticks * 10000u); return 0; }

char *_getpty(int *fildes, int oflag, unsigned mode, int nofork)
{
    static char name[64];
    int m = posix_openpt(O_RDWR | O_NOCTTY | (open_flags(oflag) & O_NONBLOCK));
    (void)mode; (void)nofork;
    if (m < 0 || grantpt(m) < 0 || unlockpt(m) < 0 || ptsname_r(m, name, sizeof name) != 0) {
        SETERR();
        if (m >= 0) close(m);
        return 0;
    }
    *fildes = m;
    return name;
}

/* utmpx: the login records are not kept */
void setutxent(void) {}
void endutxent(void) {}
void *pututxline(const void *u) { (void)u; return 0; }
void updwtmpx(const char *f, const void *u) { (void)f; (void)u; }

/* ---- passwd ---------------------------------------------------------------------------------------------- */
struct ipasswd { char *pw_name, *pw_passwd; int pw_uid, pw_gid; char *pw_age, *pw_comment, *pw_gecos, *pw_dir, *pw_shell; };
static struct ipasswd ipw;
static struct ipasswd *conv_pw(struct passwd *p)
{
    if (!p) return 0;
    memset(&ipw, 0, sizeof ipw);
    ipw.pw_name = p->pw_name; ipw.pw_passwd = p->pw_passwd; ipw.pw_uid = (int)p->pw_uid; ipw.pw_gid = (int)p->pw_gid;
    ipw.pw_age = ""; ipw.pw_comment = ""; ipw.pw_gecos = p->pw_gecos; ipw.pw_dir = p->pw_dir; ipw.pw_shell = p->pw_shell;
    return &ipw;
}
struct ipasswd *irix_getpwnam(const char *n) { return conv_pw(getpwnam(n)); }
struct ipasswd *irix_getpwuid(int u) { return conv_pw(getpwuid((uid_t)u)); }

/* ---- signals: IRIX numbers <-> Linux, handlers run through a trampoline ---------------------------------------------- */
static const int i2l[32] = { 0, SIGHUP, SIGINT, SIGQUIT, SIGILL, SIGTRAP, SIGABRT, 0, SIGFPE, SIGKILL, SIGBUS, SIGSEGV, SIGSYS,
                             SIGPIPE, SIGALRM, SIGTERM, SIGUSR1, SIGUSR2, SIGCHLD, SIGPWR, SIGWINCH, SIGURG, SIGIO, SIGSTOP,
                             SIGTSTP, SIGCONT, SIGTTIN, SIGTTOU, SIGVTALRM, SIGPROF, SIGXCPU, SIGXFSZ };
static int l2i[65];
static void (*ihandler[32])(int);
static void sigmap_init(void)
{
    static int done;
    int k;
    if (done) return;
    done = 1;
    for (k = 1; k < 32; k++) if (i2l[k]) l2i[i2l[k]] = k;
}
/* a trampoline per IRIX signal number: it receives the Linux number, finds the IRIX one and calls the lifted handler */
#define IT(n) static void it##n(int s) { (void)s; if (ihandler[n]) ihandler[n](n); }
IT(1) IT(2) IT(3) IT(4) IT(5) IT(6) IT(7) IT(8) IT(9) IT(10) IT(11) IT(12) IT(13) IT(14) IT(15) IT(16)
IT(17) IT(18) IT(19) IT(20) IT(21) IT(22) IT(23) IT(24) IT(25) IT(26) IT(27) IT(28) IT(29) IT(30) IT(31)
static void (*const itramps[32])(int) = { 0, it1, it2, it3, it4, it5, it6, it7, it8, it9, it10, it11, it12, it13, it14, it15, it16,
                                          it17, it18, it19, it20, it21, it22, it23, it24, it25, it26, it27, it28, it29, it30, it31 };

void *irix_signal(int isig, void *h)
{
    struct sigaction sa, old;
    void *prev;
    int ls;
    sigmap_init();
    if (isig <= 0 || isig > 31 || !i2l[isig]) { errno = EINVAL; SETERR(); return (void *)-1; }
    ls = i2l[isig];
    prev = (void *)ihandler[isig];
    memset(&sa, 0, sizeof sa);
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (h == 0) { sa.sa_handler = SIG_DFL; ihandler[isig] = 0; }
    else if (h == (void *)1) { sa.sa_handler = SIG_IGN; ihandler[isig] = 0; }
    else { ihandler[isig] = (void (*)(int))h; sa.sa_handler = itramps[isig]; }
    sigaction(ls, &sa, &old);
    SETERR();
    if (prev == 0 && old.sa_handler == SIG_IGN) prev = (void *)1;
    return prev;
}
int irix_sighold(int isig) { sigset_t s; sigmap_init(); sigemptyset(&s); if (isig > 0 && isig < 32 && i2l[isig]) sigaddset(&s, i2l[isig]); return sigprocmask(SIG_BLOCK, &s, 0); }
int irix_sigrelse(int isig) { sigset_t s; sigmap_init(); sigemptyset(&s); if (isig > 0 && isig < 32 && i2l[isig]) sigaddset(&s, i2l[isig]); return sigprocmask(SIG_UNBLOCK, &s, 0); }

/* ---- locale / X specifics ---------------------------------------------------------------------------------- */
void *_sgiGetLocaleCodesets(const char *locale) { (void)locale; return 0; }
void _XSGIAddToPreloadAtomList(void *dpy, const char *name) { (void)dpy; (void)name; }

/* ---- floating point through the side channel ------------------------------------------------------------------ */
static double fpa_d(int k) { double d; memcpy(&d, &lift_fpa[k], 8); return d; }
static void fpr_d(double d) { memcpy(&lift_fpr, &d, 8); }
int irix_sqrt(void) { fpr_d(sqrt(fpa_d(0))); return 0; }
int irix_floorf(void) { uint32_t u = (uint32_t)lift_fpa[0]; float x, r; memcpy(&x, &u, 4); r = floorf(x); memcpy(&u, &r, 4); lift_fpr = u; return 0; }
int irix_atof(const char *s) { fpr_d(atof(s)); return 0; }

/* ---- libC iostream init object (nothing here uses iostreams) and the IRIX loader's DSO hook -------------------------- */
void *__ct__13Iostream_initGv(void *self) { return self; }
void *__dt__13Iostream_initGv(void *self, int flags) { (void)flags; return self; }
void _dsm_crt_init(void) {}
