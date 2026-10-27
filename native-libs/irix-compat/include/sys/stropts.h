/* IRIX <stropts.h>: the real STREAMS numbers and structures (a copy of the
 * kernel's uapi header, patch 0011), plus getmsg/putmsg over the IRIX syscall
 * block.  libirixcompat's ioctl falls back to a FIFO emulation of I_SETSIG
 * and I_SRDOPT when the descriptor is not a stream. */
#ifndef _IRIX_SYS_STROPTS_H
#define _IRIX_SYS_STROPTS_H
#include <sys/irix_stropts_uapi.h>

#ifdef __cplusplus
extern "C" {
#endif
int irix_getmsg(int fd, struct strbuf *ctl, struct strbuf *dat, int *flagsp);
int irix_putmsg(int fd, const struct strbuf *ctl, const struct strbuf *dat, int flags);
int irix_getpmsg(int fd, struct strbuf *ctl, struct strbuf *dat, int *bandp, int *flagsp);
int irix_putpmsg(int fd, const struct strbuf *ctl, const struct strbuf *dat, int band, int flags);
#ifdef __cplusplus
}
#endif
#define getmsg(fd, c, d, f)		irix_getmsg((fd), (c), (d), (f))
#define putmsg(fd, c, d, f)		irix_putmsg((fd), (c), (d), (f))
#define getpmsg(fd, c, d, b, f)		irix_getpmsg((fd), (c), (d), (b), (f))
#define putpmsg(fd, c, d, b, f)		irix_putpmsg((fd), (c), (d), (b), (f))
#endif
