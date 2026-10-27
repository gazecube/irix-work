/* Definitions IRIX syslogd.c expects from IRIX headers that glibc lacks */
#ifndef _SYSLOGD_COMPAT_H
#define _SYSLOGD_COMPAT_H
#include <signal.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <fmtmsg.h>
#include <sys/capability.h>
#include <capability.h>
typedef void (*SIG_PF)(int);
#ifndef LOG_LFMT
#define LOG_LFMT	(14 << 3)	/* logalert facility (irix/kern/sys/syslog.h) */
#endif
#endif
