/* IRIX libc and SGI Xlib layer for N64 code lifted to x86_64 (lift_n64.py), over glibc and the host libX11.
 *
 * Lifted code keeps the IRIX struct layouts and constants it was compiled with.  N64 is LP64, so most of libc
 * can be called directly; this file holds what differs:
 *   - stdio: the IRIX FILE (32 bytes, getc/putc are macros over its fields) and __iob;
 *   - errno as a variable, __oserror();
 *   - struct stat, struct passwd, BSD struct dirent;
 *   - the ctype table behind __libc_attr;
 *   - SGI Xlib entry points: FAST_QUARKS names (resource names passed as small negative numbers), the fast atom
 *     table, the NumLock modifier query.
 * The build renames the lifted objects' references to the wrapped libc functions to irix_<name>
 * (objcopy --redefine-syms), so native code in the same process keeps the real ones.
 */
#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <malloc.h>
#include <sys/mman.h>
#include <dlfcn.h>
#include <errno.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/Xresource.h>
#include <X11/Xutil.h>
#include <wchar.h>
#include <X11/keysym.h>

/* ---- width shadow: one byte per 8-byte memory cell, set while the cell holds one 64-bit value written by lifted code
 *      (see lift_n64.py).  The whole user address space maps into 16 TB of lazily allocated memory. ------------------------- */
#include "../../../irix-re/recomp/tools/lift64_mem.h"

__attribute__((constructor(101))) static void shadow_init(void)
{
    void *p = mmap(SH_BASE, 1ull << 44, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);
    if (p != (void *)SH_BASE) {
        fprintf(stderr, "libc.so.1: cannot map the width shadow at %p\n", (void *)SH_BASE);
        abort();
    }
}
static void sh_clear(const void *p, size_t n)
{
    if (p && n) memset(&SH(p), 0, ((uintptr_t)p + n + 7) / 8 - (uintptr_t)p / 8);
}

/* memory handed out to lifted code starts unmarked, and marks do not outlive the block */
void *irix_malloc(size_t n) { void *p = malloc(n); if (p) sh_clear(p, malloc_usable_size(p)); return p; }
void *irix_calloc(size_t a, size_t b) { void *p = calloc(a, b); if (p) sh_clear(p, malloc_usable_size(p)); return p; }
void irix_free(void *p) { if (p) sh_clear(p, malloc_usable_size(p)); free(p); }
void *irix_realloc(void *p, size_t n)
{
    size_t old = p ? malloc_usable_size(p) / 8 : 0, keep;
    unsigned char *save = old ? malloc(old) : 0;
    void *q;
    if (save) { memcpy(save, &SH(p), old); memset(&SH(p), 0, old); }
    q = realloc(p, n);
    if (q) {
        sh_clear(q, malloc_usable_size(q));
        keep = malloc_usable_size(q) / 8 < old ? malloc_usable_size(q) / 8 : old;
        if (save && !((uintptr_t)q & 7)) memcpy(&SH(q), save, keep);
    } else if (save && n) {
        memcpy(&SH(p), save, old);              /* the old block is still there */
    }
    free(save);
    return q;
}
/* Block operations follow the states.  Whole cells move with their shadow when source and destination are aligned
 * alike (a structure keeps its native layout); anything else goes byte by byte through the state-aware accessors,
 * which is what the big-endian machine's memcpy does.  Memory that lifted code never wrote takes the native path. */
