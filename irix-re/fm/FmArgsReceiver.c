#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include "FmArgsReceiver.h"

#define FM_ARGS_ATOM "_FM_ARGS"

int fmHandleArgsClientMessage(Display *display,
                             const XClientMessageEvent *event,
                             FmArgsDispatchFn dispatch,
                             void *context)
{
    Atom argsAtom;
    Window requestWindow;
    XTextProperty property;
    char **argv = NULL;
    int argc = 0;
    int consumed = 0;

    if(display == NULL || event == NULL)
        return 0;

    if(event->type != ClientMessage || event->format != 32)
        return 0;

    argsAtom = XInternAtom(display, FM_ARGS_ATOM, False);
    if(argsAtom == None || event->message_type != argsAtom)
        return 0;

    requestWindow = (Window)event->data.l[0];
    if(requestWindow == None)
        return 0;

    property.value = NULL;
    property.encoding = None;
    property.format = 0;
    property.nitems = 0;

    if(!XGetTextProperty(display, requestWindow, &property, argsAtom))
        goto ack;

    if(XTextPropertyToStringList(&property, &argv, &argc))
    {
        if(dispatch != NULL)
            (void)dispatch(context, argc, argv);
        consumed = 1;
    }

    if(argv != NULL)
        XFreeStringList(argv);

    if(property.value != NULL)
        XFree(property.value);

ack:
    /* Original FM completion acknowledgement. */
    XDeleteProperty(display, requestWindow, argsAtom);
    XFlush(display);
    return consumed;
}
