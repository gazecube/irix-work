#ifndef IRISGL_GL_H
#define IRISGL_GL_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Milestone-zero IRIS GL public surface.
 *
 * Declarations are limited to the original SGI flash.c dependency set.
 * Expand only as further original source/tests require it.
 */
void prefsize(long width, long height);
void prefposition(long xmin, long xmax, long ymin, long ymax);
void foreground(void);
long winopen(const char *name);
void RGBmode(void);
void gconfig(void);
void RGBcolor(short red, short green, short blue);
void clear(void);
long qtest(void);
long qread(short *value);
void reshapeviewport(void);

#ifdef __cplusplus
}
#endif

#endif /* IRISGL_GL_H */