static int sh_clean(const void *p, size_t n)
{
    const unsigned char *a = &SH(p), *e = &SH((const char *)p + n - 1) + 1;
    if (!n) return 1;
    if (e - a <= 16) { while (a < e) if (*a++) return 0; return 1; }
    return a[0] == 0 && !memcmp(a, a + 1, (size_t)(e - a) - 1);
}
static void bytes_fwd(unsigned char *d, const unsigned char *s, size_t n) { size_t i; for (i = 0; i < n; i++) ST1((uintptr_t)(d + i), LD1((uintptr_t)(s + i))); }
static void bytes_back(unsigned char *d, const unsigned char *s, size_t n) { while (n--) ST1((uintptr_t)(d + n), LD1((uintptr_t)(s + n))); }
static void *lm_move(void *dv, const void *sv, size_t n)
{
    unsigned char *d = dv;
    const unsigned char *s = sv;
    size_t head, mid;
    if (!n || d == s) return dv;
    if (sh_clean(s, n) && sh_clean(d, n)) return memmove(dv, sv, n);
    if ((((uintptr_t)d ^ (uintptr_t)s) & 7) || n < 8) {
        if (d < s || d >= s + n) bytes_fwd(d, s, n); else bytes_back(d, s, n);
        return dv;
    }
    head = (size_t)(-(uintptr_t)d & 7);
    if (head > n) head = n;
    mid = (n - head) & ~(size_t)7;
    if (d < s || d >= s + n) {
        bytes_fwd(d, s, head);
        memmove(d + head, s + head, mid);
        memmove(&SH(d + head), &SH(s + head), mid / 8);
        bytes_fwd(d + head + mid, s + head + mid, n - head - mid);
    } else {
        bytes_back(d + head + mid, s + head + mid, n - head - mid);
        memmove(d + head, s + head, mid);
        memmove(&SH(d + head), &SH(s + head), mid / 8);
        bytes_back(d, s, head);
    }
    return dv;
}
/* A native function has just written n plain bytes at p (sprintf, read): say so in the states.  The bytes around
 * them in the first and last cell keep their values. */
static void lm_raw(void *p, long n)
{
    unsigned char keep[16], *b = p;
    long head, tail;
    if (!p || n <= 0 || sh_clean(p, (size_t)n)) return;
    head = n < 8 ? n : 8;
    tail = n < 16 ? n - head : 8;
    memcpy(keep, b, (size_t)head);
    memcpy(keep + 8, b + n - tail, (size_t)tail);
    lm_native((uintptr_t)p, (uint64_t)n);
    memcpy(b, keep, (size_t)head);
    memcpy(b + n - tail, keep + 8, (size_t)tail);
}
void *irix_memcpy(void *d, const void *s, size_t n) { return lm_move(d, s, n); }
void *irix_memmove(void *d, const void *s, size_t n) { return lm_move(d, s, n); }
void irix_bcopy(const void *s, void *d, size_t n) { lm_move(d, s, n); }
void *irix_memset(void *dv, int c, size_t n)
{
    unsigned char *d = dv;
    size_t head, mid, i;
    if (sh_clean(d, n)) return memset(dv, c, n);
    head = (size_t)(-(uintptr_t)d & 7);
    if (head > n) head = n;
    mid = (n - head) & ~(size_t)7;
    for (i = 0; i < head; i++) ST1((uintptr_t)(d + i), (uint8_t)c);
    memset(d + head, c, mid);
    memset(&SH(d + head), 0, mid / 8);
    for (i = head + mid; i < n; i++) ST1((uintptr_t)(d + i), (uint8_t)c);
    return dv;
}
void irix_bzero(void *d, size_t n) { irix_memset(d, 0, n); }
int irix_memcmp(const void *av, const void *bv, size_t n)
{
    const unsigned char *a = av, *b = bv;
    size_t i;
    if (sh_clean(a, n) && sh_clean(b, n)) return memcmp(av, bv, n);
    for (i = 0; i < n; i++) {
        int x = LD1((uintptr_t)(a + i)), y = LD1((uintptr_t)(b + i));
        if (x != y) return x - y;
    }
    return 0;
}
int irix_bcmp(const void *a, const void *b, size_t n) { return irix_memcmp(a, b, n) != 0; }

/* String functions: a string is bytes, but its storage may have been written in wider units (a byte table that the
 * data conversion took for words, a buffer filled by word stores).  Read through the states when the shadow says so. */
