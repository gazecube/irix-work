/* IRIX <standards.h> for a native build.  IRIX's version derives these from _SGI_SOURCE, _POSIX_SOURCE and friends,
 * which the host's feature macros (_GNU_SOURCE) set differently.  Here they are fixed to what an ordinary IRIX
 * compile sees: the values of the real header evaluated with _SGI_SOURCE and __EXTENSIONS__ (as cc defines them),
 * none of the POSIX or X/Open selectors.  Note that _NO_POSIX, _NO_XOPEN4 and _NO_XOPEN5 are true together with
 * their positives there. */
#ifndef __STANDARDS_H__
#define __STANDARDS_H__
#define _ANSIMODE		0
#define _NO_ANSIMODE		1
#define _POSIX90		1
#define _POSIX2			1
#define _POSIX93		1
#define _NO_POSIX		1
#define _POSIX1C		1
#define _XOPEN4			1
#define _NO_XOPEN4		1
#define _XOPEN4UX		1
#define _XOPEN5			1
#define _NO_XOPEN5		1
#define _SGIAPI			1
#define _ABIAPI			0
#define _NO_ABIAPI		1
#define _LFAPI			1
#define _REENTRANT_FUNCTIONS	0
#endif
