/*
 * Reconstructed from IRIX libimdUtil.so MIPS N32 machine code.
 *
 * Original compilation unit recovered from DWARF .debug_line:
 *   /xlv55/kudzu-apr12/work/userenv/desktop/lib/libimdUtil/MemoryMap.c++
 *
 * Recovered functions currently implemented here:
 *   imdMkdirP     @ 0x0983cf58, 324 bytes
 *   imdPathcanon  @ 0x0983d09c, 648 bytes
 *
 * No claim is made yet that local variable names or exact source-level
 * typedefs match SGI's original source. Function control flow and external
 * call behavior are reconstructed from the binary.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

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

extern "C" int imdPathcanon(const char *path, char *out)
{
    const size_t buffer_size = 1024;
    char *limit;
    char *dst;
    const char *src;
    char *component_start;

    if (path == 0 || out == 0)
        return -1;

    limit = out + buffer_size;
    dst = out;

    /* Relative paths are rooted at the current working directory. */
    if (*path != '/') {
        if (getcwd(out, buffer_size) == 0)
            return -1;

        dst = out + strlen(out);
        if (dst < limit)
            *dst++ = '/';
    }

    /* The original substitutes "/" for an empty input string. */
    src = (*path != '\0') ? path : "/";
    component_start = dst;

    /* Copy the supplied path into the output buffer. */
    while (dst < limit) {
        char c = *src++;
        *dst++ = c;
        if (c == '\0')
            break;
    }

    /*
     * Canonicalize in place. The MIPS routine performs reductions when a
     * slash terminates a component: duplicate '/', './', and '../'. For a
     * relative path, '..' is allowed to walk backward into the cwd prefix.
     */
    src = component_start;
    dst = component_start;

    while (dst < limit && *src != '\0') {
        char c = *src++;
        *dst++ = c;

        if (c != '/')
            continue;

        if (!(out + 1 < dst))
            continue;

        /* "//" -> "/" */
        if (dst[-2] == '/') {
            --dst;
            continue;
        }

        if (dst[-2] != '.')
            continue;

        /* "/./" -> "/" */
        if (dst[-3] == '/') {
            dst -= 2;
            continue;
        }

        if (dst[-3] != '.')
            continue;

        /* Only treat the component as ".." when it is slash-delimited. */
        if (dst[-4] != '/')
            continue;

        {
            char *p = dst - 4;

            while (out < p) {
                if (p[-1] == '/') {
                    --p;
                    break;
                }
                --p;
            }

            dst = p + 1;
        }
    }

    /* Preserve root '/', but strip a trailing slash from longer paths. */
    if (dst[-1] == '/' && out + 1 < dst)
        --dst;

    *dst = '\0';
    return 0;
}
