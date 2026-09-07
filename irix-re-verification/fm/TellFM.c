#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include "TellFM.h"

#define FM_ARGS_ATOM "_FM_ARGS"
#define SGI_TELLFM_PATHBUF 1024

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

/*
 * Portable stand-in for ImdGlobalEnv::isURL(), which is the exact helper used
 * by the original TellFM().  Keep this isolated so the real Imd URL policy can
 * replace it later without changing argument preparation or transport.
 */
static int sgiTellFMLooksLikeURL(const char *value)
{
    const unsigned char *p;

    if(value == NULL || !isalpha((unsigned char)value[0]))
        return 0;

    p = (const unsigned char *)value + 1;
    while(*p != '\0')
    {
        if(*p == ':')
            return 1;
        if(!(isalnum(*p) || *p == '+' || *p == '-' || *p == '.'))
            return 0;
        ++p;
    }

    return 0;
}

void sgiTellFMFreeArgs(SgiTellFMArgs *prepared)
{
    if(prepared == NULL)
        return;

    free(prepared->ownedPath);
    free(prepared->argv);
    prepared->argc = 0;
    prepared->argv = NULL;
    prepared->ownedPath = NULL;
}

int sgiTellFMPrepareArgs(int argc, char **argv, SgiTellFMArgs *prepared)
{
    char **copy;
    const char *last;
    int preparedArgc;

    if(prepared == NULL || argc <= 0 || argv == NULL)
        return 0;

    memset(prepared, 0, sizeof(*prepared));

    /* Original allocates room for argc + one possible cwd argument + NULL. */
    copy = (char **)calloc((size_t)argc + 2, sizeof(char *));
    if(copy == NULL)
        return 0;

    memcpy(copy, argv, (size_t)argc * sizeof(char *));
    preparedArgc = argc;
    last = argv[argc - 1];

    /*
     * Binary-proven TellFM policy:
     *
     *   argc == 1 OR trailing argument begins '-'
     *       -> append getcwd(..., 1024)
     */
    if(argc == 1 || (last != NULL && last[0] == '-'))
    {
        char *cwd = (char *)malloc(SGI_TELLFM_PATHBUF);
        if(cwd != NULL && getcwd(cwd, SGI_TELLFM_PATHBUF) != NULL)
        {
            copy[preparedArgc++] = cwd;
            prepared->ownedPath = cwd;
        }
        else
        {
            free(cwd);
        }
    }
    /*
     * If the trailing target is relative and is not an Imd URL, original
     * TellFM allocates a 1024-byte buffer and tries realpath().  A failed
     * realpath leaves the original argument in place.
     */
    else if(last != NULL && last[0] != '/' && !sgiTellFMLooksLikeURL(last))
    {
        char *resolved = (char *)malloc(SGI_TELLFM_PATHBUF);
        if(resolved != NULL)
        {
            if(realpath(last, resolved) != NULL)
            {
                copy[preparedArgc - 1] = resolved;
                prepared->ownedPath = resolved;
            }
            else
            {
                free(resolved);
            }
        }
    }

    copy[preparedArgc] = NULL;
    prepared->argc = preparedArgc;
    prepared->argv = copy;
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

int sgiTellFM(Display *display, int argc, char **argv)
{
    SgiTellFMArgs prepared;
    int result;

    if(!sgiTellFMPrepareArgs(argc, argv, &prepared))
        return 0;

    result = sgiTellFMTransport(display, prepared.argc, prepared.argv);
    sgiTellFMFreeArgs(&prepared);
    return result;
}
