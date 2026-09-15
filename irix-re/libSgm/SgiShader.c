/*
 * SGI Motif shader reconstruction.
 *
 * Sources of truth:
 *   - IRIX Motif private header Xm/SgiP.h
 *   - IRIX N32 libXm.a(Sgi.o)
 *       _sgFindGC     @ 0x0f80
 *       _sgFindShader @ 0x1688
 *   - IRIX N32 libXm.a(Manager.o) _sgManagerData
 *   - IRIX N32 libXm.a(Draw.o) _RenderShadowsDefault
 *   - IRIX N32 libXm.a(Display.o), which initializes SGImode and darkScheme
 *     from Xt resources during XmDisplay initialization.
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
#define SG_MANAGER_VISUAL_STATES 5
#define SG_MANAGER_STATE_SLOTS 10

static const int sgLightRamp[SG_SHADER_LEVELS] =
{
    0, 42, 85, 128, 170, 213, 230, 255
};

static const int sgDarkRamp[SG_SHADER_LEVELS] =
{
    0, 32, 64, 96, 128, 160, 190, 255
};

static const int sgManagerStateArray[SG_MANAGER_VISUAL_STATES][SG_MANAGER_STATE_SLOTS] =
{
    { 3, 6, 2, 4, 1, 1, 5, 3, 4, 4 },
    { 2, 1, 6, 2, 5, 3, 5, 3, 4, 4 },
    { 2, 1, 6, 2, 6, 3, 6, 3, 5, 4 },
    { 0, 0, 6, 2, 6, 3, 5, 3, 4, 4 },
    { 1, 6, 2, 6, 4, 0, 6, 3, 5, 4 }
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

typedef void (*XmDrawShadowsProc)(Display *, Drawable, GC, GC,
                                  Position, Position,
                                  Dimension, Dimension,
                                  Dimension, unsigned int);

static SgShaderRec *shader_cache;

static Boolean readBooleanResource(Widget widget,
                                   const char *name,
                                   const char *class_name,
                                   Boolean default_value)
{
    XtResource resource;
    Boolean value = default_value;

    if (!widget)
        return default_value;

    resource.resource_name = (String)name;
    resource.resource_class = (String)class_name;
    resource.resource_type = XtRBoolean;
    resource.resource_size = sizeof(Boolean);
    resource.resource_offset = 0;
    resource.default_type = XtRImmediate;
    resource.default_addr = (XtPointer)(long)default_value;

    /*
     * Display.o uses XtGetSubresources() with NULL name/class and a single
     * Boolean resource record.  Doing the same here preserves wildcard
     * application resources such as "FileManager*sgiMode: True" even though
     * host OpenMotif has no SGImode global or SGI private XmDisplay fields.
     */
    XtGetSubresources(widget, (XtPointer)&value,
                      NULL, NULL,
                      &resource, 1,
                      NULL, 0);
    return value;
}

int SgSgiModeEnabled(Widget widget)
{
    static int *sgi_mode;
    static Boolean looked_up;

    if (!looked_up)
    {
        sgi_mode = (int *)dlsym(RTLD_DEFAULT, "SGImode");
        looked_up = True;
    }

    if (sgi_mode)
        return *sgi_mode != 0;

    return readBooleanResource(widget, "sgiMode", "SgiMode", False) != False;
}

static int currentDarkScheme(Widget widget)
{
    static int *dark_scheme;
    static Boolean looked_up;

    if (!looked_up)
    {
        dark_scheme = (int *)dlsym(RTLD_DEFAULT, "darkScheme");
        looked_up = True;
    }

    if (dark_scheme)
        return *dark_scheme != 0;

    return readBooleanResource(widget, "darkScheme", "DarkScheme", False) != False;
}

static const int *shaderRamp(Widget widget)
{
    return currentDarkScheme(widget) ? sgDarkRamp : sgLightRamp;
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
    ramp = shaderRamp(widget);

    rec->dpy = dpy;
    rec->cmap = cmap;
    rec->depth = depth;
    rec->pixel = background;
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

    ramp = shaderRamp(widget);
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

        color.red = (unsigned short)((unsigned short)r << 8);
        color.green = (unsigned short)((unsigned short)g << 8);
        color.blue = (unsigned short)((unsigned short)b << 8);
        color.flags = DoRed | DoGreen | DoBlue;
        color.pixel = rec->pixel;

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

    if (rec->depth == 2 &&
        shader_index != 0 && shader_index != 4 && shader_index != 7)
        return NULL;

    rec->gc_array[shader_index] = normalShaderGC(widget, rec, shader_index);
    return rec->gc_array[shader_index];
}

void SgRenderManagerShadows(Widget widget,
                            Drawable drawable,
                            Position x,
                            Position y,
                            Dimension width,
                            Dimension height,
                            int visual_state,
                            Dimension shadow_thickness,
                            unsigned int shadow_style)
{
    static XmDrawShadowsProc draw_shadows;
    static Boolean looked_up;
    const int *state;
    Pixel background = 0;
    Dimension layers;
    Dimension i;

    if (!widget || !drawable || !shadow_thickness || !width || !height)
        return;

    if (visual_state < 0 || visual_state >= SG_MANAGER_VISUAL_STATES)
        visual_state = 0;

    if (!looked_up)
    {
        draw_shadows = (XmDrawShadowsProc)dlsym(RTLD_DEFAULT, "_XmDrawShadows");
        looked_up = True;
    }
    if (!draw_shadows)
        return;

    XtVaGetValues(widget, XtNbackground, &background, NULL);
    state = sgManagerStateArray[visual_state];

    layers = shadow_thickness;
    if (layers > 3)
        layers = 3;

    for (i = 0; i < layers; ++i)
    {
        unsigned int pair = (unsigned int)i + 1U;
        GC top_gc;
        GC bottom_gc;
        Position dx;
        Position dy;
        Dimension dw;
        Dimension dh;

        if (pair >= 5U)
            break;
        if (width <= (Dimension)(2 * i) || height <= (Dimension)(2 * i))
            break;

        top_gc = SgShaderGetGC(widget, background, state[pair * 2U]);
        bottom_gc = SgShaderGetGC(widget, background, state[pair * 2U + 1U]);
        if (!top_gc || !bottom_gc)
            continue;

        dx = (Position)(x + (Position)i);
        dy = (Position)(y + (Position)i);
        dw = (Dimension)(width - 2 * i);
        dh = (Dimension)(height - 2 * i);

        draw_shadows(XtDisplay(widget), drawable,
                     top_gc, bottom_gc,
                     dx, dy, dw, dh,
                     1, shadow_style);
    }
}
