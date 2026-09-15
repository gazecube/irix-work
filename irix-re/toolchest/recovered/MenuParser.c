/*
 * IRIX Toolchest parser byte scanners.
 *
 * Recovered directly from /usr/bin/X11/toolchest (IRIX 6.5 N32):
 *   ToLower          @ 0x1000e29c, size 0x88
 *   ScanWhitespace   @ 0x1000e324, size 0x7c
 *   ScanAlphanumeric @ 0x100135d0, size 0x74
 *
 * These are Toolchest bodies, not copied Motif source. The Motif 1.2.4
 * WmResParse.c routines are used only as a comparison baseline. In
 * particular, the surviving upstream source has multibyte/mblen logic that
 * is absent from these IRIX Toolchest machine-code bodies.
 */

#include <ctype.h>

void
ToLower(unsigned char *string)
{
    unsigned char *p = string;

    while (*p) {
        if (isupper((int)*p))
            *p = (unsigned char)tolower((int)*p);
        ++p;
    }
}

void
ScanWhitespace(unsigned char **linePP)
{
    while (*linePP && isspace((int)**linePP))
        ++(*linePP);
}

void
ScanAlphanumeric(unsigned char **linePP)
{
    while (*linePP && isalnum((int)**linePP))
        ++(*linePP);
}
