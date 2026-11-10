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
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/Xresource.h>
#include <X11/keysym.h>

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
char *irix_fgets(char *b, int n, struct ifile *f) { FILE *h = host_of(f); char *r = fgets(b, n, h); sync_flags(f, h); return r; }
int irix_fputs(const char *s, struct ifile *f) { FILE *h = host_of(f); int r = fputs(s, h); sync_flags(f, h); return r; }
int irix_fflush(struct ifile *f) { FILE *h; int r; if (!f) return fflush(0); h = host_of(f); r = fflush(h); sync_flags(f, h); return r; }
size_t irix_fread(void *b, size_t sz, size_t n, struct ifile *f) { FILE *h = host_of(f); size_t r = fread(b, sz, n, h); sync_flags(f, h); return r; }
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
long irix_read(int fd, void *b, size_t n) { long r = read(fd, b, n); SETERR(); return r; }
long irix_write(int fd, const void *b, size_t n) { long r = write(fd, b, n); SETERR(); return r; }
int irix_close(int fd) { int r = close(fd); SETERR(); return r; }
int irix_access(const char *p, int m) { int r = access(p, m); SETERR(); return r; }
int irix_unlink(const char *p) { int r = unlink(p); SETERR(); return r; }
int irix_select(int n, fd_set *r, fd_set *w, fd_set *e, struct timeval *t) { int x = select(n, r, w, e, t); SETERR(); return x; }

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
