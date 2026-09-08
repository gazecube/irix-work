/*
 * Autoboot dialog subset from IRIX 6.5.7m libsc/gui/dialog.c.
 *
 * This keeps the original SGI dialog geometry, progress icon, button layout,
 * drawing code, and object lifetime for the PROM autoboot path.  Unrelated
 * popup/text-field entry points remain deferred until their standalone I/O
 * dependencies are ported.
 */

#include <Uefi.h>
#include <Library/BaseMemoryLib.h>

#include <stand_htport.h>
#include <gfxgui.h>
#include <guicore.h>
#include <style.h>

extern void free(void *);

static void dialogIntBoarder(int x1, int y1, int x2, int y2, int leftshade);
static void drawDialog(struct Dialog *d);

#define dialogfont(d) (((d)->flags & DIALOGBIGFONT) ? ncenB18 : helvR10)
#define dialogButtonY(D) ((D)->gui.y1+DIALOGBDW+DIALOGMARGIN)

static char *buttontext[] = {
    "Continue",
    "Restart",
    "Stop",
    "Cancel",
    "Accept"
};

static void
dialogEventHandler(struct gui_obj *obj, int event)
{
    struct Dialog *d = (struct Dialog *)obj;
    int i;

    switch(event) {
    case _REDRAW:
        drawDialog(d);
        break;

    case _FREE:
        if (d->textf)
            deleteObject(guiobj(d->textf));
        for (i=0; i < DIALOGMAXB; i++)
            if (d->button[i])
                deleteObject(guiobj(d->button[i]));
        free(d);
        break;
    }
}

static int
dialogButtonX(struct Dialog *d, int button)
{
    int i,x = d->gui.x2-DIALOGBDW-DIALOGMARGIN;
    struct Button *b;

    for (i=0; i <= button; i++) {
        b = d->button[i];
        x -= b ? b->gui.x2-b->gui.x1 : TEXTBUTW;
    }

    x -= button*BUTTONGAP;

    return(x);
}

static void
dialogButtonHandler(struct Button *b, __scunsigned_t value)
{
    struct Dialog *d = (struct Dialog *)value;
    int i;

    d->flags |= DIALOGCLICKED;
    for (i=0; i < DIALOGMAXB; i++)
        if (b == d->button[i]) {
            d->which = i;
            break;
        }

    return;
}

struct Button *
addDialogButton(struct Dialog *d, int bmsg)
{
    int i, move;

    for (i=0; i < DIALOGMAXB; i++) {
        if (d->button[i])
            continue;

        if ((d->flags & DIALOGBUTTONS) == 0) {
            d->flags |= DIALOGBUTTONS;
            move = DIALOGBUTMARGIN + TEXTBUTH;
            d->gui.y1 -= move>>1;
            d->gui.y2 += (move >> 1) + (move & 1);
        }

        d->button[i] = createButton(dialogButtonX(d,i),
                                     dialogButtonY(d),
                                     TEXTBUTW,TEXTBUTH);
        d->button[i]->gui.parent = (struct gui_obj *)d;
        addButtonText(d->button[i],buttontext[bmsg]);
        addButtonCallBack(d->button[i],dialogButtonHandler,(__scunsigned_t)d);
        return(d->button[i]);
    }
    return(0);
}

