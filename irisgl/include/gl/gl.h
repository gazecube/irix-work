#ifndef IRISGL_GL_H
#define IRISGL_GL_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Early IRIS GL public surface recovered for the original SGI graphics
 * stress programs.  Keep this header source-led and grow it only when an
 * original source/test requires another interface.
 */
typedef unsigned short Colorindex;
typedef unsigned short RGBvalue;

void prefsize(long width, long height);
void prefposition(long xmin, long xmax, long ymin, long ymax);
void foreground(void);
long winopen(const char *name);

void RGBmode(void);
void cmode(void);
void gconfig(void);
void RGBcolor(short red, short green, short blue);
void color(Colorindex index);
void mapcolor(Colorindex index, RGBvalue red, RGBvalue green, RGBvalue blue);
void clear(void);

long qtest(void);
long qread(short *value);
void reshapeviewport(void);

#ifdef __cplusplus
}
#endif

#endif /* IRISGL_GL_H */
