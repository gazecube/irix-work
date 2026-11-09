#ifndef VLCMOTIF_ICONS_EXTRA_H
#define VLCMOTIF_ICONS_EXTRA_H

/*
 * The glyphs icons.h lacks, drawn the same way.  icons.h draws previous as |>
 * and next as <|, the wrong way round: skip_back and skip_forward are |< and >|.
 * One row per line, so the picture can be read.
 */
/* clang-format off */

static char *stop_icon_xpm[] = {
	"16 16 2 1",
	"  c None",
	"A c #000000",
	"                ",
	"                ",
	"                ",
	"   AAAAAAAAAA   ",
	"   AAAAAAAAAA   ",
	"   AAAAAAAAAA   ",
	"   AAAAAAAAAA   ",
	"   AAAAAAAAAA   ",
	"   AAAAAAAAAA   ",
	"   AAAAAAAAAA   ",
	"   AAAAAAAAAA   ",
	"   AAAAAAAAAA   ",
	"   AAAAAAAAAA   ",
	"                ",
	"                ",
	"                "
};

static char *rewind_icon_xpm[] = {
	"16 16 2 1",
	"  c None",
	"A c #000000",
	"                ",
	"                ",
	"      A      A  ",
	"     AA     AA  ",
	"    AAA    AAA  ",
	"   AAAA   AAAA  ",
	"  AAAAA  AAAAA  ",
	" AAAAAA AAAAAA  ",
	" AAAAAA AAAAAA  ",
	"  AAAAA  AAAAA  ",
	"   AAAA   AAAA  ",
	"    AAA    AAA  ",
	"     AA     AA  ",
	"      A      A  ",
	"                ",
	"                "
};

static char *forward_icon_xpm[] = {
	"16 16 2 1",
	"  c None",
	"A c #000000",
	"                ",
	"                ",
	" A      A       ",
	" AA     AA      ",
	" AAA    AAA     ",
	" AAAA   AAAA    ",
	" AAAAA  AAAAA   ",
	" AAAAAA AAAAAA  ",
	" AAAAAA AAAAAA  ",
	" AAAAA  AAAAA   ",
	" AAAA   AAAA    ",
	" AAA    AAA     ",
	" AA     AA      ",
	" A      A       ",
	"                ",
	"                "
};

static char *skip_back_icon_xpm[] = {
	"16 16 2 1",
	"  c None",
	"A c #000000",
	"                ",
	"                ",
	"  AAA       AA  ",
	"  AAA      AAA  ",
	"  AAA     AAAA  ",
	"  AAA    AAAAA  ",
	"  AAA   AAAAAA  ",
	"  AAA  AAAAAAA  ",
	"  AAA  AAAAAAA  ",
	"  AAA   AAAAAA  ",
	"  AAA    AAAAA  ",
	"  AAA     AAAA  ",
	"  AAA      AAA  ",
	"  AAA       AA  ",
	"                ",
	"                "
};

static char *skip_forward_icon_xpm[] = {
	"16 16 2 1",
	"  c None",
	"A c #000000",
	"                ",
	"                ",
	"  AA       AAA  ",
	"  AAA      AAA  ",
	"  AAAA     AAA  ",
	"  AAAAA    AAA  ",
	"  AAAAAA   AAA  ",
	"  AAAAAAA  AAA  ",
	"  AAAAAAA  AAA  ",
	"  AAAAAA   AAA  ",
	"  AAAAA    AAA  ",
	"  AAAA     AAA  ",
	"  AAA      AAA  ",
	"  AA       AAA  ",
	"                ",
	"                "
};

/* clang-format on */

#endif
