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
