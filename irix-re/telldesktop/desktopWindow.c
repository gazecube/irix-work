#include <X11/Xlib.h>
#include <X11/Xatom.h>

#include "desktopWindow.h"

#define SGI_DESKTOP_WINDOW_ATOM "_SGI_DESKS_DESKTOP_WINDOW"

Window getDesktopWindow(Display *display, Window root)
{
    Atom property;
    Atom actual_type;
    int actual_format;
    unsigned long nitems;
    unsigned long bytes_after;
    unsigned char *data;
    Window desktop;
    int status;

    if(display == NULL || root == None)
        return None;

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
