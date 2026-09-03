/*
 * Reconstructed from IRIX libimdUtil.so MIPS N32 machine code.
 *
 * Original symbol: imdMkdirP
 * Original address: 0x0983cf58
 * Original size: 324 bytes
 *
 * Resolved external calls from the MIPS GOT:
 *   strdup
 *   access
 *   free
 *   mkdir
 *
 * This file is a decompilation artifact. The function body follows the
 * observed control flow and side effects of the original binary; no claim is
 * made yet about the original source filename or exact source-level typedefs.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

extern "C" int imdMkdirP(const char *path, mode_t mode)
{
    char *copy;
    char *scan;
    int result;

    copy = strdup(path);
    scan = copy + 1;

    while (*scan != '\0') {
        if (*scan == '/') {
            *scan = '\0';

            if (access(copy, F_OK) != 0) {
                result = mkdir(copy, mode);
                if (result < 0) {
                    free(copy);
                    return result;
                }
            }

            *scan = '/';
        }

        ++scan;
    }

    free(copy);

    if (access(path, F_OK) != 0) {
        result = mkdir(path, mode);
        if (result < 0)
            return result;
    }

    return 0;
}
