/*
 * SgColor reconstruction.
 *
 * Source unit and public class shape are taken from the original SGI
 * Sgm/SgColor.h and libSgm.a(SgColor.o). The exported entry points keep the
 * original MIPSpro C++ mangled names so existing libSgm ABI users can resolve
 * them without depending on a modern C++ ABI.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Old object layout reconstructed from the N32 object. */
typedef struct SgColorCompat SgColorCompat;
struct SgColorCompat
{
    short red;
    short green;
    short blue;
    short pad;
    char *schemename;
    SgColorCompat *next;
    void *vptr;
};

extern unsigned char __vtbl__7SgColor[];
SgColorCompat *first__7SgColor = NULL;

void *__nw__7SgColorSGUi(unsigned int size)
{
    return malloc(size ? size : 1U);
}

void __dl__7SgColorSGPv(void *obj)
{
    if (obj)
        free(obj);
}

static float sg_absf(float value)
{
    return value < 0.0f ? -value : value;
}

void RGBtoHSV__7SgColorSGsN21RfN24(short red, short green, short blue,
                                   float *h, float *s, float *v)
{
    float r = (float)red / 255.0f;
    float g = (float)green / 255.0f;
    float b = (float)blue / 255.0f;
    float max = r;
    float min = r;
    float delta;
    float hue = 0.0f;

    if (g > max) max = g;
    if (b > max) max = b;
    if (g < min) min = g;
    if (b < min) min = b;

    delta = max - min;

    if (v)
        *v = max;

    if (max == 0.0f)
    {
        if (s) *s = 0.0f;
        if (h) *h = 0.0f;
        return;
    }

    if (s)
        *s = delta / max;

    if (delta == 0.0f)
    {
        if (h) *h = 0.0f;
        return;
    }

    if (max == r)
        hue = (g - b) / delta;
    else if (max == g)
        hue = 2.0f + (b - r) / delta;
    else
        hue = 4.0f + (r - g) / delta;

    hue *= 60.0f;
    if (hue < 0.0f)
        hue += 360.0f;

    if (h)
        *h = hue / 360.0f;
}

void HSVtoRGB__7SgColorSGfN21RsN24(float h, float s, float v,
                                   short *red, short *green, short *blue)
{
    float hue;
    float f;
    float p;
    float q;
    float t;
    float r;
    float g;
    float b;
    int sector;

    if (s == 0.0f)
    {
        short value = (short)(v * 255.0f);
        if (red) *red = value;
        if (green) *green = value;
        if (blue) *blue = value;
        return;
    }

    hue = h * 360.0f;
    while (hue >= 360.0f) hue -= 360.0f;
    while (hue < 0.0f) hue += 360.0f;

    hue /= 60.0f;
    sector = (int)hue;
    f = hue - (float)sector;
    p = v * (1.0f - s);
    q = v * (1.0f - s * f);
    t = v * (1.0f - s * (1.0f - f));

    switch (sector)
    {
        case 0: r = v; g = t; b = p; break;
        case 1: r = q; g = v; b = p; break;
        case 2: r = p; g = v; b = t; break;
        case 3: r = p; g = q; b = v; break;
        case 4: r = t; g = p; b = v; break;
        default: r = v; g = p; b = q; break;
    }

    if (red) *red = (short)(r * 255.0f);
    if (green) *green = (short)(g * 255.0f);
    if (blue) *blue = (short)(b * 255.0f);
}

SgColorCompat *__ct__7SgColorGsN21PCc(SgColorCompat *color,
                                       short red, short green, short blue,
                                       const char *schemename)
{
    if (!color)
        return NULL;

    color->red = red;
    color->green = green;
    color->blue = blue;
    color->pad = 0;
    color->schemename = schemename ? strdup(schemename) : NULL;
    color->next = first__7SgColor;
    color->vptr = (void *)__vtbl__7SgColor;
    first__7SgColor = color;
    return color;
}

void __dt__7SgColorGv(SgColorCompat *color, int delete_flag)
{
    SgColorCompat **link;

    if (!color)
        return;

    /* SgColor.o unlinks the object but does not free the duplicated scheme. */
    link = &first__7SgColor;
    while (*link)
    {
        if (*link == color)
        {
            *link = color->next;
            break;
        }
        link = &(*link)->next;
    }

    color->vptr = (void *)__vtbl__7SgColor;

    if (delete_flag & 1)
        __dl__7SgColorSGPv(color);
}

static int scheme_equal(const char *a, const char *b)
{
    if (a == b)
        return 1;
    if (!a || !b)
        return 0;
    return strcmp(a, b) == 0;
}

SgColorCompat *FindColor__7SgColorSGsN21PCc(short red, short green, short blue,
                                            const char *schemename)
{
    SgColorCompat *color;

    for (color = first__7SgColor; color; color = color->next)
    {
        if (color->red == red && color->green == green && color->blue == blue &&
            scheme_equal(color->schemename, schemename))
            return color;
    }

    color = (SgColorCompat *)__nw__7SgColorSGUi(sizeof(*color));
    if (!color)
        return NULL;

    return __ct__7SgColorGsN21PCc(color, red, green, blue, schemename);
}

SgColorCompat *FindColor__7SgColorSGfN21PCc(float h, float s, float v,
                                            const char *schemename)
{
    short red;
    short green;
    short blue;

    HSVtoRGB__7SgColorSGfN21RsN24(h, s, v, &red, &green, &blue);
    return FindColor__7SgColorSGsN21PCc(red, green, blue, schemename);
}

void getHSV__7SgColorGRfN21(SgColorCompat *color,
                            float *h, float *s, float *v)
{
    if (!color)
        return;

    RGBtoHSV__7SgColorSGsN21RfN24(color->red, color->green, color->blue,
                                  h, s, v);
}

char *getString__7SgColorGv(SgColorCompat *color)
{
    char buffer[16];

    if (!color)
        return NULL;

    sprintf(buffer, "#%02x%02x%02x",
            (unsigned int)(unsigned short)color->red & 0xffU,
            (unsigned int)(unsigned short)color->green & 0xffU,
            (unsigned int)(unsigned short)color->blue & 0xffU);
    return strdup(buffer);
}
