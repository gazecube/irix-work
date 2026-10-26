/* SVR4 pfmt(3C) message interface (no message catalogs: the default text is used). */
#ifndef _IRIX_PFMT_H
#define _IRIX_PFMT_H
#include <stdio.h>
#define MM_HALT		0x00000001L
#define MM_ERROR	0x00000002L
#define MM_WARNING	0x00000004L
#define MM_INFO		0x00000008L
#define MM_NOSTD	0x00000010L
#define MM_ACTION	0x00000020L
#define MM_NOGET	0x00000040L
#define MM_NOCONSOLE	0x00000080L
int pfmt(FILE *, long, const char *, ...);
int lfmt(FILE *, long, const char *, ...);
int setcat(const char *);
char *setlabel(const char *);
#endif
