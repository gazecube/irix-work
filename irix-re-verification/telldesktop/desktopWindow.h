#ifndef IRIX_RE_DESKTOP_WINDOW_H
#define IRIX_RE_DESKTOP_WINDOW_H

#include <X11/Xlib.h>

/*
 * Return the desktop/file-manager window published on root through
 * _SGI_DESKS_DESKTOP_WINDOW, or None when no desktop is registered.
 */
Window getDesktopWindow(Display *display, Window root);

#endif