static size_t lm_strlen(const char *p)
{
    size_t n = 0;
    for (;;) {
        if (SH(p + n) == 0) {                       /* the rest of this cell is plain bytes */
            do { if (!p[n]) return n; n++; } while ((uintptr_t)(p + n) & 7);
        } else {
            if (!LD1((uintptr_t)(p + n))) return n;
            n++;
        }
    }
}
static const char *lm_plain(const char *p, size_t n, char **tmp)      /* n bytes of p as plain bytes */
{
    size_t i;
    *tmp = 0;
    if (sh_clean(p, n)) return p;
    *tmp = malloc(n + 1);
    for (i = 0; i < n; i++) (*tmp)[i] = (char)LD1((uintptr_t)(p + i));
    (*tmp)[n] = 0;
    return *tmp;
}
size_t irix_strlen(const char *p) { return lm_strlen(p); }
int irix_strcmp(const char *a, const char *b)
{
    char *ta, *tb;
    int r = strcmp(lm_plain(a, lm_strlen(a) + 1, &ta), lm_plain(b, lm_strlen(b) + 1, &tb));
    free(ta); free(tb);
    return r;
}
int irix_strncmp(const char *a, const char *b, size_t n)
{
    char *ta, *tb;
    size_t la = lm_strlen(a) + 1, lb = lm_strlen(b) + 1;
    int r = strncmp(lm_plain(a, la < n ? la : n, &ta), lm_plain(b, lb < n ? lb : n, &tb), n);
    free(ta); free(tb);
    return r;
}
char *irix_strcpy(char *d, const char *s) { lm_move(d, s, lm_strlen(s) + 1); return d; }
char *irix_strcat(char *d, const char *s) { lm_move(d + lm_strlen(d), s, lm_strlen(s) + 1); return d; }
char *irix_strncpy(char *d, const char *s, size_t n)
{
    size_t l = lm_strlen(s);
    if (l >= n) { lm_move(d, s, n); return d; }
    lm_move(d, s, l + 1);
    irix_memset(d + l + 1, 0, n - l - 1);
    return d;
}
char *irix_strchr(const char *p, int c)
{
    size_t i, n = lm_strlen(p);
    for (i = 0; i <= n; i++) if (LD1((uintptr_t)(p + i)) == (uint8_t)c) return (char *)p + i;
    return 0;
}
char *irix_strrchr(const char *p, int c)
{
    size_t n = lm_strlen(p) + 1;
    while (n--) if (LD1((uintptr_t)(p + n)) == (uint8_t)c) return (char *)p + n;
    return 0;
}

/* ---- errno --------------------------------------------------------------------------------------------------------- */
int irix_errno;                                     /* the `errno` variable of code that was built without threads */
#define SETERR() (irix_errno = errno)
int *__oserror(void) { return &errno; }             /* threaded code asks for its address: glibc's own */
int setoserror(int e) { errno = irix_errno = e; return e; }
int oserror(void) { return errno; }

/* ---- ctype: __libc_attr._ctype_tbl->_class[c + 1], then the lower and upper tables ---------------------------------- */
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
int __us_rsthread_stdio;

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

void __assert(const char *expr, const char *file, int line)
{
    fprintf(stderr, "Assertion failed: %s, file %s, line %d\n", expr, file, line);
    abort();
}

/* ---- stdio: the N64 FILE ---------------------------------------------------------------------------------------------- */
struct ifile { long _cnt; unsigned char *_ptr; unsigned char *_base; unsigned char _reserved[2]; unsigned short _flag; unsigned int _file; };
#define I_READ 0001
#define I_WRT 0002
#define I_EOF 0020
#define I_ERR 0040
struct ifile __iob[100] = { { 0, 0, 0, { 0, 0 }, I_READ, 0 }, { 0, 0, 0, { 0, 0 }, I_WRT, 1 }, { 0, 0, 0, { 0, 0 }, I_WRT | 0004, 2 } };
struct ifile_x { struct ifile f; FILE *host; };      /* allocated by fopen: the host stream follows the IRIX fields */

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

