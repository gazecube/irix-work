#ifndef IRIX_RE_TELLDESKTOP_H
#define IRIX_RE_TELLDESKTOP_H

#include <X11/Xlib.h>

/*
 * Reconstructed public boundary for the original libimdXutil tellDesktop()
 * transport.  The exact original return convention is still under RE; the
 * portable implementation uses 0 for success and nonzero for failure.
 */
int tellDesktop(Display *display, const char *command);

#endif
