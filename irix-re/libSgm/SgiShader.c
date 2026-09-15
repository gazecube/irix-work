/*
 * SGI Motif shader reconstruction.
 *
 * Binary source of truth:
 *   IRIX N32 libXm.so.1
 *     _sgFindShader @ 0x0b2947f0
 *     _sgFindGC     @ 0x0b2a8be0
 *
 * Recovered normal-depth behavior:
 *   - shader records are keyed by display/colormap/depth/background pixel
 *   - XQueryColor() supplies the background RGB
 *   - shader slot 4 is the reference/background level
 *   - darker slots scale RGB toward black
 *   - lighter slots interpolate RGB toward white
 *   - resulting 8-bit channels are shifted left by 8 into XColor
 *   - foreground and background of the returned Xt GC use the same pixel
 *
 * The original depth-2 path has additional SGI stipple handling using the
 * named pixmaps "50_foreground" and "75_foreground".  That path remains a
 * separate RE item; modern TrueColor/PseudoColor operation uses the fully
 * recovered path below.
 */

#include "SgiShader.h"

#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>
#include <dlfcn.h>
#include <limits.h>
#include <stdlib.h>

#define SG_SHADER_COUNT 8

/* Tables recovered from the original libXm data referenced by _sgFindGC. */
static const unsigned short sgLightRamp[SG_SHADER_COUNT] =
{
    0, 42, 85, 128, 170, 213, 230, 255
};

static const unsigned short sgDarkRamp[SG_SHADER_COUNT] =
{
    0, 32, 64, 96, 128, 160, 190, 255
};

typedef struct SgShaderRec
{
    Display *display;
    Colormap colormap;
    int depth;
    Pixel background;
    int dark_scheme;
    unsigned short red;
    unsigned short green;
    unsigned short blue;
    unsigned short reference;
    GC gc[SG_SHADER_COUNT];
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

static const unsigned short *shaderRamp(int dark_scheme)
{
    return dark_scheme ? sgDarkRamp : sgLightRamp;
}

static SgShaderRec *findShaderRecord(Widget widget, Pixel background)
{
    SgShaderRec *rec;
    Display *display;
    Colormap colormap = None;
    int depth = 0;
    int dark_scheme;
    XColor color;
    const unsigned short *ramp;

    if (!widget)
        return NULL;

    display = XtDisplay(widget);
    XtVaGetValues(widget,
                  XtNcolormap, &colormap,
                  XtNdepth, &depth,
                  NULL);
    dark_scheme = currentDarkScheme();

    for (rec = shader_cache; rec; rec = rec->next)
    {
        if (rec->display == display &&
            rec->colormap == colormap &&
            rec->depth == depth &&
            rec->background == background &&
            rec->dark_scheme == dark_scheme)
            return rec;
    }

    rec = (SgShaderRec *)calloc(1, sizeof(*rec));
    if (!rec)
        return NULL;

    color.pixel = background;
    XQueryColor(display, colormap, &color);
    ramp = shaderRamp(dark_scheme);

    rec->display = display;
    rec->colormap = colormap;
    rec->depth = depth;
    rec->background = background;
    rec->dark_scheme = dark_scheme;

    /* Original n32 code stores the high 8 bits in 16-bit fields. */
    rec->red = (unsigned short)(color.red >> 8);
    rec->green = (unsigned short)(color.green >> 8);
    rec->blue = (unsigned short)(color.blue >> 8);
    rec->reference = ramp[4];

    rec->next = shader_cache;
    shader_cache = rec;
    return rec;
}

static Boolean allocClosestColor(Widget widget, SgShaderRec *rec, XColor *color)
{
    int cells;
    int i;
    unsigned long best_distance = ULONG_MAX;
    Pixel best_pixel = rec->background;
    XColor *colors;

    if (XAllocColor(rec->display, rec->colormap, color))
        return True;

    /*
     * The IRIX fallback is _sgAllocClosestColor().  For indexed visuals the
     * behavior we need is nearest available RGB.  Restrict enumeration to
     * small indexed maps; TrueColor normally succeeds in XAllocColor above.
     */
    if (rec->depth <= 0 || rec->depth > 8)
        return False;

    cells = 1 << rec->depth;
    colors = (XColor *)calloc((size_t)cells, sizeof(*colors));
    if (!colors)
        return False;

    for (i = 0; i < cells; ++i)
        colors[i].pixel = (Pixel)i;
    XQueryColors(rec->display, rec->colormap, colors, cells);

    for (i = 0; i < cells; ++i)
    {
        long dr = (long)colors[i].red - (long)color->red;
        long dg = (long)colors[i].green - (long)color->green;
        long db = (long)colors[i].blue - (long)color->blue;
        unsigned long distance =
            (unsigned long)(dr * dr) +
            (unsigned long)(dg * dg) +
            (unsigned long)(db * db);

        if (distance < best_distance)
        {
            best_distance = distance;
            best_pixel = colors[i].pixel;
        }
    }

    free(colors);
    color->pixel = best_pixel;
    (void)widget;
    return True;
}

static unsigned short shadeChannel(unsigned short channel,
                                   unsigned short target,
                                   unsigned short reference,
                                   unsigned short maximum)
{
    if (target == reference)
        return channel;

    if (target < reference)
    {
        if (!reference)
            return 0;
        return (unsigned short)(((unsigned int)channel * target) / reference);
    }

    if (maximum == reference)
        return channel;

    return (unsigned short)(channel +
        (((unsigned int)(maximum - channel) * (target - reference)) /
         (maximum - reference)));
}

GC SgShaderGetGC(Widget widget, Pixel background, int shader_index)
{
    SgShaderRec *rec;
    const unsigned short *ramp;
    unsigned short target;
    unsigned short maximum;
    XColor color;
    XGCValues values;

    if (!widget)
        return NULL;

    if (shader_index < 0 || shader_index >= SG_SHADER_COUNT)
        shader_index = 4;

    rec = findShaderRecord(widget, background);
    if (!rec)
        return NULL;

    if (rec->gc[shader_index])
        return rec->gc[shader_index];

    ramp = shaderRamp(rec->dark_scheme);
    target = ramp[shader_index];
    maximum = ramp[SG_SHADER_COUNT - 1];

    if (target == rec->reference)
    {
        color.pixel = rec->background;
    }
    else
    {
        unsigned short red = shadeChannel(rec->red, target,
                                          rec->reference, maximum);
        unsigned short green = shadeChannel(rec->green, target,
                                            rec->reference, maximum);
        unsigned short blue = shadeChannel(rec->blue, target,
                                           rec->reference, maximum);

        /* Exact IRIX conversion: 8-bit shader channel << 8. */
        color.red = (unsigned short)(red << 8);
        color.green = (unsigned short)(green << 8);
        color.blue = (unsigned short)(blue << 8);
        color.flags = DoRed | DoGreen | DoBlue;
        color.pixel = rec->background;

        if (!allocClosestColor(widget, rec, &color))
            color.pixel = rec->background;
    }

    values.foreground = color.pixel;
    values.background = color.pixel;
    rec->gc[shader_index] = XtGetGC(widget,
                                    GCForeground | GCBackground,
                                    &values);
    return rec->gc[shader_index];
}
