#include "IrixGuest.h"

void irix_al_bridge_init(IrixGuestRuntime *rt) { (void)rt; }

IrixGuestFunction irix_al_bridge_lookup(const char *name)
{
    (void)name;
    return 0;
}