struct Dialog *
createDialog(char *msg, int type, int flags)
{
    struct Dialog *d;
    int basew = DIALOGBDW+DIALOGMARGIN+DIALOGINTBDW+DIALOGICONW+
        DIALOGINTBDW+DIALOGICONMARGIN+DIALOGINTBDW+DIALOGTEXTMARGIN+
        DIALOGTEXTMARGIN+DIALOGINTBDW+DIALOGMARGIN+
        DIALOGBDW-1;
    int baseh = DIALOGBDW+DIALOGMARGIN+DIALOGINTBDW+DIALOGTEXTMARGIN+
        DIALOGTEXTMARGIN+DIALOGINTBDW+
        DIALOGMARGIN+DIALOGBDW-1;
    int th,tw;

    d = (struct Dialog *)guimalloc(sizeof(struct Dialog));
    d->gui.type = GDIALOG;
    d->gui.flags = _GRAB_FOCUS;
    d->gui.eventHandler = dialogEventHandler;

    while ((*msg == '\n') && (*msg != '\0'))
        msg++;

    d->flags = flags;

    textSize(msg,&tw,&th,dialogfont(d));
    if (flags & DIALOGCENTER)
        d->textw = tw;
    else
        d->textw = 0;
    d->texth = th;
    tw += basew;
    th += baseh;

    if (tw < 350) {
        if (d->textw)
            d->textw += (350 - tw);
        tw = 350;
    }

    d->gui.x1 = (gfxWidth() - tw) >> 1;
    d->gui.y1 = ((gfxHeight() - th)/3)*2;
    d->gui.x2 = d->gui.x1 + tw;
    d->gui.y2 = d->gui.y1 + th;

    d->textf = 0;
    d->msg = msg;
    d->type = type;
    d->which = 0;

    ZeroMem(d->bmsg,sizeof(d->bmsg));
    ZeroMem(d->button,sizeof(d->button));

    addObject(guiobj(d));

    return(d);
}

static int typecolors[] = {
    lightGrayBlue,
    lightGrayCyan,
    lightGrayGreen,
    lightGrayMagenta,
    lightGrayRed
};

#define acti_w 22
#define acti_h 22
static unsigned short acti_bits[] = {
   0x01fe, 0x0000, 0x07ff, 0x8000, 0x1fff, 0xe000, 0x3e01, 0xf000,
   0x3801, 0xf000, 0x7003, 0xf800, 0x7007, 0xb800, 0xe00f, 0x1c00,
   0xe01e, 0x1c00, 0xe03c, 0x1c00, 0xe078, 0x1c00, 0xe0f0, 0x1c00,
   0xe1e0, 0x1c00, 0xe3c0, 0x1c00, 0xe780, 0x1c00, 0x7f00, 0x3800,
   0x7e00, 0x3800, 0x3c00, 0x7000, 0x3e01, 0xf000, 0x1fff, 0xe000,
   0x07ff, 0x8000, 0x01fe, 0x0000};
#define info_w 6
#define info_h 20
static unsigned short info_bits[] = {
   0xfc00, 0x7800, 0x7800, 0x7800, 0x7800, 0x7800, 0x7800, 0x7800,
   0x7800, 0x7800, 0x7800, 0x7800, 0x7800, 0xf800, 0x0000, 0x0000,
   0x0000, 0x7800, 0x7800, 0x7800};
#define prog_w 12
#define prog_h 23
static unsigned short prog_bits[] = {
   0xfff0, 0x8010, 0x9f90, 0x8f10, 0x8610, 0x8010, 0x4020, 0x4020,
   0x2040, 0x1080, 0x0900, 0x0900, 0x0900, 0x1080, 0x2640, 0x4f20,
   0x5fa0, 0x9f90, 0xbfd0, 0xbfd0, 0x8010, 0x8010, 0xfff0};
#define ques_w 11
#define ques_h 20
static unsigned short ques_bits[] = {
   0x0c00, 0x1e00, 0x1e00, 0x1e00, 0x0c00, 0x0000, 0x0000, 0x0000,
   0x0c00, 0x0c00, 0x0e00, 0x0700, 0x0780, 0x03c0, 0x61e0, 0xf0e0,
   0xf0e0, 0xf0e0, 0x79c0, 0x1f80};
#define warn_w 4
#define warn_h 20
static unsigned short warn_bits[] = {
   0x6000, 0xf000, 0xf000, 0xf000, 0x6000, 0x0000, 0x0000, 0x0000,
   0x6000, 0x6000, 0x6000, 0x6000, 0x6000, 0x6000, 0xf000, 0xf000,
   0xf000, 0xf000, 0xf000, 0x6000};

