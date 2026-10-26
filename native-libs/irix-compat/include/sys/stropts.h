/* SVR4 STREAMS ioctls used for plain FIFOs: I_SETSIG becomes O_ASYNC +
 * F_SETOWN (SIGPOLL == SIGIO on Linux); I_SRDOPT is a no-op. */
#ifndef _IRIX_SYS_STROPTS_H
#define _IRIX_SYS_STROPTS_H
#define I_SETSIG	(('S' << 8) | 9)
#define I_SRDOPT	(('S' << 8) | 3)
#define S_INPUT		0x0001
#define S_HIPRI		0x0002
#define S_OUTPUT	0x0004
#define RNORM		0x0001
#define RMSGD		0x0002
#define RMSGN		0x0004
#endif
