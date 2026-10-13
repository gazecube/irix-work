/* /lib/cpp for the IRIX tree.
 *
 * The original is an o32 binary needing /usr/lib/libc.so.1, which does not
 * exist for the N32 runtime.  xrdb (and others) only run it as
 *     /lib/cpp [-Dname[=val]] [-Uname] [-Idir] [file]
 * and read the preprocessed text from stdout, so hand over to the host's C
 * preprocessor in traditional mode (X resource files are not C).
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    const char *host = getenv("IRIX_HOST_CPP");
    char **out;
    int i, n = 0;
    if (!host || !*host)
        host = "/usr/bin/cpp";
    out = (char **)calloc((size_t)argc + 8, sizeof(*out));
    if (!out)
        return 127;
    out[n++] = (char *)host;
    out[n++] = "-traditional-cpp";
    out[n++] = "-undef";          /* no host predefined macros */
    out[n++] = "-P";              /* no line markers: resources are not C */
    for (i = 1; i < argc; ++i)
        out[n++] = argv[i];
    out[n] = NULL;
    execv(host, out);
    perror("irix-cpp: exec");
    return 127;
}
