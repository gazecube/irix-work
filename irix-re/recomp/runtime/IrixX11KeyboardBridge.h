#ifndef IID_IRIX_X11_KEYBOARD_BRIDGE_H
#define IID_IRIX_X11_KEYBOARD_BRIDGE_H

#include "IrixGuest.h"

#ifdef __cplusplus
extern "C" {
#endif

void irix_x11_keyboard_bridge_init(IrixGuestRuntime *rt);
IrixGuestFunction irix_x11_keyboard_bridge_lookup(const char *name);

#ifdef __cplusplus
}
#endif

#endif
