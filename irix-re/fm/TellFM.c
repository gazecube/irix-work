#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include "TellFM.h"

#define FM_ARGS_ATOM "_FM_ARGS"

static int buildFmSelectionName(char *buffer, size_t bufferSize)
{
    char hostname[256];
    uid_t uid;

    if(buffer == NULL || bufferSize == 0)
        return 0;

    if(gethostname(hostname, sizeof(hostname)) != 0)
        return 0;

    hostname[sizeof(hostname) - 1] = '\0';
    uid = getuid();

    if(snprintf(buffer,
                bufferSize,
                "fm-%s-%lu",
                hostname,
                (unsigned long)uid) >= (int)bufferSize)
        return 0;

    return 1;
}

int sgiTellFMTransport(Display *display, int argc, char **argv)
{
    char selectionName[512];
    Atom selectionAtom;
    Atom argsAtom;
    Window owner;
    Window root;
    Window requestWindow;
    XTextProperty property;
    XEvent event;

    if(display == NULL || argc <= 0 || argv == NULL)
        return 0;

    if(!buildFmSelectionName(selectionName, sizeof(selectionName)))
        return 0;

    selectionAtom = XInternAtom(display, selectionName, False);
    if(selectionAtom == None)
        return 0;

    owner = XGetSelectionOwner(display, selectionAtom);
    if(owner == None)
        return 0;

    argsAtom = XInternAtom(display, FM_ARGS_ATOM, False);
    if(argsAtom == None)
        return 0;

    root = DefaultRootWindow(display);

    /*
     * Binary-proven request window geometry from the original TellFM():
     *
     *   XCreateSimpleWindow(dpy, root, -1, -1, 1, 1, 0, 0, 0)
     */
    requestWindow = XCreateSimpleWindow(display,
                                        root,
                                        -1,
                                        -1,
                                        1,
                                        1,
                                        0,
                                        0,
                                        0);
    if(requestWindow == None)
        return 0;

    XSelectInput(display, requestWindow, PropertyChangeMask);

    memset(&property, 0, sizeof(property));
    if(!XStringListToTextProperty(argv, argc, &property))
    {
        XDestroyWindow(display, requestWindow);
        return 0;
    }

    XSetTextProperty(display, requestWindow, &property, argsAtom);
    if(property.value != NULL)
        XFree(property.value);

    memset(&event, 0, sizeof(event));
    event.xclient.type = ClientMessage;
    event.xclient.display = display;
    event.xclient.window = owner;
    event.xclient.message_type = argsAtom;
    event.xclient.format = 32;
    event.xclient.data.l[0] = (long)requestWindow;

    if(!XSendEvent(display,
                   owner,
                   False,
                   NoEventMask,
                   &event))
    {
        XDestroyWindow(display, requestWindow);
        XFlush(display);
        return 0;
    }

    XFlush(display);

    /*
     * Binary-proven synchronous acknowledgement loop.  The fm receiver
     * deletes _FM_ARGS from our temporary request window when the request has
     * completed.  Original TellFM() waits in XNextEvent() until it sees:
     *
     *   type   == PropertyNotify
     *   window == requestWindow
     *   atom   == _FM_ARGS
     *   state  == PropertyDelete
     */
    for(;;)
    {
        XNextEvent(display, &event);

        if(event.type == PropertyNotify &&
           event.xproperty.window == requestWindow &&
           event.xproperty.atom == argsAtom &&
           event.xproperty.state == PropertyDelete)
            break;
    }

    XDestroyWindow(display, requestWindow);
    XFlush(display);
    return 1;
}
