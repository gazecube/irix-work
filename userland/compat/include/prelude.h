/* Forced into every compile of IRIX source (gcc -include).  Brings in the common host headers first, then papers
 * over the differences that would otherwise stop old SGI source from compiling against them. */
#ifndef IRIX_PRELUDE_H
#define IRIX_PRELUDE_H
#define _GNU_SOURCE 1
/* what SGI's compilers predefine about the system (not the MIPS ones: the code is for x86 now) */
#define sgi 1
#define __sgi 1
/* and what it predefines about the language in an ordinary (-xansi) compile */
#define _LONGLONG 1
#define _LANGUAGE_C 1
#define _SGI_SOURCE 1
#define __EXTENSIONS__ 1
/* and not what gcc predefines for i386: SGI's sources still carry x86 paths from System V (sysi86) */
#undef i386
/* environ: old programs declare it themselves, with their own type; keep the host's declaration out of the way */
#define environ irix_host_environ_decl
#include <sys/types.h>
#include <limits.h>
#include <locale.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/wait.h>
#include <malloc.h>
#include <time.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/statvfs.h>
#include <sys/ioctl.h>
#include <termio.h>                             /* (compat: struct termio, the SysV interface) */
#include <utime.h>
#include <utmp.h>
#include <mntent.h>
#include <irix_mntent.h>                        /* IRIX's MNTTYPE_/MNTOPT_ names the host lacks (fetch.sh) */
#include <irix_utmp.h>
#include <irix_statvfs.h>
#include <sys/param.h>
#include <paths.h>
#include <irix_paths.h>                         /* IRIX's _PATH_ names the host lacks (fetch.sh) */
#include <sgidefs.h>
#include <irix_types.h>                         /* IRIX type names the host lacks (uchar_t, ulong_t, prid_t, ...) */
#include <standards.h>                         /* IRIX feature macros (_SGIAPI, ...), as cc gives every compile */

#undef environ
/* stdin, stdout, stderr as the addresses of glibc's three FILE objects (stdin always points at
 * _IO_2_1_stdin_), so old code's static "FILE *f = stdin;" is a constant initializer as it was on IRIX */
extern FILE _IO_2_1_stdin_, _IO_2_1_stdout_, _IO_2_1_stderr_;
#undef stdin
#undef stdout
#undef stderr
#define stdin (&_IO_2_1_stdin_)
#define stdout (&_IO_2_1_stdout_)
#define stderr (&_IO_2_1_stderr_)

typedef struct dirent dirent_t;                 /* (incomplete here: <dirent.h> is not forced in, programs name
                                                   variables DIR) */
typedef struct dirent64 dirent64_t;
typedef struct timespec timespec_t;
typedef struct statvfs64 statvfs64_t;
typedef struct flock64 flock64_t;
#include <sys/stat.h>
/* IRIX <sys/stat.h> additions (the other S_ values are the same on both) */
#ifndef S_IAMB
#define S_IAMB 0x1FF                            /* access mode bits */
#endif
#ifndef S_IFNAM
#define S_IFNAM 0x5000                          /* XENIX special named file; Linux has none, so never matches */
#define S_INSEM 0x1
#define S_INSHD 0x2
#endif
/* IRIX <limits.h> names for the long long limits */
#ifndef LONGLONG_MAX
#define LONGLONG_MAX LLONG_MAX
#define LONGLONG_MIN LLONG_MIN
#define ULONGLONG_MAX ULLONG_MAX
#endif
#ifndef NGROUPS_UMAX
#define NGROUPS_UMAX NGROUPS_MAX               /* IRIX <sys/param.h> */
#endif
#ifndef __NBBY
#define __NBBY 8                                /* bits per byte (IRIX <sys/types.h>) */
#endif

/* IRIX's error message table and errno accessors (compat/lib/irix_k.c) */
#define sys_errlist _sys_errlist
#define sys_nerr _sys_nerr
extern char *_sys_errlist[];
extern int _sys_nerr;
extern int oserror(void);
extern int setoserror(int);                 /* IRIX names for host types */

/* K&R declarations such as "char *malloc();" clash with the host's prototypes.  After the host headers are in, these
 * names become function-like macros: the old declaration then declares a different name, and calls still reach the
 * host function through it (irix_compat.c). */
