/*
 * SVR4/IRIX <termio.h> with IRIX's struct termio (it has c_ospeed/c_ispeed and
 * NCCS control characters).  ioctl() is redirected to irix_ioctl(), which
 * translates TCGETA/TCSETA* to Linux termios; LDSETT/LDGETT are no-ops.
 */
#ifndef _IRIX_TERMIO_H
#define _IRIX_TERMIO_H
#include <termios.h>
#include <sys/ioctl.h>

#ifdef NCCS
#undef NCCS
#endif
#define NCCS		23
#define IRIX_NCC	8

struct irix_termio {
	unsigned int c_iflag;
	unsigned int c_oflag;
	unsigned int c_cflag;
	unsigned int c_lflag;
	unsigned int c_ospeed;
	unsigned int c_ispeed;
	char c_line;
	unsigned char c_cc[NCCS];
};
#define termio irix_termio

#define LDISC0		0
#define LDISC1		1
#define LDSETT		(('D' << 8) | 1)
#define LDGETT		(('D' << 8) | 0)
struct termcb {
	char st_flgs;
	char st_termt;
	char st_crow;
	char st_ccol;
	char st_vrow;
	char st_lrow;
};
#define TERM_NONE	0
#define TERM_TEC	1
#define TERM_V61	2
#define TERM_V10	3
#define TERM_TEX	4
#define TERM_D40	5
#define TERM_H45	6
#define TERM_D42	7

int irix_ioctl(int, unsigned long, ...);
#define ioctl		irix_ioctl
#endif
