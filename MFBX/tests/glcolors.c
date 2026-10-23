/*
 * Known-colour double-buffered GLX test for the Xmfb GPU path (DMA-BUF import
 * and presentation).  Every frame fills the four quadrants of the window with
 * solid colours and swaps; odd frames use a different arrangement, so a flip
 * that shows the wrong buffer, a stale buffer or a mixed buffer is visible.
 * After the last swap the window stays up and the colours of the last frame
 * are checked with XGetImage (server-side readback) and, if --gl, with
 * glReadPixels of the front buffer.
 *
 *   DISPLAY=:29 ./glcolors [frames]        (default 8)
 * Exit status 0 when every sample matches, 1 otherwise.  Optionally leaves
 * the window up (--hold seconds) for a screenshot of the composited result.
 *
 * build: cc glcolors.c -o glcolors -lGL -lX11
 */
#include <GL/glx.h>
#include <GL/gl.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define W 256
#define H 256

static const float pal[2][4][3] = {
    { {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, 1, 0} },   /* even frames: TL TR BL BR */
    { {0, 1, 1}, {1, 0, 1}, {1, 1, 1}, {0.5f, 0.5f, 0.5f} },  /* odd frames */
};

static unsigned long pack(const float *c)
{
    return ((unsigned long) (c[0] * 255 + 0.5f) << 16) | ((unsigned long) (c[1] * 255 + 0.5f) << 8) |
           (unsigned long) (c[2] * 255 + 0.5f);
}

static void draw(int frame)
{
    int q;
    glViewport(0, 0, W, H);
    glDisable(GL_DITHER);
    glEnable(GL_SCISSOR_TEST);
    for (q = 0; q < 4; q++) {
        const float *c = pal[frame & 1][q];
        int x = (q & 1) ? W / 2 : 0, y = (q & 2) ? 0 : H / 2;   /* GL origin: bottom left */
        glScissor(x, y, W / 2, H / 2);
        glClearColor(c[0], c[1], c[2], 1);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    glDisable(GL_SCISSOR_TEST);
}

int main(int argc, char **argv)
{
    int frames = 8, hold = 0, readfront = 0, i, bad = 0;
    Display *d;
    int attr[] = { GLX_RGBA, GLX_DOUBLEBUFFER, GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8, GLX_BLUE_SIZE, 8, None };
    XVisualInfo *vi;
    XSetWindowAttributes a;
    Window w;
    GLXContext ctx;
    XImage *im;
    static const int sx[4] = { W / 4, 3 * W / 4, W / 4, 3 * W / 4 }, sy[4] = { H / 4, H / 4, 3 * H / 4, 3 * H / 4 };

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--hold") && i + 1 < argc) hold = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--gl")) readfront = 1;
        else frames = atoi(argv[i]);
    }
    d = XOpenDisplay(NULL);
    if (!d) { fprintf(stderr, "no display\n"); return 2; }
    vi = glXChooseVisual(d, DefaultScreen(d), attr);
    if (!vi) { fprintf(stderr, "no double-buffered RGBA visual\n"); return 2; }
    a.colormap = XCreateColormap(d, RootWindow(d, vi->screen), vi->visual, AllocNone);
    a.border_pixel = 0; a.background_pixel = 0; a.event_mask = ExposureMask;
    w = XCreateWindow(d, RootWindow(d, vi->screen), 40, 40, W, H, 0, vi->depth, InputOutput, vi->visual,
                      CWColormap | CWBorderPixel | CWBackPixel | CWEventMask, &a);
    XMapWindow(d, w);
    ctx = glXCreateContext(d, vi, NULL, True);
    glXMakeCurrent(d, w, ctx);
    printf("GL_RENDERER: %s\nGL_VERSION: %s\ndirect: %d\n", glGetString(GL_RENDERER), glGetString(GL_VERSION),
           glXIsDirect(d, ctx));
    for (i = 0; i < frames; i++) {
        draw(i);
        glXSwapBuffers(d, w);
    }
    glXWaitGL();
    XSync(d, False);
    usleep(300000);
    /* expected = the last frame drawn */
    im = XGetImage(d, w, 0, 0, W, H, AllPlanes, ZPixmap);
    for (i = 0; i < 4; i++) {
        unsigned long want = pack(pal[(frames - 1) & 1][i]) & 0xffffff;
        unsigned long got = im ? XGetPixel(im, sx[i], sy[i]) & 0xffffff : 0xdeadbe;
        printf("XGetImage quadrant %d: want %06lx got %06lx %s\n", i, want, got, want == got ? "ok" : "MISMATCH");
        if (want != got) bad++;
    }
    if (readfront) {
        unsigned char px[4];
        glReadBuffer(GL_FRONT);
        for (i = 0; i < 4; i++) {
            unsigned long want = pack(pal[(frames - 1) & 1][i]) & 0xffffff, got;
            glReadPixels(sx[i], H - 1 - sy[i], 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
            got = ((unsigned long) px[0] << 16) | ((unsigned long) px[1] << 8) | px[2];
            printf("glReadPixels(front) quadrant %d: want %06lx got %06lx %s\n", i, want, got, want == got ? "ok" : "MISMATCH");
            if (want != got) bad++;
        }
    }
    printf(bad ? "FAIL: %d mismatches\n" : "PASS\n", bad);
    if (hold) sleep(hold);
    return bad ? 1 : 0;
}
