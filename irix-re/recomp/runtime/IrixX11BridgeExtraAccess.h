#ifndef IRIX_X11_BRIDGE_EXTRA_ACCESS_H
#define IRIX_X11_BRIDGE_EXTRA_ACCESS_H

#include "IrixGuest.h"
#include <X11/Xlib.h>

Display *irix_x11_primary_display_from_guest(IrixGuestAddr addr);
GC irix_x11_primary_gc_from_guest(IrixGuestAddr addr);
XFontStruct *irix_x11_primary_font_from_guest(IrixGuestAddr addr);
IrixGuestAddr irix_x11_primary_font_to_guest(IrixGuestRuntime *rt, XFontStruct *font);
void irix_x11_primary_forget_font(IrixGuestAddr addr);
IrixGuestAddr irix_x11_primary_create_gc(IrixGuestRuntime *rt,
                                         IrixGuestAddr display_addr,
                                         uint32_t drawable,
                                         uint32_t mask,
                                         IrixGuestAddr values_addr);
int irix_x11_primary_free_gc(IrixGuestRuntime *rt,
                             IrixGuestAddr display_addr,
                             IrixGuestAddr guest_gc);

/*
 * IRIX's X server never reports NumLock/ScrollLock as modifiers, so Motif and
 * Xt programs match modifier state exactly.  Linux XKB puts NumLock in Mod2,
 * which would make every click look like Button+Mod2 and miss the bindings.
 * The bridges hide the lock modifiers from the guest (event/query state) and
 * duplicate passive grabs across every lock combination so the server still
 * matches them.
 */
unsigned int irix_x11_lock_modifier_mask(Display *d);
unsigned int irix_x11_strip_lock_modifiers(Display *d, unsigned int state);
/* Calls fn(state) for `state` and each combination of lock modifiers added. */
void irix_x11_for_each_lock_combo(Display *d, unsigned int state,
                                  void (*fn)(void *ctx, unsigned int state),
                                  void *ctx);

#endif
