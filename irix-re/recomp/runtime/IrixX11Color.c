#include "IrixX11Color.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

/* X color names ignore case and whitespace. The installed IRIX database
   supplies SGI names that are absent from a Linux X server's database. */
static int same_color_name(const char *a, const char *b)
{
    for (;;) {
        while (isspace((unsigned char)*a)) ++a;
        while (isspace((unsigned char)*b)) ++b;
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        if (!*a) return 1;
        ++a;
        ++b;
    }
}

int irix_x11_guest_named_color(IrixGuestRuntime *rt, const char *name,
                               XColor *color)
{
    char path_buffer[4096], line[1024];
    const char *path;
    FILE *file;
    int found = 0;
    if (!rt || !rt->rootfs || !rt->rootfs[0] || !name || !name[0] ||
        name[0] == '#' || !color) return 0;
    path = irix_guest_resolve_path(rt, "/usr/lib/X11/rgb.txt", path_buffer,
                                   sizeof(path_buffer), "X11 named color", 1);
    file = path ? fopen(path, "r") : 0;
    if (!file) return 0;
    while (fgets(line, sizeof(line), file)) {
        unsigned red, green, blue;
        int offset = 0;
        if (sscanf(line, "%u %u %u %n", &red, &green, &blue, &offset) != 3 ||
            !offset || red > 255u || green > 255u || blue > 255u ||
            !line[offset] || !same_color_name(name, line + offset)) continue;
        memset(color, 0, sizeof(*color));
        color->red = (unsigned short)(red * 257u);
        color->green = (unsigned short)(green * 257u);
        color->blue = (unsigned short)(blue * 257u);
        color->flags = DoRed | DoGreen | DoBlue;
        found = 1;
        break;
    }
    fclose(file);
    return found;
}

int irix_x11_parse_color(IrixGuestRuntime *rt, Display *display, Colormap cmap,
                         const char *name, XColor *color)
{
    return XParseColor(display, cmap, name, color) ||
           irix_x11_guest_named_color(rt, name, color);
}

int irix_x11_alloc_named_color(IrixGuestRuntime *rt, Display *display,
                               Colormap cmap, const char *name,
                               XColor *screen, XColor *exact)
{
    if (XAllocNamedColor(display, cmap, name, screen, exact)) return 1;
    if (!irix_x11_guest_named_color(rt, name, exact)) return 0;
    *screen = *exact;
    return XAllocColor(display, cmap, screen);
}

int irix_x11_lookup_color(IrixGuestRuntime *rt, Display *display, Colormap cmap,
                          const char *name, XColor *exact, XColor *screen)
{
    char numeric[14];
    XColor guest_color;
    if (XLookupColor(display, cmap, name, exact, screen)) return 1;
    if (!irix_x11_guest_named_color(rt, name, &guest_color)) return 0;
    /* Ask the server for its screen approximation without allocating a cell. */
    snprintf(numeric, sizeof(numeric), "#%04x%04x%04x", guest_color.red,
             guest_color.green, guest_color.blue);
    return XLookupColor(display, cmap, numeric, exact, screen);
}
