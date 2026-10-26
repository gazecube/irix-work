#ifndef _IRIX_SYS_SYSMP_H
#define _IRIX_SYS_SYSMP_H
#define MP_KERNADDR	10
#define MPKA_VAR	1
long irix_sysmp(int, ...);
#define sysmp irix_sysmp
#endif
