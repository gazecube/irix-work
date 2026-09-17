#include "IrixX11BridgeExtra.c"
#include "IrixX11BridgeExtraAccess.h"

Display *irix_x11_primary_display_from_guest(IrixGuestAddr addr)
{
    return display_from_guest(addr);
}

GC irix_x11_primary_gc_from_guest(IrixGuestAddr addr)
{
    return gc_from_guest(addr);
}

XFontStruct *irix_x11_primary_font_from_guest(IrixGuestAddr addr)
{
    size_t i;
    for (i = 0; i < g_x11.font_count; ++i)
        if (g_x11.font_guest[i] == addr)
            return g_x11.fonts[i];
    return 0;
}

IrixGuestAddr irix_x11_primary_font_to_guest(IrixGuestRuntime *rt, XFontStruct *font)
{
    return font_to_guest(rt, font);
}
