#include <Uefi.h>
#include <Library/BaseMemoryLib.h>

#include <style.h>
#include <stand_htport.h>

#include "../vendor/sgibootscreen-src/include/types.h"
#include "../vendor/sgibootscreen-src/include/fonts.h"
#include "font_bridge.h"

static struct fontinfo ScrInfo[256];
static struct fontinfo NcenInfo[256];
static struct fontinfo HelvInfo[256];

static void
ConvertFont(
    const font_t *Source,
    struct htp_font *Dest,
    struct fontinfo *InfoStorage
    )
{
    UINTN I;
    UINTN Count;

    Count = Source->infoLen;
    if (Count > 256)
        Count = 256;

    for (I = 0; I < Count; ++I) {
        InfoStorage[I].offset = (short)Source->info[I].offset;
        InfoStorage[I].xsize = (char)Source->info[I].w;
        InfoStorage[I].ysize = (char)Source->info[I].h;
        InfoStorage[I].xorig = (signed char)Source->info[I].xoff;
        InfoStorage[I].yorig = (signed char)Source->info[I].yoff;
        InfoStorage[I].xmove = (short)Source->info[I].width;
    }

    Dest->data = (unsigned short *)(VOID *)Source->data;
    Dest->info = InfoStorage;
    Dest->height = (short)Source->fontHeight;
    Dest->descender = (short)Source->fontDescender;
}

void
ArcsInitBringupFonts(struct htp_font Fonts[NUMFONTS])
{
    ZeroMem(Fonts, sizeof(struct htp_font) * NUMFONTS);

    ConvertFont(&ScrB18, &Fonts[ScrB18], ScrInfo);
    ConvertFont(&ncenB18, &Fonts[ncenB18], NcenInfo);
    ConvertFont(&helvR10, &Fonts[helvR10], HelvInfo);

    /* The first drawBackground() milestone does not emit the standalone
     * control glyphs that use the Special slot.  Keep that slot empty until
     * the original special.h table is imported verbatim. */
}
