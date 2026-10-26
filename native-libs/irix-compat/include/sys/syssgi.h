#ifndef _IRIX_SYS_SYSSGI_H
#define _IRIX_SYS_SYSSGI_H
#define SGI_SETNVRAM	8
#define SGI_GETNVRAM	9
#define SGI_NVSTRSIZE	4096
long irix_syssgi(int, ...);
#define syssgi irix_syssgi
#endif
