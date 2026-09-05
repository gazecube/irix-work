#include <stand_htport.h>
#include <style.h>

#include <fonts/scrB18.h>
#include <fonts/special.h>
#include <fonts/ncenB18.h>
#include <fonts/helvR10.h>

#include "original_fonts.h"

void
ArcsInitOriginalFonts(struct htp_font Fonts[NUMFONTS])
{
    Fonts[ScrB18].data = ScrB18Data;
    Fonts[ScrB18].info = ScrB18Info;
    Fonts[ScrB18].height = ScrB18_ht;
    Fonts[ScrB18].descender = ScrB18_ds;

    Fonts[Special].data = tpsdata;
    Fonts[Special].info = tpspecial;
    Fonts[Special].height = special_ht;
    Fonts[Special].descender = special_ds;

    Fonts[ncenB18].data = ncenB18Data;
    Fonts[ncenB18].info = ncenB18Info;
    Fonts[ncenB18].height = ncenB18_ht;
    Fonts[ncenB18].descender = ncenB18_ds;

    Fonts[helvR10].data = helvR10Data;
    Fonts[helvR10].info = helvR10Info;
    Fonts[helvR10].height = helvR10_ht;
    Fonts[helvR10].descender = helvR10_ds;
}