static struct ifile *wrap(FILE *h, const char *mode)
{
    struct ifile_x *x;
    if (!h) { SETERR(); return 0; }
    x = calloc(1, sizeof *x);
    x->host = h;
    x->f._file = (unsigned)fileno(h);
    x->f._flag = (mode[0] == 'r' ? I_READ : I_WRT);
    return &x->f;
}

struct ifile *irix_fopen(const char *path, const char *mode) { return wrap(fopen(path, mode), mode); }
struct ifile *irix_fdopen(int fd, const char *mode) { return wrap(fdopen(fd, mode), mode); }
struct ifile *irix_popen(const char *cmd, const char *mode) { return wrap(popen(cmd, mode), mode); }

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
int irix_pclose(struct ifile *f) { int r = pclose(host_of(f)); SETERR(); free(f); return r; }

int __filbuf(struct ifile *f) { FILE *h = host_of(f); int c = fgetc(h); sync_flags(f, h); return c; }
int __semgetc(struct ifile *f) { return __filbuf(f); }
int __flsbuf(int c, struct ifile *f) { FILE *h = host_of(f); int r = fputc(c & 0xff, h); sync_flags(f, h); return r == EOF ? EOF : (c & 0xff); }
int __semputc(int c, struct ifile *f) { return __flsbuf(c, f); }

int irix_fgetc(struct ifile *f) { return __filbuf(f); }
int irix_getc(struct ifile *f) { return __filbuf(f); }
int irix_fputc(int c, struct ifile *f) { return __flsbuf(c, f); }
int irix_putc(int c, struct ifile *f) { return __flsbuf(c, f); }
int irix_ungetc(int c, struct ifile *f) { FILE *h = host_of(f); int r = ungetc(c, h); sync_flags(f, h); return r; }
char *irix_fgets(char *b, int n, struct ifile *f) { FILE *h = host_of(f); char *r = fgets(b, n, h); sync_flags(f, h); if (r) lm_raw(b, (long)strlen(b) + 1); return r; }
int irix_fputs(const char *s, struct ifile *f) { FILE *h = host_of(f); int r = fputs(s, h); sync_flags(f, h); return r; }
int irix_fflush(struct ifile *f) { FILE *h; int r; if (!f) return fflush(0); h = host_of(f); r = fflush(h); sync_flags(f, h); return r; }
size_t irix_fread(void *b, size_t sz, size_t n, struct ifile *f) { FILE *h = host_of(f); size_t r = fread(b, sz, n, h); sync_flags(f, h); lm_raw(b, (long)(r * sz)); return r; }
size_t irix_fwrite(const void *b, size_t sz, size_t n, struct ifile *f) { FILE *h = host_of(f); size_t r = fwrite(b, sz, n, h); sync_flags(f, h); return r; }
int irix_fseek(struct ifile *f, long off, int wh) { FILE *h = host_of(f); int r = fseek(h, off, wh); sync_flags(f, h); return r; }
long irix_ftell(struct ifile *f) { return ftell(host_of(f)); }
void irix_rewind(struct ifile *f) { FILE *h = host_of(f); rewind(h); sync_flags(f, h); }
int irix_feof(struct ifile *f) { return feof(host_of(f)); }
int irix_ferror(struct ifile *f) { return ferror(host_of(f)); }
int irix_fileno(struct ifile *f) { return fileno(host_of(f)); }
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
int irix_vfprintf(struct ifile *f, const char *fmt, va_list ap) { FILE *h = host_of(f); int r = vfprintf(h, fmt, ap); sync_flags(f, h); return r; }
int irix_fscanf(struct ifile *f, const char *fmt, ...)
{
    FILE *h = host_of(f);
    va_list ap;
    int r;
    va_start(ap, fmt);
    r = vfscanf(h, fmt, ap);
    va_end(ap);
    sync_flags(f, h);
    return r;
}
void irix_perror(const char *s) { errno = irix_errno ? irix_errno : errno; perror(s); }

