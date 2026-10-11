#ifndef IRIX_X11_COLOR_H
#define IRIX_X11_COLOR_H

#include "IrixGuest.h"
#include <X11/Xlib.h>

int irix_x11_guest_named_color(IrixGuestRuntime *rt, const char *name,
                               XColor *color);
int irix_x11_parse_color(IrixGuestRuntime *rt, Display *display, Colormap cmap,
                         const char *name, XColor *color);
int irix_x11_alloc_named_color(IrixGuestRuntime *rt, Display *display,
                               Colormap cmap, const char *name,
                               XColor *screen, XColor *exact);
int irix_x11_lookup_color(IrixGuestRuntime *rt, Display *display, Colormap cmap,
                          const char *name, XColor *exact, XColor *screen);

#endif
