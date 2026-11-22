/* the targets of the prelude's renaming macros; weak, so a program's own definition wins */
#include <stdlib.h>
__attribute__((weak)) char *irix_k_malloc(size_t n) { return malloc(n); }
__attribute__((weak)) char *irix_k_calloc(size_t n, size_t m) { return calloc(n, m); }
__attribute__((weak)) char *irix_k_realloc(void *p, size_t n) { return realloc(p, n); }
__attribute__((weak)) int irix_k_setenv(const char *n, const char *v, int over) { return setenv(n, v, over); }
#include <stdio.h>
/* old libc internals some programs call: flush every stream; run the exit handlers (only the stdio part here) */
void _cleanup(void) { fflush(NULL); }
void _exithandle(void) { fflush(NULL); }
#include <errno.h>
#include <string.h>
/* IRIX's error message table and errno accessors (the host has neither under these names) */
char *_sys_errlist[152];
int _sys_nerr = 152;
static void __attribute__((constructor)) irix_errlist_init(void) { int i; for (i = 0; i < 152; i++) _sys_errlist[i] = strerror(i); }
int oserror(void) { return errno; }
int setoserror(int e) { errno = e; return e; }
/* libc's multithreading hooks.  The flag stays 0: libc code only calls the lock functions when it is set (the IRIX
 * thread library sets it), so they are never reached; they exist for the linker. */
int __us_rsthread_misc = 0;
int __us_rsthread_stdio = 0;
int __libc_locklocale(void) { return 0; }
int __libc_unlocklocale(int x) { (void)x; return 0; }
int __libc_islocklocale(void) { return 1; }
int __libc_lockmisc(void) { return 0; }
int __libc_unlockmisc(int x) { (void)x; return 0; }
int __libc_lockdir(void) { return 0; }
int __libc_unlockdir(int x) { (void)x; return 0; }
/* the libc-internal spellings of the errno accessors, called by libc sources built here */
int _oserror(void) { return errno; }
int _setoserror(int e) { errno = e; return e; }
#include <stdio.h>
/* a program that only calls getline (the host's three-argument one) reaches it through the prelude's rename */
__attribute__((weak)) long irix_k_getline(char **l, size_t *n, FILE *f) { return getline(l, n, f); }
