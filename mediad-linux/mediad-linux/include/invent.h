#pragma once
#include <sys/invent.h>
#ifdef __cplusplus
extern "C" {
#endif
int setinvent(void);
inventory_t *getinvent(void);
void endinvent(void);
#ifdef __cplusplus
}
#endif
