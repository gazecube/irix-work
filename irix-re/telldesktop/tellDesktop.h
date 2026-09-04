#ifndef IRIX_RE_TELLDESKTOP_H
#define IRIX_RE_TELLDESKTOP_H

#include <X11/Xlib.h>

/*
 * Reconstructed original call boundary.
 *
 * The IRIX telldesktop wrapper proves three arguments are passed:
 *
 *     Display *display
 *     Window root
 *     char *command
 *
 * The original caller ignores the return register, so the precise original
 * return type is not yet claimed here.  int is retained as a portable ABI
 * placeholder until matching DWARF or the implementation proves otherwise.
 */
int tellDesktop(Display *display, Window root, char *command);

#endif
