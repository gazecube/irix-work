#ifndef IRIX_RE_TELLDESKTOP_H
#define IRIX_RE_TELLDESKTOP_H

#include <X11/Xlib.h>

/*
 * Binary-proven original libimdXutil boundary:
 *
 *     int tellDesktop(Display *display, Window root, char *command)
 *
 * The implementation returns 0 when no desktop window is registered and 1
 * after sending the command property successfully.  The standalone original
 * telldesktop wrapper ignores this return value and exits 0 after the call.
 */
int tellDesktop(Display *display, Window root, char *command);

#endif
