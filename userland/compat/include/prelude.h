/* Forced into every compile of IRIX source (gcc -include).  Brings in the common host headers first, then papers
 * over the differences that would otherwise stop old SGI source from compiling against them. */
#ifndef IRIX_PRELUDE_H
#define IRIX_PRELUDE_H
#define _GNU_SOURCE 1
/* what SGI's compilers predefine about the system (not the MIPS ones: the code is for x86 now) */
#define sgi 1
#define __sgi 1
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
#include <dirent.h>
#include <sgidefs.h>

#undef environ
typedef struct dirent dirent_t;                 /* IRIX names for host types */

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