/* ---- calls after which the program looks at the errno variable ----------------------------------------------------------- */
long irix_read(int fd, void *b, size_t n) { long r = read(fd, b, n); SETERR(); lm_raw(b, r); return r; }
long irix_write(int fd, const void *b, size_t n) { long r = write(fd, b, n); SETERR(); return r; }
int irix_close(int fd) { int r = close(fd); SETERR(); return r; }
int irix_access(const char *p, int m) { int r = access(p, m); SETERR(); return r; }
int irix_unlink(const char *p) { int r = unlink(p); SETERR(); return r; }

/* ---- struct timeval (N64): 32 bits of padding, the 32-bit tv_sec, then the 64-bit tv_usec.  Sixteen bytes like the
 *      host's, but the seconds are an int in the second half of the first cell. ------------------------------------------- */
struct itimeval { int pad; int tv_sec; long tv_usec; };
static void put_timeval(struct itimeval *o, const struct timeval *h)
{
    o->pad = 0;
    o->tv_sec = (int)h->tv_sec;
    o->tv_usec = h->tv_usec;
    SH(o) = 0x05;                                   /* two 32-bit values */
    SH(&o->tv_usec) = 0x0f;                         /* one 64-bit value */
}
int irix_select(int n, fd_set *r, fd_set *w, fd_set *e, struct itimeval *t)
{
    struct timeval h;
    int x;
    lift_tag tg;
    if (t) {                                        /* read the way lifted code would: it may have written wider units */
        h.tv_sec = (int)LD4((uintptr_t)&t->tv_sec, &tg);
        h.tv_usec = (long)LD8((uintptr_t)&t->tv_usec, &tg);
    }
    x = select(n, r, w, e, t ? &h : 0);
    SETERR();
    return x;
}
int irix_gettimeofday(struct itimeval *t, void *tz)
{
    struct timeval h;
    int x = gettimeofday(&h, 0);
    (void)tz;
    if (t) put_timeval(t, &h);
    return x;
}

/* ---- struct stat (N64): dev 0, ino 32, mode 40, nlink 44, uid 48, gid 52, rdev 56, size 80, atim 96, mtim 112, ctim 128,
 *      blksize 144, blocks 152, fstype 160; 240 bytes ------------------------------------------------------------------------ */
static int fill_stat(unsigned char *b, const struct stat *st)
{
    memset(b, 0, 240);
    *(uint32_t *)(b + 0) = (uint32_t)st->st_dev;
    *(uint64_t *)(b + 32) = st->st_ino;
    *(uint32_t *)(b + 40) = st->st_mode;            /* the S_IF bits have the same values */
    *(uint32_t *)(b + 44) = (uint32_t)st->st_nlink;
    *(int32_t *)(b + 48) = (int32_t)st->st_uid;
    *(int32_t *)(b + 52) = (int32_t)st->st_gid;
    *(uint32_t *)(b + 56) = (uint32_t)st->st_rdev;
    *(int64_t *)(b + 80) = st->st_size;
    *(int64_t *)(b + 96) = st->st_atim.tv_sec;   *(int64_t *)(b + 104) = st->st_atim.tv_nsec;
    *(int64_t *)(b + 112) = st->st_mtim.tv_sec;  *(int64_t *)(b + 120) = st->st_mtim.tv_nsec;
    *(int64_t *)(b + 128) = st->st_ctim.tv_sec;  *(int64_t *)(b + 136) = st->st_ctim.tv_nsec;
    *(int64_t *)(b + 144) = st->st_blksize;
    *(int64_t *)(b + 152) = st->st_blocks;
    strcpy((char *)b + 160, "xfs");
    return 0;
}
int irix_stat(const char *p, unsigned char *buf) { struct stat st; int r = stat(p, &st); SETERR(); return r ? r : fill_stat(buf, &st); }
int irix_lstat(const char *p, unsigned char *buf) { struct stat st; int r = lstat(p, &st); SETERR(); return r ? r : fill_stat(buf, &st); }
int irix_fstat(int fd, unsigned char *buf) { struct stat st; int r = fstat(fd, &st); SETERR(); return r ? r : fill_stat(buf, &st); }

