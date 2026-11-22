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
