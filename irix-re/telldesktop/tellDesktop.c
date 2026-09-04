#include <stddef.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>

#include "tellDesktop.h"
#include "desktopWindow.h"

#define SGI_DESKTOP_COMMAND_ATOM "_SGI_DESKS_DESKTOP_COMMAND"

int tellDesktop(Display *display, Window root, char *command)
{
    Window desktop;
    Atom property;
    size_t length;

    if(display == NULL || root == None || command == NULL)
        return 0;

    /*
     * Binary-proven original path:
     *
     *     desktop = getDesktopWindow(display, root)
     *
     * and return 0 immediately when no desktop window is registered.
     */
    desktop = getDesktopWindow(display, root);
    if(desktop == None)
        return 0;

    property = XInternAtom(display, SGI_DESKTOP_COMMAND_ATOM, False);

    /*
     * Binary-proven original sender format:
     *
     *   property = _SGI_DESKS_DESKTOP_COMMAND
     *   type     = XA_STRING
     *   format   = 8
     *   mode     = PropModeReplace
     *   data     = command
     *   count    = strlen(command), exactly (no trailing NUL)
     */
    length = strlen(command);

    XChangeProperty(display,
                    desktop,
                    property,
                    XA_STRING,
                    8,
                    PropModeReplace,
                    (const unsigned char *)command,
                    (int)length);

    XFlush(display);
    return 1;
}
