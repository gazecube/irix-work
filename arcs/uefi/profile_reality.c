#include "profile_reality.h"

/* Temporary bring-up asset from gazecube/sgibootscreen-src, IP19 Reality profile.
 * The SGI standalone renderer remains unchanged above this assignment. */
extern const unsigned char realitylogo[];

void
ArcsUseRealityLogo(struct htp_state *Htp)
{
    if (!Htp)
        return;

    Htp->clogow = 512;
    Htp->clogoh = 256;
    Htp->clogodata = (unsigned char *)(void *)realitylogo;
}
