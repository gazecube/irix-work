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
        return 1;

    desktop = getDesktopWindow(display, root);
    if(desktop == None)
        return 1;

    property = XInternAtom(display, SGI_DESKTOP_COMMAND_ATOM, False);
    if(property == None)
        return 1;

    length = strlen(command);

    /*
     * Receiver-proven protocol:
     *
     *   desktop window
     *     _SGI_DESKS_DESKTOP_COMMAND : XA_STRING / format 8
     *
     * Replacing this property produces the PropertyNotify consumed by fm.
     * The exact historical sender element count is still pending the original
     * library binary; strlen(command) is interoperable with the proven fm
     * XGetWindowProperty(XA_STRING) receiver.
     */
    XChangeProperty(display,
                    desktop,
                    property,
                    XA_STRING,
                    8,
                    PropModeReplace,
                    (const unsigned char *)command,
                    (int)length);

    XFlush(display);
    return 0;
}
