/*
 * xclock for the X11R5-era Xaw Clock widget (as shipped with IRIX).
 * Option and resource names follow the MIT R5 client.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>
#include <X11/Shell.h>
#include <X11/Xaw/Clock.h>

extern WidgetClass clockWidgetClass;

static XrmOptionDescRec options[] = {
    {"-chime",     "*clock.chime",     XrmoptionNoArg,  "TRUE"},
    {"-hd",        "*clock.hands",     XrmoptionSepArg, NULL},
    {"-hands",     "*clock.hands",     XrmoptionSepArg, NULL},
    {"-hl",        "*clock.highlight", XrmoptionSepArg, NULL},
    {"-highlight", "*clock.highlight", XrmoptionSepArg, NULL},
    {"-update",    "*clock.update",    XrmoptionSepArg, NULL},
    {"-padding",   "*clock.padding",   XrmoptionSepArg, NULL},
    {"-d",         "*clock.analog",    XrmoptionNoArg,  "FALSE"},
    {"-digital",   "*clock.analog",    XrmoptionNoArg,  "FALSE"},
    {"-analog",    "*clock.analog",    XrmoptionNoArg,  "TRUE"},
};

static void usage(const char *name)
{
    fprintf(stderr,
            "Usage: %s [-analog] [-bw <pixels>] [-digital]\n"
            "       [-fg <color>] [-bg <color>] [-hd <color>]\n"
            "       [-hl <color>] [-bd <color>]\n"
            "       [-fn <font_name>] [-help] [-padding <pixels>]\n"
            "       [-rv] [-update <seconds>] [-display displayname]\n"
            "       [-geometry geom]\n", name);
    exit(1);
}

int main(int argc, char **argv)
{
    Widget toplevel;
    XtAppContext app;
    int i;

    for (i = 1; i < argc; ++i)
        if (!strcmp(argv[i], "-help") || !strcmp(argv[i], "-?"))
            usage(argv[0]);

    toplevel = XtAppInitialize(&app, "XClock", options, XtNumber(options),
                               &argc, argv, NULL, NULL, 0);
    if (argc != 1) usage(argv[0]);
    XtCreateManagedWidget("clock", clockWidgetClass, toplevel, NULL, 0);
    XtRealizeWidget(toplevel);
    XtAppMainLoop(app);
    return 0;
}
