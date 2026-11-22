/* IRIX <termio.h>: the System V terminal interface on the host's.  TCGETA, TCSETA, TCSETAW and TCSETAF come from
 * <sys/ioctl.h> with Linux's numbers; struct termio is the kernel's (asm-generic/termios.h), which glibc no longer
 * defines and whose asm header cannot be mixed with glibc's <termios.h>.  IRIX's own header has IRIX's numbers. */
#ifndef IRIX_COMPAT_TERMIO_H
#define IRIX_COMPAT_TERMIO_H
#include <termios.h>
#include <sys/ioctl.h>
#ifndef NCC
#define NCC 8
#endif
struct termio {
	unsigned short c_iflag;		/* input mode flags */
	unsigned short c_oflag;		/* output mode flags */
	unsigned short c_cflag;		/* control mode flags */
	unsigned short c_lflag;		/* local mode flags */
	unsigned char c_line;		/* line discipline */
	unsigned char c_cc[NCC];	/* control characters */
};
#endif
