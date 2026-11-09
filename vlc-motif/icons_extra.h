#ifndef VLCMOTIF_ICONS_EXTRA_H
#define VLCMOTIF_ICONS_EXTRA_H

/* the glyphs icons.h lacks, drawn the same way */

static char *stop_icon_xpm[] = {
"16 16 2 1", "  c None", "A c #000000",
"                ", "                ", "                ", "   AAAAAAAAAA   ",
"   AAAAAAAAAA   ", "   AAAAAAAAAA   ", "   AAAAAAAAAA   ", "   AAAAAAAAAA   ",
"   AAAAAAAAAA   ", "   AAAAAAAAAA   ", "   AAAAAAAAAA   ", "   AAAAAAAAAA   ",
"   AAAAAAAAAA   ", "                ", "                ", "                "
};

static char *rewind_icon_xpm[] = {
"16 16 2 1", "  c None", "A c #000000",
"                ", "                ", "      A      A  ", "     AA     AA  ",
"    AAA    AAA  ", "   AAAA   AAAA  ", "  AAAAA  AAAAA  ", " AAAAAA AAAAAA  ",
" AAAAAA AAAAAA  ", "  AAAAA  AAAAA  ", "   AAAA   AAAA  ", "    AAA    AAA  ",
"     AA     AA  ", "      A      A  ", "                ", "                "
};

static char *forward_icon_xpm[] = {
"16 16 2 1", "  c None", "A c #000000",
"                ", "                ", " A      A       ", " AA     AA      ",
" AAA    AAA     ", " AAAA   AAAA    ", " AAAAA  AAAAA   ", " AAAAAA AAAAAA  ",
" AAAAAA AAAAAA  ", " AAAAA  AAAAA   ", " AAAA   AAAA    ", " AAA    AAA     ",
" AA     AA      ", " A      A       ", "                ", "                "
};

/* icons.h draws previous as |> and next as <|, the wrong way round: these are |< and >| */
static char *skip_back_icon_xpm[] = {
"16 16 2 1", "  c None", "A c #000000",
"                ", "                ", "  AAA       AA  ", "  AAA      AAA  ",
"  AAA     AAAA  ", "  AAA    AAAAA  ", "  AAA   AAAAAA  ", "  AAA  AAAAAAA  ",
"  AAA  AAAAAAA  ", "  AAA   AAAAAA  ", "  AAA    AAAAA  ", "  AAA     AAAA  ",
"  AAA      AAA  ", "  AAA       AA  ", "                ", "                "
};

static char *skip_forward_icon_xpm[] = {
"16 16 2 1", "  c None", "A c #000000",
"                ", "                ", "  AA       AAA  ", "  AAA      AAA  ",
"  AAAA     AAA  ", "  AAAAA    AAA  ", "  AAAAAA   AAA  ", "  AAAAAAA  AAA  ",
"  AAAAAAA  AAA  ", "  AAAAAA   AAA  ", "  AAAAA    AAA  ", "  AAAA     AAA  ",
"  AAA      AAA  ", "  AA       AAA  ", "                ", "                "
};

#endif