/* ---- struct passwd: IRIX has pw_age and pw_comment between pw_gid and pw_gecos --------------------------------------------- */
struct ipasswd { char *pw_name, *pw_passwd; int pw_uid, pw_gid; char *pw_age, *pw_comment, *pw_gecos, *pw_dir, *pw_shell; };
static struct ipasswd *conv_pw(struct ipasswd *o, const struct passwd *p)
{
    static char empty[1];
    if (!p) return 0;
    o->pw_name = p->pw_name; o->pw_passwd = p->pw_passwd; o->pw_uid = (int)p->pw_uid; o->pw_gid = (int)p->pw_gid;
    o->pw_age = empty; o->pw_comment = empty; o->pw_gecos = p->pw_gecos; o->pw_dir = p->pw_dir; o->pw_shell = p->pw_shell;
    return o;
}
struct ipasswd *irix_getpwnam(const char *n) { static __thread struct ipasswd o; return conv_pw(&o, getpwnam(n)); }
struct ipasswd *irix_getpwuid(int u) { static __thread struct ipasswd o; return conv_pw(&o, getpwuid((uid_t)u)); }
int irix_getpwnam_r(const char *n, struct ipasswd *o, char *buf, size_t len, struct ipasswd **res)
{
    struct passwd p, *r = 0;
    int e = getpwnam_r(n, &p, buf, len, &r);
    *res = e ? 0 : conv_pw(o, r);
    return e;
}
int irix_getpwuid_r(int u, struct ipasswd *o, char *buf, size_t len, struct ipasswd **res)
{
    struct passwd p, *r = 0;
    int e = getpwuid_r((uid_t)u, &p, buf, len, &r);
    *res = e ? 0 : conv_pw(o, r);
    return e;
}

/* ---- BSD directory reading: ino 8, off 8, reclen 2, name at 18 --------------------------------------------------------------- */
struct irix_dirent { uint64_t d_ino; int64_t d_off; unsigned short d_reclen; char d_name[256]; };
void *BSDopendir(const char *p) { return opendir(p); }
int BSDclosedir(void *d) { return closedir(d); }
struct irix_dirent *BSDreaddir(void *d)
{
    static __thread struct irix_dirent e;
    struct dirent *h = readdir(d);
    if (!h) return 0;
    e.d_ino = h->d_ino;
    e.d_off = h->d_off;
    strncpy(e.d_name, h->d_name, sizeof e.d_name - 1);
    e.d_name[sizeof e.d_name - 1] = 0;
    e.d_reclen = (unsigned short)(18 + strlen(e.d_name) + 1);
    return &e;
}

/* ---- SGI Xlib: FAST_QUARKS.  IRIX headers define XmNfoo / XtNfoo as (char *)~index into the name table of libX11 when
 *      FAST_QUARKS is set; the Xrm string functions accept such names.  Bit 63 marks one in the 64-bit ABI. ------------------- */
#include "../../patches/Xt/nm_fq_table.inc"
#define FQ_COUNT (sizeof irix_fast_quark_names / sizeof irix_fast_quark_names[0])

static const char *fq(const char *s)
{
    uint64_t v = (uint64_t)(uintptr_t)s;
    if ((int64_t)v < 0 && ~v < FQ_COUNT)
        return irix_fast_quark_names[~v];
    return s;
}
const char *_XQstring(const char *s) { return fq(s); }
int _XQstrcmp(const char *a, const char *b) { return a == b ? 0 : strcmp(fq(a), fq(b)); }

