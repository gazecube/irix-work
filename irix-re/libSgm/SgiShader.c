/*
 * SGI Motif shader reconstruction.
 *
 * Sources of truth:
 *   - IRIX Motif private header Xm/SgiP.h
 *   - IRIX N32 libXm.a(Sgi.o)
 *       _sgFindGC     @ 0x0f80
 *       _sgFindShader @ 0x1688
 *
 * SgiP.h names the original cache record fields:
 *   dpy, cmap, depth, pixel, r/g/b, faceShade, GCarray[9], next.
 *
 * The normal-depth color arithmetic below is instruction-for-instruction
 * equivalent to the path in Sgi.o.  The original depth-2 tiled path is kept
 * separate and is not approximated here.
 */

#include "SgiShader.h"

#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>
#include <dlfcn.h>
#include <stdlib.h>

#define SG_SHADER_LEVELS 8
#define SG_SHADER_GC_SLOTS 9

/* Exact integer tables from libXm.a(Sgi.o) .data. */
static const int sgLightRamp[SG_SHADER_LEVELS] =
{
    0, 42, 85, 128, 170, 213, 230, 255
};

static const int sgDarkRamp[SG_SHADER_LEVELS] =
{
    0, 32, 64, 96, 128, 160, 190, 255
};

typedef struct SgShaderRec
{
    Display *dpy;
    Colormap cmap;
    Cardinal depth;
    Pixel pixel;
    short r;
    short g;
    short b;
    int face_shade;
    GC gc_array[SG_SHADER_GC_SLOTS];
    struct SgShaderRec *next;
} SgShaderRec;

static SgShaderRec *shader_cache;

static int currentDarkScheme(void)
{
    static int *dark_scheme;
    static Boolean looked_up;

    if (!looked_up)
    {
        dark_scheme = (int *)dlsym(RTLD_DEFAULT, "darkScheme");
        looked_up = True;
    }

    return dark_scheme ? (*dark_scheme != 0) : 0;
}

static const int *shaderRamp(void)
{
    return currentDarkScheme() ? sgDarkRamp : sgLightRamp;
}

static SgShaderRec *findShaderRecord(Widget widget, Pixel background)
{
    SgShaderRec *rec;
    Display *dpy;
    Colormap cmap = None;
    Cardinal depth = 0;
    XColor color;
    const int *ramp;

    if (!widget)
        return NULL;

    dpy = XtDisplay(widget);
    XtVaGetValues(widget,
                  XtNcolormap, &cmap,
                  XtNdepth, &depth,
                  NULL);

    /*
     * Exact original lookup key from _sgFindShader:
     * dpy, cmap, depth, background pixel.  darkScheme is deliberately NOT
     * part of the cache key in IRIX libXm.
     */
    for (rec = shader_cache; rec; rec = rec->next)
    {
        if (rec->dpy == dpy &&
            rec->cmap == cmap &&
            rec->depth == depth &&
            rec->pixel == background)
            return rec;
    }

    rec = (SgShaderRec *)calloc(1, sizeof(*rec));
    if (!rec)
        return NULL;

    color.pixel = background;
    XQueryColor(dpy, cmap, &color);
    ramp = shaderRamp();

    rec->dpy = dpy;
    rec->cmap = cmap;
    rec->depth = depth;
    rec->pixel = background;

    /* _sgFindShader stores the high eight bits in signed-short fields. */
    rec->r = (short)(color.red >> 8);
    rec->g = (short)(color.green >> 8);
    rec->b = (short)(color.blue >> 8);
    rec->face_shade = ramp[4];

    rec->next = shader_cache;
    shader_cache = rec;
    return rec;
}

static short shadeChannel(short channel, int target, int face_shade, int maximum)
{
    int value;

    if (target == face_shade)
        return channel;

    if (target < face_shade)
    {
        if (!face_shade)
            return 0;
        value = ((int)channel * target) / face_shade;
        return (short)value;
    }

    if (maximum == face_shade)
        return channel;

    value = (int)channel +
            (((maximum - (int)channel) * (target - face_shade)) /
             (maximum - face_shade));
    return (short)value;
}

static GC normalShaderGC(Widget widget, SgShaderRec *rec, int shader_index)
{
    const int *ramp;
    int target;
    int maximum;
    XColor color;
    XGCValues values;

    ramp = shaderRamp();
    target = ramp[shader_index];
    maximum = ramp[SG_SHADER_LEVELS - 1];

    if (target == rec->face_shade)
    {
        color.pixel = rec->pixel;
    }
    else
    {
        short r = shadeChannel(rec->r, target, rec->face_shade, maximum);
        short g = shadeChannel(rec->g, target, rec->face_shade, maximum);
        short b = shadeChannel(rec->b, target, rec->face_shade, maximum);

        /* Exact Sgi.o conversion: eight-bit shader component << 8. */
        color.red = (unsigned short)((unsigned short)r << 8);
        color.green = (unsigned short)((unsigned short)g << 8);
        color.blue = (unsigned short)((unsigned short)b << 8);
        color.flags = DoRed | DoGreen | DoBlue;
        color.pixel = rec->pixel;

        /*
         * Exact normal-path failure behavior: if XAllocColor fails, SGI uses
         * the original background pixel. _sgAllocClosestColor is NOT called
         * here; it belongs to the depth-2 support-color path.
         */
        if (!XAllocColor(rec->dpy, rec->cmap, &color))
            color.pixel = rec->pixel;
    }

    values.foreground = color.pixel;
    values.background = color.pixel;
    return XtGetGC(widget, GCForeground | GCBackground, &values);
}

GC SgShaderGetGC(Widget widget, Pixel background, int shader_index)
{
    SgShaderRec *rec;

    if (!widget)
        return NULL;

    if (shader_index < 0 || shader_index >= SG_SHADER_LEVELS)
        shader_index = 4;

    rec = findShaderRecord(widget, background);
    if (!rec)
        return NULL;

    if (rec->gc_array[shader_index])
        return rec->gc_array[shader_index];

    /*
     * In Sgi.o, depth == 2 selects a separate jump-table implementation for
     * levels 1,2,3,5,6 using the 50_foreground/75_foreground tiled pixmaps.
     * Levels 0,4,7 still use the normal arithmetic path.  Until that branch
     * is reproduced exactly, do not substitute an invented approximation.
     */
    if (rec->depth == 2 &&
        shader_index != 0 && shader_index != 4 && shader_index != 7)
        return NULL;

    rec->gc_array[shader_index] = normalShaderGC(widget, rec, shader_index);
    return rec->gc_array[shader_index];
}
