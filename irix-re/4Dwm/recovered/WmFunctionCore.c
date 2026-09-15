/*
 * First machine-code-complete 4Dwm core routines.
 *
 * IRIX 6.5 /usr/bin/X11/4Dwm is based on OSF/Motif 1.2.4#sgi1.0.
 * These bodies are written from the IRIX MIPS implementation; the pinned mwm
 * tree is used to recover original public signatures and classify lineage.
 */

#include <X11/Intrinsic.h>
#include <X11/Xlib.h>

typedef struct _ClientData ClientData;

/* MWM-BASE: IRIX bodies are the same semantic one-line sentinels. */
Boolean
F_Nop(String args, ClientData *pCD, XEvent *event)
{
    (void)args;
    (void)pCD;
    (void)event;
    return True;
}

Boolean
F_Separator(String args, ClientData *pCD, XEvent *event)
{
    (void)args;
    (void)pCD;
    (void)event;
    return True;
}

Boolean
F_Title(String args, ClientData *pCD, XEvent *event)
{
    (void)args;
    (void)pCD;
    (void)event;
    return True;
}

/* SGI-NEW relative to the pinned mwm tree. Body @ 0x10060350 is return True. */
Boolean
F_Inactive(String args, ClientData *pCD, XEvent *event)
{
    (void)args;
    (void)pCD;
    (void)event;
    return True;
}

/*
 * MWM-MODIFIED: upstream WmEvent.c synthesizes a server timestamp. SGI's
 * exported 4Dwm body @ 0x100204e8 is exactly `return 0` (CurrentTime).
 */
Time
GetTimestamp(void)
{
    return CurrentTime;
}

/* IRIX body @ 0x1001fee0 is intentionally empty. */
void
WmXtWarningHandler(char *message)
{
    (void)message;
}
