#define irix_x11_window_bridge_lookup irix_x11_window_bridge_lookup_base
#include "IrixX11WindowBridge.c"
#undef irix_x11_window_bridge_lookup

#include "IrixX11WindowBridgeAccess.h"

XFontSet irix_x11_window_fontset_from_guest(IrixGuestAddr addr)
{
    return fontset_from_guest(addr);
}

IrixGuestFunction irix_x11_window_bridge_lookup(const char *name)
{
    return irix_x11_window_bridge_lookup_base(name);
}
