#define _XOPEN_SOURCE 600
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <pty.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#define COLS 80
#define ROWS 24
#define MAX_CSI 64

typedef struct {
    unsigned char ch;
    unsigned char inverse;
} Cell;

static Display *dpy;
static Window win;
static GC gc;
static XFontStruct *font;
static int cw, ch, ascent;
static int cols = COLS, rows = ROWS;
static int cx, cy;
static int inverse;
static Cell *cells;
static int master_fd = -1;
static pid_t child = -1;
static int esc_state;
static char csi[MAX_CSI];
static int csi_len;

static Cell *cell_at(int x, int y) { return &cells[y * cols + x]; }

static void clear_row(int y, int from, int to)
{
    int x;
    if (y < 0 || y >= rows) return;
    if (from < 0) from = 0;
    if (to > cols) to = cols;
    for (x = from; x < to; ++x) {
        Cell *c = cell_at(x, y);
        c->ch = ' ';
        c->inverse = 0;
    }
}

static void clear_all(void)
{
    int y;
    for (y = 0; y < rows; ++y) clear_row(y, 0, cols);
    cx = cy = 0;
}

static void scroll_up(void)
{
    memmove(cells, cells + cols, (size_t)(rows - 1) * cols * sizeof(*cells));
    clear_row(rows - 1, 0, cols);
    if (cy > 0) --cy;
}

static void normalize_cursor(void)
{
    if (cx < 0) cx = 0;
    if (cx >= cols) cx = cols - 1;
    if (cy < 0) cy = 0;
    while (cy >= rows) scroll_up();
}

static void put_char(unsigned char v)
{
    if (v == '\r') { cx = 0; return; }
    if (v == '\n') { ++cy; normalize_cursor(); return; }
    if (v == '\b') { if (cx > 0) --cx; return; }
    if (v == '\t') { cx = (cx + 8) & ~7; if (cx >= cols) { cx = 0; ++cy; } normalize_cursor(); return; }
    if (v < 0x20 || v == 0x7f) return;
    cell_at(cx, cy)->ch = v;
    cell_at(cx, cy)->inverse = (unsigned char)inverse;
    if (++cx >= cols) { cx = 0; ++cy; normalize_cursor(); }
}

static int csi_arg(int n, int def)
{
    int i = 0, arg = 0, val = 0, have = 0;
    while (i <= csi_len) {
        int end = (i == csi_len || csi[i] == ';');
        if (!end && csi[i] >= '0' && csi[i] <= '9') {
            val = val * 10 + (csi[i] - '0');
            have = 1;
        }
        if (end) {
            if (arg == n) return have ? val : def;
            ++arg; val = 0; have = 0;
        }
        ++i;
    }
    return def;
}

static void do_csi(unsigned char final)
{
    int n = csi_arg(0, 1);
    int mode = csi_arg(0, 0);
    switch (final) {
    case 'A': cy -= n; break;
    case 'B': cy += n; break;
    case 'C': cx += n; break;
    case 'D': cx -= n; break;
    case 'H': case 'f':
        cy = csi_arg(0, 1) - 1;
        cx = csi_arg(1, 1) - 1;
        break;
    case 'J':
        if (mode == 2 || mode == 3) clear_all();
        else {
            clear_row(cy, cx, cols);
            for (n = cy + 1; n < rows; ++n) clear_row(n, 0, cols);
        }
        break;
    case 'K':
        if (mode == 1) clear_row(cy, 0, cx + 1);
        else if (mode == 2) clear_row(cy, 0, cols);
        else clear_row(cy, cx, cols);
        break;
    case 'm': {
        int i = 0;
        for (;;) {
            int a = csi_arg(i++, -1);
            if (a < 0) break;
            if (a == 0) inverse = 0;
            else if (a == 7) inverse = 1;
            else if (a == 27) inverse = 0;
        }
        break;
    }
    default: break;
    }
    normalize_cursor();
}

static void feed(const unsigned char *p, size_t n)
{
    size_t i;
    for (i = 0; i < n; ++i) {
        unsigned char v = p[i];
        if (esc_state == 0) {
            if (v == 0x1b) esc_state = 1;
            else put_char(v);
        } else if (esc_state == 1) {
            if (v == '[') { esc_state = 2; csi_len = 0; }
            else esc_state = 0;
        } else {
            if (v >= 0x40 && v <= 0x7e) {
                do_csi(v);
                esc_state = 0;
            } else if (csi_len < MAX_CSI - 1) {
                csi[csi_len++] = (char)v;
                csi[csi_len] = 0;
            }
        }
    }
}

static void redraw(void)
{
    int x, y;
    unsigned long fg = BlackPixel(dpy, DefaultScreen(dpy));
    unsigned long bg = WhitePixel(dpy, DefaultScreen(dpy));
    XSetForeground(dpy, gc, bg);
    XFillRectangle(dpy, win, gc, 0, 0, (unsigned)(cols * cw), (unsigned)(rows * ch));
    for (y = 0; y < rows; ++y) {
        for (x = 0; x < cols; ++x) {
            Cell *c = cell_at(x, y);
            char s[1];
            if (c->ch == ' ') continue;
            s[0] = (char)c->ch;
            if (c->inverse) {
                XSetForeground(dpy, gc, fg);
                XFillRectangle(dpy, win, gc, x*cw, y*ch, (unsigned)cw, (unsigned)ch);
                XSetForeground(dpy, gc, bg);
            } else XSetForeground(dpy, gc, fg);
            XDrawString(dpy, win, gc, x*cw, y*ch + ascent, s, 1);
        }
    }
    XSetForeground(dpy, gc, fg);
    XDrawRectangle(dpy, win, gc, cx*cw, cy*ch, (unsigned)(cw-1), (unsigned)(ch-1));
    XFlush(dpy);
}

