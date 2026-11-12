/* the syssgi commands used by the IRIX source files built here (values from irix/kern/sys/syssgi.h) */
#ifndef IRIXSRC_SYSSGI_H
#define IRIXSRC_SYSSGI_H
#define SGI_INVENT	5	/* hardware inventory */
#define SGI_INV_SIZEOF	1	/* get sizeof inventory struct */
#define SGI_INV_READ	2	/* read inventory table */
extern long syssgi(int, ...);
#endif
