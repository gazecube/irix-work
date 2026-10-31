#ifndef ARCS_UEFI_BOOTANIM_H
#define ARCS_UEFI_BOOTANIM_H

#include <Uefi.h>

/*
 * Menu -> flyout transition for the System Maintenance Menu.
 *
 * The original SGI drawing code is indexed colour with no alpha, scaling or
 * read-back, so the transition is done one layer below it, on the true-colour
 * GOP framebuffer: the screen before (Full), the background alone (Back) and
 * the finished flyout (Final) are captured -- the last two by pointing the GUI
 * backend at an in-memory GOP, so nothing flashes -- and the frames in between
 * are composed in memory and blitted.  The resting states are always drawn by
 * the original GUI code.
 *
 * One motion, start to finish (850 ms).  The clicked option travels first: it drops
 * down to its height, then slides across and lands beside the flyout at 45% of
 * the time (ease-in-out on each leg); the other options
 * are pulled into it as it moves, nearest first -- labels behind it, icons
 * dissolving inside its icon, each one's opacity tied to how far it has
 * travelled.  Only once the title is in place does the flyout open: a fast
 * vertical raster line flashes in at the panel's edge and sweeps it out (the
 * panel itself does not fade).  No bounce.  Progress follows the clock, not
 * the frame count; any key skips to the end; the reverse plays the same
 * timeline backwards.
 */

typedef struct {
    int X1, Y1, X2, Y2;             /* GUI coordinates (y up), inclusive */
} ARCS_RECT;

typedef struct {
    ARCS_RECT Title;                /* where the clicked option ends up */
    ARCS_RECT Panel;                /* the raised choices panel */
} ARCS_ANIM_GEOM;

typedef VOID (*ARCS_ANIM_FN)(VOID);
typedef VOID (*ARCS_ANIM_GEOM_FN)(ARCS_ANIM_GEOM *Geom);

/* Play menu -> flyout.  BuildFlyout draws the finished flyout with the normal
 * GUI code (it is called while drawing is redirected off screen).  Returns
 * FALSE, having drawn nothing, if the animation cannot run; the caller then
 * builds the flyout the ordinary way. */
BOOLEAN ArcsAnimOpen(ARCS_ANIM_FN BuildFlyout, ARCS_ANIM_GEOM_FN GetGeom,
                     UINTN Clicked, EFI_SYSTEM_TABLE *SystemTable);

/* Play flyout -> menu.  DestroyFlyout tears the flyout's GUI objects down
 * (also redirected off screen). */
VOID ArcsAnimClose(ARCS_ANIM_FN DestroyFlyout, EFI_SYSTEM_TABLE *SystemTable);

/* Release the buffers. */
VOID ArcsAnimEnd(VOID);

#endif
