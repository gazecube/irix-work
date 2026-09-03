#ifndef IID_SGI_ICON_H
#define IID_SGI_ICON_H

#include <X11/Xlib.h>

typedef struct SgiIconImage SgiIconImage;

SgiIconImage *sgi_icon_load(const char *path);
void sgi_icon_free(SgiIconImage *image);
void sgi_icon_draw(Display *dpy, Window win, GC gc,
                   const SgiIconImage *image,
                   int x, int y, unsigned int box_w, unsigned int box_h);

#endif