#define malloc(n) irix_k_malloc(n)
#define calloc(n, m) irix_k_calloc(n, m)
#define realloc(p, n) irix_k_realloc(p, n)
/* No declaration of the irix_k_ names on i386: a program's own "char *malloc();" or "void *malloc();" then declares
 * them without a clash, and a call without one is an implicit int, which holds a pointer there.  An x86_64 build
 * needs the prototypes (an int would cut the pointer); there a K&R redeclaration still clashes. */
#if __SIZEOF_POINTER__ == 8
extern void *irix_k_malloc(size_t), *irix_k_calloc(size_t, size_t), *irix_k_realloc(void *, size_t);
#endif

/* The host's string functions as macros would be invoked by old declarations like "char *strchr();" */
#undef strchr
#undef strrchr
#undef strcpy
#undef strcmp
#undef strncmp
#undef strlen
#undef strcat
#undef strdup
#undef memcpy
#undef memset

/* Names that old programs define themselves, with their own signatures (sprite's two-argument setenv): renamed, so a
 * program's own definition does not clash with the host's; a program that only calls one reaches the host function
 * through irix_compat.c. */
#define setenv irix_k_setenv
#define getline irix_k_getline
/* functions old programs define for themselves under libc names (chmod's abs, awk's asprintf) or declare K&R with
 * another type (sort's char *sbrk()); a program that only calls one reaches the host's (compat/lib/irix_k.c) */
#define abs irix_k_abs
#define asprintf irix_k_asprintf
#define sbrk irix_k_sbrk
#define index irix_k_index
#define rindex irix_k_rindex
/* basename, dirname: IRIX's (libgen, POSIX), not glibc's GNU basename; old programs also declare them K&R */
/* getopt: IRIX's (libc source in libirixc), with its own optind & co.  GNU getopt reads a leading '-' in the option
 * string as "return operands in order", which IRIX ls's "-RadC1..." relies on not happening */
#define getopt _getopt
#define optind _optind
#define optarg _optarg
#define opterr _opterr
#define optopt _optopt
extern int _getopt(int, char *const *, const char *);
extern int _optind, _opterr, _optopt;
extern char *_optarg;
#define basename _basename
#define dirname _dirname
/* sysinfo: IRIX's (command, buffer, count) call, not Linux's statistics call */
#define sysinfo irix_k_sysinfo
extern int irix_k_sysinfo(int, char *, long);
/* sysconf: IRIX-only names (irix_sysconf.h, numbered from 0x4000) go to the kernel like IRIX's sysconf sends them */
#include <irix_sysconf.h>
#define sysconf irix_k_sysconf
extern long irix_k_sysconf(int);

/* union wait, from BSD: the host C library no longer has it (little-endian field order) */
union wait {
	int w_status;
	struct { unsigned int w_Termsig:7, w_Coredump:1, w_Retcode:8, :16; } w_T;
	struct { unsigned int w_Stopval:8, w_Stopsig:8, :16; } w_S;
};
#define w_termsig w_T.w_Termsig
#define w_coredump w_T.w_Coredump
#define w_retcode w_T.w_Retcode
#define w_stopval w_S.w_Stopval
#define w_stopsig w_S.w_Stopsig
/* the W* macros take a union wait as well as an int (as BSD's did) */
#define __irix_wint(x) (*(const int *)&(x))
#undef WIFEXITED
#undef WEXITSTATUS
#undef WIFSIGNALED
#undef WTERMSIG
#undef WIFSTOPPED
#undef WSTOPSIG
#undef WCOREDUMP
#define WIFEXITED(x) ((__irix_wint(x) & 0x7f) == 0)
#define WEXITSTATUS(x) ((__irix_wint(x) >> 8) & 0xff)
#define WIFSIGNALED(x) ((__irix_wint(x) & 0x7f) != 0 && (__irix_wint(x) & 0x7f) != 0x7f)
#define WTERMSIG(x) (__irix_wint(x) & 0x7f)
#define WIFSTOPPED(x) ((__irix_wint(x) & 0xff) == 0x7f)
#define WSTOPSIG(x) ((__irix_wint(x) >> 8) & 0xff)
#define WCOREDUMP(x) (__irix_wint(x) & 0x80)
#endif
