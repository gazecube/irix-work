/*
 * Original SgDropPocket built-in raster recovered byte-for-byte from the
 * IRIX N32 libSgm.so.1 DropPocket.c data image.
 *
 * The source variables are named file_bits and filemask_bits in the SGI
 * DWARF.  Both are 24x22 XBM-style bitmaps.  This file installs an expose
 * method overlay on the reconstructed SgDropPocket class so the host build
 * uses the actual SGI fallback artwork while the desktopFileicon renderer is
 * reconstructed separately.
 */

#include <Xm/PrimitiveP.h>
#include <Xm/Xm.h>
#include <X11/Intrinsic.h>
#include <X11/IntrinsicP.h>
#include <X11/Xlib.h>

#define SG_DP_FILE_WIDTH 24
#define SG_DP_FILE_HEIGHT 22

/* N32 libSgm.so.1 .data @ 0x0b24fc98, 66 bytes. */
static const unsigned char file_bits[] = {
    0x00,0x00,0x00,
    0xfe,0x0f,0x00,
    0x02,0x18,0x00,
    0x02,0x28,0x00,
    0x02,0x48,0x00,
    0x02,0x88,0x00,
    0x02,0xf8,0x01,
    0x02,0x00,0x01,
    0x02,0x00,0x01,
    0x02,0x00,0x01,
    0x02,0x00,0x01,
    0x02,0x00,0x01,
    0x02,0x00,0x01,
    0x02,0x00,0x01,
    0x02,0x00,0x01,
    0x02,0x00,0x01,
    0x02,0x00,0x01,
    0x02,0x00,0x01,
    0x02,0x00,0x01,
    0x02,0x00,0x01,
    0x02,0x00,0x01,
    0xfe,0xff,0x01
};

/* N32 libSgm.so.1 .data @ 0x0b24fce0, 66 bytes. */
static const unsigned char filemask_bits[] = {
    0xff,0x0f,0x00,
    0xff,0x1f,0x00,
    0xff,0x3f,0x00,
    0xff,0x7f,0x00,
    0xff,0xff,0x00,
    0xff,0xff,0x01,
    0xff,0xff,0x03,
    0xff,0xff,0x03,
    0xff,0xff,0x03,
    0xff,0xff,0x03,
    0xff,0xff,0x03,
    0xff,0xff,0x03,
    0xff,0xff,0x03,
    0xff,0xff,0x03,
    0xff,0xff,0x03,
    0xff,0xff,0x03,
    0xff,0xff,0x03,
    0xff,0xff,0x03,
    0xff,0xff,0x03,
    0xff,0xff,0x03,
    0xff,0xff,0x03,
    0xff,0xff,0x03
};

extern WidgetClass sgDropPocketNativeWidgetClass;

static XtExposeProc previousExpose = NULL;

static void drawRecoveredRaster(Widget w, XEvent *event, Region region)
{
    Display *dpy;
    Window win;
    Pixel foreground = 0;
    Pixel background = 0;
    Pixel top_shadow = 0;
    Pixel bottom_shadow = 0;
    Pixel active_pixel = 0;
    Dimension shadow = 0;
    Dimension width;
    Dimension height;
    Pixmap bitmap;
    Pixmap mask;
    GC gc;
    XGCValues gcv;
    int x;
    int y;

    (void)event;
    (void)region;

    if (!w || !XtIsRealized(w))
        return;

    dpy = XtDisplay(w);
    win = XtWindow(w);
    width = XtWidth(w);
    height = XtHeight(w);

    XtVaGetValues(w,
                  XmNforeground, &foreground,
                  XmNbackground, &background,
                  XmNtopShadowColor, &top_shadow,
                  XmNbottomShadowColor, &bottom_shadow,
                  XmNshadowThickness, &shadow,
                  "activePixel", &active_pixel,
                  NULL);

    gcv.foreground = foreground;
    gcv.background = background;
    gc = XCreateGC(dpy, win, GCForeground | GCBackground, &gcv);
    if (!gc)
        return;

    /* Match the reconstructed pocket recess, then place SGI's real raster. */
    XSetForeground(dpy, gc, background);
    XFillRectangle(dpy, win, gc, 0, 0, width, height);

    if (shadow < 1)
        shadow = 1;

    XSetForeground(dpy, gc, bottom_shadow);
    XFillRectangle(dpy, win, gc, 1, 1,
                   width > 2 ? width - 2 : 1, shadow);
    XFillRectangle(dpy, win, gc, 1, 1,
                   shadow, height > 2 ? height - 2 : 1);

    XSetForeground(dpy, gc, top_shadow);
    XFillRectangle(dpy, win, gc,
                   1, height > shadow ? height - shadow - 1 : 0,
                   width > 2 ? width - 2 : 1, shadow);
    XFillRectangle(dpy, win, gc,
                   width > shadow ? width - shadow - 1 : 0, 1,
                   shadow, height > 2 ? height - 2 : 1);

    bitmap = XCreateBitmapFromData(dpy, win, (const char *)file_bits,
                                   SG_DP_FILE_WIDTH, SG_DP_FILE_HEIGHT);
    mask = XCreateBitmapFromData(dpy, win, (const char *)filemask_bits,
                                 SG_DP_FILE_WIDTH, SG_DP_FILE_HEIGHT);

    if (bitmap != None && mask != None)
    {
        x = ((int)width - SG_DP_FILE_WIDTH) / 2;
        y = ((int)height - SG_DP_FILE_HEIGHT) / 2;

        XSetForeground(dpy, gc, active_pixel ? active_pixel : foreground);
        XSetBackground(dpy, gc, background);
        XSetClipMask(dpy, gc, mask);
        XSetClipOrigin(dpy, gc, x, y);
        XCopyPlane(dpy, bitmap, win, gc,
                   0, 0, SG_DP_FILE_WIDTH, SG_DP_FILE_HEIGHT,
                   x, y, 1L);
        XSetClipMask(dpy, gc, None);
    }

    if (bitmap != None)
        XFreePixmap(dpy, bitmap);
    if (mask != None)
        XFreePixmap(dpy, mask);
    XFreeGC(dpy, gc);
}

#if defined(__GNUC__)
__attribute__((constructor(202)))
#endif
static void installRecoveredDropPocketRaster(void)
{
    CoreClassRec *klass;

    if (!sgDropPocketNativeWidgetClass)
        return;

    klass = (CoreClassRec *)sgDropPocketNativeWidgetClass;
    previousExpose = klass->core_class.expose;
    (void)previousExpose;
    klass->core_class.expose = drawRecoveredRaster;
}