static void resize_pty(void)
{
    struct winsize ws;
    memset(&ws, 0, sizeof(ws));
    ws.ws_col = (unsigned short)cols;
    ws.ws_row = (unsigned short)rows;
    ws.ws_xpixel = (unsigned short)(cols * cw);
    ws.ws_ypixel = (unsigned short)(rows * ch);
    if (master_fd >= 0) ioctl(master_fd, TIOCSWINSZ, &ws);
}

static void send_key(XKeyEvent *e)
{
    char buf[64];
    KeySym ks;
    int n = XLookupString(e, buf, sizeof(buf), &ks, 0);
    const char *seq = 0;
    switch (ks) {
    case XK_Left: seq = "\033[D"; break;
    case XK_Right: seq = "\033[C"; break;
    case XK_Up: seq = "\033[A"; break;
    case XK_Down: seq = "\033[B"; break;
    case XK_Home: seq = "\033[H"; break;
    case XK_End: seq = "\033[F"; break;
    case XK_Delete: seq = "\033[3~"; break;
    case XK_Page_Up: seq = "\033[5~"; break;
    case XK_Page_Down: seq = "\033[6~"; break;
    default: break;
    }
    if (seq) write(master_fd, seq, strlen(seq));
    else if (n > 0) write(master_fd, buf, (size_t)n);
}

static void spawn_shell(char **cmd)
{
    struct winsize ws;
    const char *shell = getenv("SHELL");
    if (!shell || !*shell) shell = "/bin/sh";
    memset(&ws, 0, sizeof(ws));
    ws.ws_col = COLS; ws.ws_row = ROWS;
    child = forkpty(&master_fd, 0, 0, &ws);
    if (child < 0) { perror("forkpty"); exit(1); }
    if (child == 0) {
        setenv("TERM", "xterm", 1);
        if (cmd && *cmd) execvp(cmd[0], cmd);
        execlp(shell, shell, (char *)0);
        perror("exec");
        _exit(127);
    }
}

int main(int argc, char **argv)
{
    XEvent ev;
    fd_set rfds;
    int xfd, maxfd;
    char **cmd = 0;
    int i;

    for (i = 1; i < argc; ++i)
        if (!strcmp(argv[i], "-e") && i + 1 < argc) { cmd = &argv[i+1]; break; }

    dpy = XOpenDisplay(0);
    if (!dpy) { fprintf(stderr, "xwsh: cannot open display\n"); return 1; }
    font = XLoadQueryFont(dpy, "fixed");
    if (!font) { fprintf(stderr, "xwsh: cannot load fixed font\n"); return 1; }
    cw = font->max_bounds.width;
    ch = font->ascent + font->descent;
    ascent = font->ascent;
    cells = calloc((size_t)cols * rows, sizeof(*cells));
    if (!cells) return 1;
    clear_all();

    win = XCreateSimpleWindow(dpy, DefaultRootWindow(dpy), 40, 40,
                              (unsigned)(cols*cw), (unsigned)(rows*ch), 1,
                              BlackPixel(dpy, DefaultScreen(dpy)),
                              WhitePixel(dpy, DefaultScreen(dpy)));
    XStoreName(dpy, win, "Winterm");
    XSelectInput(dpy, win, ExposureMask|KeyPressMask|StructureNotifyMask);
    gc = XCreateGC(dpy, win, 0, 0);
    XSetFont(dpy, gc, font->fid);
    XMapWindow(dpy, win);

    spawn_shell(cmd);
    resize_pty();
    xfd = ConnectionNumber(dpy);

    for (;;) {
        int rc;
        FD_ZERO(&rfds);
        FD_SET(xfd, &rfds);
        FD_SET(master_fd, &rfds);
        maxfd = xfd > master_fd ? xfd : master_fd;
        rc = select(maxfd + 1, &rfds, 0, 0, 0);
        if (rc < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (FD_ISSET(master_fd, &rfds)) {
            unsigned char buf[4096];
            ssize_t n = read(master_fd, buf, sizeof(buf));
            if (n <= 0) break;
            feed(buf, (size_t)n);
            redraw();
        }
        if (FD_ISSET(xfd, &rfds)) {
            while (XPending(dpy)) {
                XNextEvent(dpy, &ev);
                if (ev.type == Expose) redraw();
                else if (ev.type == KeyPress) send_key(&ev.xkey);
                else if (ev.type == ConfigureNotify) {
                    int nc = ev.xconfigure.width / cw;
                    int nr = ev.xconfigure.height / ch;
                    if (nc < 2) nc = 2; if (nr < 2) nr = 2;
                    if (nc != cols || nr != rows) {
                        Cell *old = cells;
                        int oldc = cols, oldr = rows, y, copyc;
                        cols = nc; rows = nr;
                        cells = calloc((size_t)cols * rows, sizeof(*cells));
                        if (!cells) exit(1);
                        for (y = 0; y < rows; ++y) clear_row(y, 0, cols);
                        copyc = oldc < cols ? oldc : cols;
                        for (y = 0; y < oldr && y < rows; ++y)
                            memcpy(cells + y*cols, old + y*oldc, (size_t)copyc*sizeof(*cells));
                        free(old);
                        normalize_cursor();
                        resize_pty();
                        redraw();
                    }
                }
            }
        }
        if (waitpid(child, 0, WNOHANG) == child) break;
    }
    if (child > 0) kill(child, SIGHUP);
    return 0;
}
