/* IRIX audit trail (SAT): not available, records are dropped. */
#ifndef _IRIX_SAT_H
#define _IRIX_SAT_H
#define SAT_AE_IDENTITY	1
#define SAT_SUCCESS	0
#define SAT_FAILURE	1
int satvwrite(int, int, const char *, ...);
#endif
