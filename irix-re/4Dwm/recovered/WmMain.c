/*
 * 4Dwm WmMain recovery notes / source scaffold.
 *
 * Upstream architecture: OSF/Motif mwm WmMain.c.
 * IRIX authority: /usr/bin/X11/4Dwm main @ 0x1002a974, size 0x1a0.
 *
 * This file deliberately stops short of pretending the SGI WmGlobalData
 * layout has been recovered. The control-flow below is binary-backed; field
 * names remain tied to the upstream baseline until their SGI offsets/types are
 * independently recovered.
 */

#include "WmMain.h"

/*
 * Binary-backed control-flow skeleton:
 *
 *   XtSetLanguageProc(NULL, NULL, NULL);
 *   InitWmGlobal(argc, argv, environ);
 *
 *   for (;;) {
 *       XtAppNextEvent(wmGD.mwmAppContext, &event);
 *       wmGD.attributesWindow = 0;
 *
 *       // IRIX additionally normalizes button modifier state using a
 *       // per-button mask table before menu dispatch.
 *
 *       dispatchEvent = True;
 *       if (wmGD.menuActive && SGI menu-preprocess conditions)
 *           dispatchEvent = WmDispatchMenuEvent((XButtonEvent *)&event);
 *
 *       if (dispatchEvent) {
 *           if (event belongs to a managed root/workspace)
 *               dispatchEvent = WmDispatchWsEvent(&event);
 *           else
 *               dispatchEvent = WmDispatchClientEvent(&event);
 *
 *           if (dispatchEvent)
 *               XtDispatchEvent(&event);
 *       }
 *   }
 *
 * Exact SGI field offsets observed in main (relative to the global WM record)
 * include 0x18, 0x20, 0x28, 0x68, 0x7c, 0x80, 0xdc, 0xf8, 0xfc, 0x191,
 * 0x1c8 and the modifier-mask table beginning at 0x280. These are evidence,
 * not yet source-level member declarations.
 */
