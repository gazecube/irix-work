#ifndef IRISGL_GL_H
#define IRISGL_GL_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Early IRIS GL public surface recovered for the original SGI graphics
 * stress programs. Keep this header source-led and grow it only when an
 * original source/test requires another interface.
 */
typedef unsigned short Colorindex;
typedef unsigned short RGBvalue;

/* Canonical default colormap indices from historical SGI gl.h. */
#define BLACK   0
#define RED     1
#define GREEN   2
#define YELLOW  3
#define BLUE    4
#define MAGENTA 5
#define CYAN    6
#define WHITE   7

void prefsize(long width, long height);
void prefposition(long xmin, long xmax, long ymin, long ymax);
void foreground(void);
long winopen(const char *name);

void RGBmode(void);
void cmode(void);
void singlebuffer(void);
void doublebuffer(void);
void gconfig(void);
void gsync(void);
void RGBcolor(short red, short green, short blue);
void color(Colorindex index);
void mapcolor(Colorindex index, RGBvalue red, RGBvalue green, RGBvalue blue);
void clear(void);
void swapbuffers(void);

/* Source-documented IRIS GL polygon entry points. */
void bgnpolygon(void);
void v3f(const float v[3]);
void endpolygon(void);

long qtest(void);
long qread(short *value);
void reshapeviewport(void);

#ifdef __cplusplus
}
#endif

#endif /* IRISGL_GL_H */
