#include <stddef.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>

#include "tellDesktop.h"

#define SGI_DESKTOP_WINDOW_ATOM  "_SGI_DESKS_DESKTOP_WINDOW"
#define SGI_DESKTOP_COMMAND_ATOM "_SGI_DESKS_DESKTOP_COMMAND"

/*
 * Reconstructed inverse of the original setWindowAsProperty() publication
 * used by fm.  The original helper name in later IID libraries is
 * getDesktopWindow()/getDtWindow.c++.
 */
static Window getDesktopWindow(Display *display, Window root)
{
    Atom property;
    Atom actual_type;
    int actual_format;
    unsigned long nitems;
    unsigned long bytes_after;
    unsigned char *data;
    Window desktop;
    int status;

    property = XInternAtom(display, SGI_DESKTOP_WINDOW_ATOM, False);
    if(property == None)
        return None;

    actual_type = None;
    actual_format = 0;
    nitems = 0;
    bytes_after = 0;
    data = NULL;

    status = XGetWindowProperty(display,
                                root,
                                property,
                                0,
                                1,
                                False,
                                XA_WINDOW,
                                &actual_type,
                                &actual_format,
                                &nitems,
                                &bytes_after,
                                &data);

    if(status != Success ||
       data == NULL ||
       actual_type != XA_WINDOW ||
       actual_format != 32 ||
       nitems < 1)
    {
        if(data != NULL)
            XFree(data);
        return None;
    }

    desktop = *((Window *)data);
    XFree(data);
    return desktop;
}

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
