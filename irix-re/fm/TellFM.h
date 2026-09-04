#ifndef IRIX_RE_FM_TELLFM_H
#define IRIX_RE_FM_TELLFM_H

#include <X11/Xlib.h>

/*
 * Portable transport core for the newer SGI fm service protocol recovered
 * from the original IRIX fm binary.
 *
 * The original exported function is mangled as:
 *
 *   TellFM__GP9_XDisplayP10_WidgetReciPPc
 *
 * corresponding to:
 *
 *   TellFM(Display *, WidgetRec *, int, char **)
 *
 * The original Widget parameter participates in UI/error handling and fmserv
 * fallback.  The transport itself only needs Display + argv, so it is kept
 * behind this smaller reusable boundary.
 *
 * Returns 1 after receiving the original PropertyDelete completion ack,
 * 0 on transport/setup failure.
 */
int sgiTellFMTransport(Display *display, int argc, char **argv);

#endif /* IRIX_RE_FM_TELLFM_H */
