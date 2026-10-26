#ifndef _IRIX_SYS_SYSTEMINFO_H
#define _IRIX_SYS_SYSTEMINFO_H
#define SI_SYSNAME	1
#define SI_HOSTNAME	2
#define SI_RELEASE	3
#define SI_VERSION	4
#define SI_MACHINE	5
long irix_sysinfo(int, char *, long);
#define sysinfo irix_sysinfo
#endif
