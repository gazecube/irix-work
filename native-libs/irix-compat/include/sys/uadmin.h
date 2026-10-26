#ifndef _IRIX_SYS_UADMIN_H
#define _IRIX_SYS_UADMIN_H
#define A_REBOOT	1
#define A_SHUTDOWN	2
#define A_REMOUNT	4
#define A_KILLALL	8
#define A_SETFLAG	256
#define AD_HALT		0
#define AD_BOOT		1
#define AD_IBOOT	2
#define AD_EXEC		3
#define AD_POWEROFF	9
int uadmin(int, int, int);
#endif
