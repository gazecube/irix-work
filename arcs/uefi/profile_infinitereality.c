#include "profile_infinitereality.h"

/* Temporary bring-up asset from gazecube/sgibootscreen-src, IP35 profile.
 * The SGI standalone renderer remains unchanged above this assignment. */
extern const unsigned char infiniterealitylogo[];

void
ArcsUseInfiniteRealityLogo(struct htp_state *Htp)
{
    if (!Htp)
        return;

    Htp->clogow = 872;
    Htp->clogoh = 174;
    Htp->clogodata = (unsigned char *)(void *)infiniterealitylogo;
}
