#ifndef ARCS_UEFI_X64_CTYPE_H
#define ARCS_UEFI_X64_CTYPE_H

/*
 * Standalone ASCII ctype surface for the native X64 PROM build.
 *
 * The original ARCS sources expect the standalone C environment, not the
 * host libc ABI.  Letting the build pick up glibc's <ctype.h> introduces the
 * glibc-private __ctype_b_loc() dependency into a freestanding UEFI image.
 * Keep the source-level ctype contract here and classify the PROM's ASCII
 * command input directly.
 */
#define isupper(c) ((unsigned int)((c) - 'A') <= (unsigned int)('Z' - 'A'))
#define islower(c) ((unsigned int)((c) - 'a') <= (unsigned int)('z' - 'a'))
#define isalpha(c) (isupper(c) || islower(c))
#define isdigit(c) ((unsigned int)((c) - '0') <= (unsigned int)('9' - '0'))
#define isxdigit(c) (isdigit(c) || \
                     (unsigned int)((c) - 'A') <= (unsigned int)('F' - 'A') || \
                     (unsigned int)((c) - 'a') <= (unsigned int)('f' - 'a'))
#define isalnum(c) (isalpha(c) || isdigit(c))
#define isspace(c) ((c) == ' ' || (c) == '\t' || (c) == '\n' || \
                    (c) == '\r' || (c) == '\f' || (c) == '\v')
#define iscntrl(c) ((unsigned int)(c) < 0x20U || (c) == 0x7f)
#define isprint(c) ((unsigned int)((c) - 0x20) <= (unsigned int)(0x7e - 0x20))
#define isgraph(c) ((unsigned int)((c) - 0x21) <= (unsigned int)(0x7e - 0x21))
#define ispunct(c) (isgraph(c) && !isalnum(c))
#define tolower(c) (isupper(c) ? ((c) + ('a' - 'A')) : (c))
#define toupper(c) (islower(c) ? ((c) - ('a' - 'A')) : (c))

#endif /* ARCS_UEFI_X64_CTYPE_H */
