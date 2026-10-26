/* IRIX <capability.h>: the /etc/capability user database. */
#ifndef _IRIX_CAPABILITY_H
#define _IRIX_CAPABILITY_H
#include <sys/capability.h>

#define USER_CAPABILITY	"/etc/capability"

struct user_cap {
	char *ca_name;		/* Name */
	char *ca_default;	/* Default Capability */
	char *ca_allowed;	/* Allowed Capability */
};

struct user_cap *sgi_getcapabilitybyname(const char *);
#endif
