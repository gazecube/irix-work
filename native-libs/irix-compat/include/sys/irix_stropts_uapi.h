/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * IRIX STREAMS: the user-visible interface (<stropts.h>, <sys/strlog.h>).
 *
 * Numbers and structure layouts follow IRIX 6.5 so that code written for IRIX
 * (syslogd, xwsh, telnetd, ...) compiled natively on this kernel agrees with
 * it.  The structures are LP64 versions: IRIX's n64 ABI, which is also what a
 * native x86-64 build looks like.
 */
#ifndef _UAPI_LINUX_IRIX_STROPTS_H
#define _UAPI_LINUX_IRIX_STROPTS_H

/* The IRIX syscall block: numbers 1000 and 1001 are statvfs64/fstatvfs64. */
#define __NR_irix_getmsg	1002
#define __NR_irix_putmsg	1003
#define __NR_irix_getpmsg	1004
#define __NR_irix_putpmsg	1005

#define FMNAMESZ	8	/* longest module name */

struct strbuf {
	int	maxlen;		/* size of buf */
	int	len;		/* bytes returned/supplied */
	char	*buf;
};

struct strioctl {
	int	ic_cmd;		/* command */
	int	ic_timout;	/* seconds; -1 forever, 0 = default (15) */
	int	ic_len;		/* length of data */
	char	*ic_dp;		/* data */
};

struct strpeek {
	struct strbuf	ctlbuf;
	struct strbuf	databuf;
	long		flags;
};

struct strfdinsert {
	struct strbuf	ctlbuf;
	struct strbuf	databuf;
	long		flags;
	int		fildes;
	int		offset;
};

struct bandinfo {
	unsigned char	bi_pri;
	int		bi_flag;
};

struct str_mlist {
	char	l_name[FMNAMESZ + 1];
};

struct str_list {
	int			sl_nmods;
	struct str_mlist	*sl_modlist;
};

struct strrecvfd {
	int		fd;
	unsigned int	uid;
	unsigned int	gid;
	char		__fill[8];
};

/* getmsg/putmsg flags */
#define RS_HIPRI	0x01
#define MSG_HIPRI	0x01
#define MSG_ANY		0x02
#define MSG_BAND	0x04
#define MORECTL		1
#define MOREDATA	2

/* write options (I_SWROPT) */
#define SNDZERO		0x001
#define SNDPIPE		0x002

/* read options (I_SRDOPT): mode in the low bits, protocol handling above */
#define RNORM		0x000
#define RMSGD		0x001
#define RMSGN		0x002
#define RMODEMASK	0x003
#define RPROTDAT	0x004
#define RPROTDIS	0x008
#define RPROTNORM	0x010
#define RPROTMASK	0x01c

/* I_FLUSH */
#define FLUSHR		0x01
#define FLUSHW		0x02
#define FLUSHRW		0x03
#define FLUSHBAND	0x04

/* I_SETSIG events */
#define S_INPUT		0x0001
#define S_HIPRI		0x0002
#define S_OUTPUT	0x0004
#define S_MSG		0x0008
#define S_ERROR		0x0010
#define S_HANGUP	0x0020
#define S_RDNORM	0x0040
#define S_WRNORM	S_OUTPUT
#define S_RDBAND	0x0080
#define S_WRBAND	0x0100
#define S_BANDURG	0x0200

/* I_ATMARK */
#define ANYMARK		0x01
#define LASTMARK	0x02

#define STR		(('S') << 8)
#define I_NREAD		(STR | 01)
#define I_PUSH		(STR | 02)
#define I_POP		(STR | 03)
#define I_LOOK		(STR | 04)
#define I_FLUSH		(STR | 05)
#define I_SRDOPT	(STR | 06)
#define I_GRDOPT	(STR | 07)
#define I_STR		(STR | 010)
#define I_SETSIG	(STR | 011)
#define I_GETSIG	(STR | 012)
#define I_FIND		(STR | 013)
#define I_LINK		(STR | 014)
#define I_UNLINK	(STR | 015)
#define I_RECVFD	(STR | 016)
#define I_E_RECVFD	(STR | 016)
#define I_PEEK		(STR | 017)
#define I_FDINSERT	(STR | 020)
#define I_SENDFD	(STR | 021)
#define I_SWROPT	(STR | 023)
#define I_GWROPT	(STR | 024)
#define I_LIST		(STR | 025)
#define I_PLINK		(STR | 026)
#define I_PUNLINK	(STR | 027)
#define I_FLUSHBAND	(STR | 034)
#define I_CKBAND	(STR | 035)
#define I_GETBAND	(STR | 036)
#define I_ATMARK	(STR | 037)
#define I_SETCLTIME	(STR | 040)
#define I_GETCLTIME	(STR | 041)
#define I_CANPUT	(STR | 042)
#define I_NREADN	(STR | 044)
#define I_PEEKN		(STR | 045)

/* The log driver (/dev/streams/log; <sys/strlog.h>) */
#define SL_FATAL	0x01
#define SL_NOTIFY	0x02
#define SL_ERROR	0x04
#define SL_TRACE	0x08
#define SL_CONSOLE	0x10
#define SL_WARN		0x20
#define SL_NOTE		0x40

#define LOGCTL		(('L') << 8)
#define I_TRCLOG	(LOGCTL | 1)	/* process is tracer */
#define I_ERRLOG	(LOGCTL | 2)	/* process is error logger */
#define I_CONSLOG	(LOGCTL | 3)	/* process is console logger */
#define I_CHKSYSLOGD	(LOGCTL | 4)	/* is syslogd running? */

struct log_ctl {
	short	mid;		/* module id */
	short	sid;		/* sub-id */
	char	level;		/* level of message for tracing */
	short	flags;		/* SL_* disposition */
	long	ltime;		/* ticks since boot */
	long	ttime;		/* seconds since 1970 */
	int	seq_no;		/* sequence number */
	int	pri;		/* priority = facility | level */
};

struct trace_ids {
	short	ti_mid;
	short	ti_sid;
	char	ti_level;
};

#endif /* _UAPI_LINUX_IRIX_STROPTS_H */
