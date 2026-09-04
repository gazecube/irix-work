#include <stdio.h>
#include <stdlib.h>
#include <X11/Xlib.h>

#include "tellDesktop.h"

int main(int argc, char **argv)
{
    Display *display;
    int rc;

    if(argc != 2)
    {
        fprintf(stderr, "usage: %s command\n", argv[0]);
        return 1;
    }

    display = XOpenDisplay(NULL);
    if(display == NULL)
    {
        const char *name = getenv("DISPLAY");
        if(name == NULL) name = "";

        fprintf(stderr,
                "telldesktop (%s):  Can't open display\n",
                name);
        return 1;
    }

    rc = tellDesktop(display, argv[1]);
    XCloseDisplay(display);

    return rc;
}
