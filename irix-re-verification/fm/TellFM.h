#ifndef IRIX_RE_FM_TELLFM_H
#define IRIX_RE_FM_TELLFM_H

#include <X11/Xlib.h>

/*
 * Portable reconstruction of the newer SGI fm service path.
 *
 * Original exported boundary:
 *
 *   TellFM(Display *, WidgetRec *, int, char **)
 *
 * The original function combines argument preparation, fmserv fallback/UI,
 * and the X11 request transport.  These are split here so the exact wire
 * protocol can stay reusable and testable.
 */

typedef struct SgiTellFMArgs
{
    int argc;
    char **argv;
    char *ownedPath;
} SgiTellFMArgs;

int sgiTellFMPrepareArgs(int argc, char **argv, SgiTellFMArgs *prepared);
void sgiTellFMFreeArgs(SgiTellFMArgs *prepared);

/* Exact X11 request transport once an fm service owner already exists. */
int sgiTellFMTransport(Display *display, int argc, char **argv);

/* Prepare arguments, send them, then release temporary preparation storage. */
int sgiTellFM(Display *display, int argc, char **argv);

#endif /* IRIX_RE_FM_TELLFM_H */
