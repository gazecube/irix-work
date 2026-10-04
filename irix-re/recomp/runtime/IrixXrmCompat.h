#ifndef IID_IRIX_XRM_COMPAT_H
#define IID_IRIX_XRM_COMPAT_H

#include "IrixGuest.h"
#include <X11/Xlib.h>

#ifdef __cplusplus
extern "C" {
#endif

void irix_xrm_compat_init(IrixGuestRuntime *rt);
IrixGuestFunction irix_xrm_compat_lookup(const char *name);
Display *irix_xrm_compat_display(void);
int irix_xrm_compat_is_database_handle(IrixGuestRuntime *rt,
                                       IrixGuestAddr address);

#ifdef __cplusplus
}
#endif

#endif
