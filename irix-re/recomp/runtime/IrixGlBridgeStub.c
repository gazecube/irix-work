#include "IrixGuest.h"

IrixGuestFunction irix_gl_bridge_lookup(const char *name)
{
    (void)name;
    return 0;
}

void irix_gl_bridge_init(IrixGuestRuntime *rt)
{
    (void)rt;
}
