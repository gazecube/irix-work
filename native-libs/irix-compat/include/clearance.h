#ifndef _IRIX_CLEARANCE_H
#define _IRIX_CLEARANCE_H
#include <capability.h>
struct clearance { char *cl_name; };
struct clearance *sgi_getclearancebyname(const char *);
#endif
