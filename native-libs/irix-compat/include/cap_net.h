/* IRIX <cap_net.h>: cap_bind() is bind() with CAP_PRIV_PORT acquired; the
 * Linux capability bits are already in place, so plain bind() */
#ifndef _IRIX_CAP_NET_H
#define _IRIX_CAP_NET_H
#include <sys/types.h>
#include <sys/socket.h>
static inline int cap_bind(int s, const struct sockaddr *a, socklen_t l)
{
	return bind(s, a, l);
}
#endif
