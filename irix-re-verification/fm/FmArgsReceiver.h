#ifndef IRIX_RE_FM_ARGS_RECEIVER_H
#define IRIX_RE_FM_ARGS_RECEIVER_H

#include <X11/Xlib.h>

/*
 * Callback invoked with the argv reconstructed from the original SGI _FM_ARGS
 * request property. Return nonzero when the request was accepted.
 */
typedef int (*FmArgsDispatchFn)(void *context, int argc, char **argv);

/*
 * Handle one ClientMessage using the original SGI FM request protocol.
 * Returns 1 when the event was an _FM_ARGS request and was consumed, 0 when
 * the event is unrelated or malformed.
 */
int fmHandleArgsClientMessage(Display *display,
                             const XClientMessageEvent *event,
                             FmArgsDispatchFn dispatch,
                             void *context);

#endif /* IRIX_RE_FM_ARGS_RECEIVER_H */