static struct {
    unsigned short *bitmap;
    short w,h;
} iconbitmaps[] = {
    { info_bits, info_w, info_h },
    { prog_bits, prog_w, prog_h },
    { ques_bits, ques_w, ques_h },
    { warn_bits, warn_w, warn_h },
    { acti_bits, acti_w, acti_h }
};

static void
drawDialog(struct Dialog *d)
{
    struct htp_font *font;
    int x1 = d->gui.x1;
    int x2 = d->gui.x2;
    int y2 = d->gui.y2;
    int i,cx,cy;

    drawCanvas(&d->gui);

    color(typecolors[d->type]);
    sboxfi(x1+DIALOGBDW+DIALOGMARGIN+DIALOGINTBDW,
           y2-DIALOGBDW-DIALOGMARGIN-DIALOGINTBDW,
           x1+DIALOGBDW+DIALOGMARGIN+DIALOGINTBDW+DIALOGICONW-1,
           y2-DIALOGBDW-DIALOGMARGIN-DIALOGINTBDW-DIALOGICONW+1);

    dialogIntBoarder(x1+DIALOGBDW+DIALOGMARGIN,
        y2-DIALOGBDW-DIALOGMARGIN-2*DIALOGINTBDW-DIALOGICONW+1,
        x1+DIALOGBDW+DIALOGMARGIN+2*DIALOGINTBDW+DIALOGICONW-1,
        y2-DIALOGBDW-DIALOGMARGIN,typecolors[d->type]+1);

    cx = x1+DIALOGBDW+DIALOGMARGIN+DIALOGINTBDW+
        ((DIALOGICONW-iconbitmaps[d->type].w)>>1);
    cy = y2-DIALOGBDW-DIALOGMARGIN-DIALOGINTBDW-DIALOGICONW+
        ((DIALOGICONW-iconbitmaps[d->type].h)>>1)+1;

    color(BLACK);
    cmov2i(cx,cy);
    drawBitmap(iconbitmaps[d->type].bitmap,
               iconbitmaps[d->type].w,
               iconbitmaps[d->type].h);

    if (d->textf)
        drawObject(guiobj(d->textf));

    for (i=0; i < DIALOGMAXB ; i++)
        if (d->button[i])
            drawObject(guiobj(d->button[i]));

    font = &gfxgui.htp->fonts[dialogfont(d)];
    cx = x1+DIALOGTEXTBDX+DIALOGINTBDW+DIALOGTEXTMARGIN;
    cy = y2-DIALOGBDW-DIALOGMARGIN-DIALOGINTBDW-DIALOGTEXTMARGIN+1;

    dialogIntBoarder(x1+DIALOGTEXTBDX,
                     cy-DIALOGTEXTMARGIN-DIALOGINTBDW-d->texth,
                     x2-DIALOGBDW-DIALOGMARGIN,
                     y2-DIALOGBDW-DIALOGMARGIN,
                     0);

    color(BLACK);
    cy -= font->height - font->descender;
    putctext(d->msg,cx,cy,d->textw,dialogfont(d));
}

static void
dialogIntBoarder(int x1, int y1, int x2, int y2, int leftshade)
{
    int i,ramp[DIALOGINTBDW];

    ramp[0] = White; ramp[1] = DarkGray; ramp[2] = LightGray;
    for (i = 0; i < DIALOGINTBDW; i++) {
        color(ramp[i]);
        sboxfi(x1+i, y1+i, x2-i, y1+i);
    }
    for (i = 0; i < DIALOGINTBDW; i++) {
        color(ramp[i]);
        sboxfi(x2-i, y1+i, x2-i, y2-i);
    }

    ramp[0] = LightGray; ramp[1] = DarkGray;
    ramp[2] = leftshade ? leftshade : White;
    x2--;
    for (i = 0; i < DIALOGINTBDW; i++) {
        color(ramp[i]);
        sboxfi(x1+i, y2-i, x2-i, y2-i);
    }
    for (i = 0; i < DIALOGINTBDW; i++) {
        color(ramp[i]);
        sboxfi(x1+i, y1+i, x1+i, y2-i);
    }
}