#define NEXT(ret, name, params, args) \
    { static ret (*next) params; if (!next) next = (ret (*) params)dlsym(RTLD_NEXT, #name); return next args; }
XrmQuark XrmStringToQuark(const char *s) NEXT(XrmQuark, XrmStringToQuark, (const char *), (fq(s)))
XrmQuark XrmPermStringToQuark(const char *s) NEXT(XrmQuark, XrmPermStringToQuark, (const char *), (fq(s)))
void XrmStringToQuarkList(const char *s, XrmQuarkList q) NEXT(void, XrmStringToQuarkList, (const char *, XrmQuarkList), (fq(s), q))
void XrmStringToBindingQuarkList(const char *s, XrmBindingList b, XrmQuarkList q)
    NEXT(void, XrmStringToBindingQuarkList, (const char *, XrmBindingList, XrmQuarkList), (fq(s), b, q))
XrmQuark _XrmBufferToQuark(const char *buf, int len)
{
    char tmp[512], *p = len < (int)sizeof tmp ? tmp : malloc((size_t)len + 1);
    XrmQuark q;
    memcpy(p, buf, (size_t)len);
    p[len] = 0;
    q = XrmStringToQuark(p);
    if (p != tmp) free(p);
    return q;
}

#include "sgi_fast_atoms.inc"
Atom _XSGIFastInternAtom(Display *dpy, int fast_name, Bool only_if_exists)
{
    unsigned n = (unsigned)fast_name;
    if (n < 69u || n > 200u || n - 69u >= sizeof sgi_fast_atom_names / sizeof *sgi_fast_atom_names)
        return 0;
    return XInternAtom(dpy, sgi_fast_atom_names[n - 69u], only_if_exists);
}

unsigned int SgiGetNumLockModifiers(Display *dpy)
{
    unsigned int mask = 0;
    KeyCode nl = XKeysymToKeycode(dpy, XK_Num_Lock);
    XModifierKeymap *map = XGetModifierMapping(dpy);
    if (map && nl) {
        int mod, k;
        for (mod = 3; mod < 8; mod++)           /* Mod1..Mod5 */
            for (k = 0; k < map->max_keypermod; k++)
                if (map->modifiermap[mod * map->max_keypermod + k] == nl)
                    mask |= 1u << mod;
    }
    if (map) XFreeModifiermap(map);
    return mask;
}

/* ---- Property data.  Format 8 is a byte stream: what lifted code built from wider units goes out in the byte order
 *      the big-endian machine would have sent, and what comes back is marked as single bytes, so that a 16- or 32-bit
 *      value copied out of it reads the way that machine read it.  (Motif's drag and drop tables carry their own byte
 *      order mark and are built and parsed this way.)  Results are stored through the accessors: the cells the caller
 *      passes may hold other values of its own. ------------------------------------------------------------------- */
int irix_XChangeProperty(Display *dpy, Window w, Atom prop, Atom type, int format, int mode, const unsigned char *data, int n)
{
    char *tmp = 0;
    int r;
    if (format == 8 && data && n > 0)
        data = (const unsigned char *)lm_plain((const char *)data, (size_t)n, &tmp);
    r = XChangeProperty(dpy, w, prop, type, format, mode, data, n);
    free(tmp);
    return r;
}
int irix_XGetWindowProperty(Display *dpy, Window w, Atom prop, long off, long len, Bool del, Atom req, Atom *type, int *format,
                            unsigned long *nitems, unsigned long *after, unsigned char **data)
{
    Atom t = 0;
    int f = 0, r;
    unsigned long n = 0, a = 0;
    unsigned char *p = 0;
    r = XGetWindowProperty(dpy, w, prop, off, len, del, req, &t, &f, &n, &a, &p);
    if (r == Success && f == 8 && p && !((uintptr_t)p & 7))
        memset(&SH(p), ST_N | (ST_N << 2), malloc_usable_size(p) / 8);
    if (type) ST8((uintptr_t)type, t, 0);
    if (format) ST4((uintptr_t)format, (uint32_t)f, 0);
    if (nitems) ST8((uintptr_t)nitems, n, 0);
    if (after) ST8((uintptr_t)after, a, 0);
    if (data) ST8((uintptr_t)data, (uint64_t)(uintptr_t)p, 0);
    return r;
}
int irix_XFree(void *p)
{
    if (p && !((uintptr_t)p & 7) && !sh_clean(p, 8)) sh_clear(p, malloc_usable_size(p));
    return XFree(p);
}

/* ---- Native functions that fill a caller's buffer with bytes ---------------------------------------------------- */
int irix_sprintf(char *b, const char *fmt, ...)
{
    va_list ap;
    int r;
    va_start(ap, fmt);
    r = vsprintf(b, fmt, ap);
    va_end(ap);
    lm_raw(b, (long)r + 1);
    return r;
}
int irix_snprintf(char *b, size_t n, const char *fmt, ...)
{
    va_list ap;
    int r;
    va_start(ap, fmt);
    r = vsnprintf(b, n, fmt, ap);
    va_end(ap);
    if (n) lm_raw(b, (long)strlen(b) + 1);
    return r;
}
char *irix_getcwd(char *b, size_t n) { char *r = getcwd(b, n); if (r && b) lm_raw(b, (long)strlen(b) + 1); return r; }
int irix_gethostname(char *b, size_t n) { int r = gethostname(b, n); if (!r) lm_raw(b, (long)strnlen(b, n) + 1); return r; }
size_t irix_wcstombs(char *b, const wchar_t *w, size_t n) { size_t r = wcstombs(b, w, n); if (b && r != (size_t)-1) lm_raw(b, (long)(r < n ? r + 1 : n)); return r; }
int irix_XLookupString(XKeyEvent *ev, char *b, int n, KeySym *ks, XComposeStatus *cs)
{
    KeySym k = 0;
    int r = XLookupString(ev, b, n, &k, cs);
    lm_raw(b, r);
    if (ks) ST8((uintptr_t)ks, k, 0);
    return r;
}
int irix_XmbLookupString(XIC ic, XKeyPressedEvent *ev, char *b, int n, KeySym *ks, Status *st)
{
    KeySym k = 0;
    Status t = 0;
    int r = XmbLookupString(ic, ev, b, n, &k, &t);
    if (t == XLookupChars || t == XLookupBoth) lm_raw(b, r);
    if (ks) ST8((uintptr_t)ks, k, 0);
    if (st) ST4((uintptr_t)st, (uint32_t)t, 0);
    return r;
}

/* ---- Events.  Xlib fills in the event; its fields get their widths recorded by event type, because lifted code
 *      reads parts of them (the low 16 bits of x and y by 16-bit loads). ------------------------------------------- */
#include "xevent_fields.inc"
static void lm_event(XEvent *ev)
{
    static const uint16_t l5[] = {56,8, 64,8, 72,8, 80,8, 88,8, 0xffff};
    static const uint16_t s10[] = {56,2, 58,2, 60,2, 62,2, 64,2, 66,2, 68,2, 70,2, 72,2, 74,2, 0xffff};
    int t = ev->type & 0x7f;
    if (getenv("LIFT_NOEV")) return;
    memset(&SH(ev), 0, sizeof *ev / 8);
    lm_mark((uintptr_t)ev, t < 36 ? ev_fields[t] : ev_fields[0]);
    if (t == ClientMessage && ev->xclient.format != 8)
        lm_mark((uintptr_t)ev, ev->xclient.format == 32 ? l5 : s10);
}
int irix_XNextEvent(Display *d, XEvent *ev) { int r = XNextEvent(d, ev); lm_event(ev); return r; }
int irix_XPeekEvent(Display *d, XEvent *ev) { int r = XPeekEvent(d, ev); lm_event(ev); return r; }
int irix_XWindowEvent(Display *d, Window w, long m, XEvent *ev) { int r = XWindowEvent(d, w, m, ev); lm_event(ev); return r; }
int irix_XIfEvent(Display *d, XEvent *ev, Bool (*p)(Display *, XEvent *, XPointer), XPointer a) { int r = XIfEvent(d, ev, p, a); lm_event(ev); return r; }
Bool irix_XCheckIfEvent(Display *d, XEvent *ev, Bool (*p)(Display *, XEvent *, XPointer), XPointer a) { Bool r = XCheckIfEvent(d, ev, p, a); if (r) lm_event(ev); return r; }
Bool irix_XCheckMaskEvent(Display *d, long m, XEvent *ev) { Bool r = XCheckMaskEvent(d, m, ev); if (r) lm_event(ev); return r; }
