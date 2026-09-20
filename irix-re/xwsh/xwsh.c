#define _XOPEN_SOURCE 600
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
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
#define RETAIN_LIMIT 1000

typedef struct {
    unsigned char ch;
    unsigned char inverse;
    unsigned char bold;
} Cell;

static Display *dpy;
static Window win;
static GC gc;
static XFontStruct *font;
static int cw, ch, ascent;
static int cols = COLS, rows = ROWS;
static int cx, cy;
static int inverse;
static int bold;
static int app_cursor, app_keypad;
static int saved_cx, saved_cy;
static int scroll_top, scroll_bottom;
static unsigned long term_fg, term_bg, bold_pixel, cursor_fg, cursor_bg, select_fg, select_bg;
static Atom wm_delete, xa_clipboard, xa_utf8;
static int selecting, sel_x0, sel_y0, sel_x1, sel_y1;
static char *selection_text;
static const char *opt_title="Winterm", *opt_font="fixed";
static const char *opt_fg="#d6d6d6", *opt_bg="#00003f", *opt_bold="#ffff00";
static const char *opt_selfg="#000000", *opt_selbg="#adadad";
static const char *filter_cmd1="nr -newwin '%s'";
static int hold_on_exit;
static Cell *cells;
static Cell *history;
static int history_count, view_offset;
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
        c->bold = 0;
    }
}

static void clear_all(void)
{
    int y;
    for (y = 0; y < rows; ++y) clear_row(y, 0, cols);
    cx = cy = 0;
    scroll_top = 0; scroll_bottom = rows - 1;
}

static void save_history_row(void)
{
    if (!history) return;
    if (history_count < RETAIN_LIMIT) ++history_count;
    else memmove(history, history + cols, (size_t)(RETAIN_LIMIT-1)*cols*sizeof(*history));
    memcpy(history + (history_count-1)*cols, cells, (size_t)cols*sizeof(*history));
}

static void scroll_region_up(int top, int bottom)
{
    if (top==0 && bottom==rows-1) save_history_row();
    if (top < 0) top=0; if (bottom >= rows) bottom=rows-1; if (top >= bottom) return;
    memmove(cells + top*cols, cells + (top+1)*cols, (size_t)(bottom-top)*cols*sizeof(*cells));
    clear_row(bottom,0,cols);
}

static void scroll_region_down(int top, int bottom)
{
    if (top < 0) top=0; if (bottom >= rows) bottom=rows-1; if (top >= bottom) return;
    memmove(cells + (top+1)*cols, cells + top*cols, (size_t)(bottom-top)*cols*sizeof(*cells));
    clear_row(top,0,cols);
}

static void scroll_up(void)
{
    scroll_region_up(scroll_top, scroll_bottom);
    if (cy > scroll_top) --cy;
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
    if (v == '\n') { if (cy == scroll_bottom) scroll_region_up(scroll_top,scroll_bottom); else ++cy; normalize_cursor(); return; }
    if (v == '\b') { if (cx > 0) --cx; return; }
    if (v == '\t') { cx = (cx + 8) & ~7; if (cx >= cols) { cx = 0; ++cy; } normalize_cursor(); return; }
    if (v < 0x20 || v == 0x7f) return;
    cell_at(cx, cy)->ch = v;
    cell_at(cx, cy)->inverse = (unsigned char)inverse;
    cell_at(cx, cy)->bold = (unsigned char)bold;
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
    if (csi[0]=='?' && (final=='h' || final=='l')) {
        int on=(final=='h'), m=csi_arg(0,0);
        /* csi_arg sees '?' as a non-digit, so parse the DEC private mode here. */
        m=atoi(csi+1);
        if (m==1) app_cursor=on;
        return;
    }
    switch (final) {
    case 'A': cy -= n; break;
    case '@': { int x; if(n>cols-cx)n=cols-cx; for(x=cols-1;x>=cx+n;--x)*cell_at(x,cy)=*cell_at(x-n,cy); for(x=cx;x<cx+n;++x){cell_at(x,cy)->ch=' ';cell_at(x,cy)->inverse=cell_at(x,cy)->bold=0;} break; }
    case 'P': { int x; if(n>cols-cx)n=cols-cx; for(x=cx;x<cols-n;++x)*cell_at(x,cy)=*cell_at(x+n,cy); clear_row(cy,cols-n,cols); break; }
    case 'L': while(n-->0) scroll_region_down(cy,scroll_bottom); break;
    case 'M': while(n-->0) scroll_region_up(cy,scroll_bottom); break;
    case 'B': cy += n; break;
    case 'C': cx += n; break;
    case 'D': cx -= n; break;
    case 's': saved_cx=cx; saved_cy=cy; break;
    case 'u': cx=saved_cx; cy=saved_cy; break;
    case 'r': scroll_top=csi_arg(0,1)-1; scroll_bottom=csi_arg(1,rows)-1; if(scroll_top<0)scroll_top=0; if(scroll_bottom>=rows)scroll_bottom=rows-1; cx=0;cy=scroll_top; break;
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
            if (a == 0) { inverse = 0; bold = 0; }
            else if (a == 1) bold = 1;
            else if (a == 7) inverse = 1;
            else if (a == 22) bold = 0;
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
            else if (v == '=') { app_keypad=1; esc_state=0; }
            else if (v == '>') { app_keypad=0; esc_state=0; }
            else if (v == '7') { saved_cx=cx; saved_cy=cy; esc_state=0; }
            else if (v == '8') { cx=saved_cx; cy=saved_cy; normalize_cursor(); esc_state=0; }
            else if (v == 'D') { if(cy==scroll_bottom)scroll_region_up(scroll_top,scroll_bottom);else ++cy; esc_state=0; }
            else if (v == 'M') { if(cy==scroll_top)scroll_region_down(scroll_top,scroll_bottom);else --cy; esc_state=0; }
            else if (v == 'c') { inverse=bold=app_cursor=app_keypad=0; clear_all(); esc_state=0; }
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

static Cell *display_cell(int x, int y)
{
    int line = history_count + y - view_offset;
    if (view_offset && line < history_count) return &history[line*cols+x];
    line -= history_count;
    if (line < 0) line=0; if (line >= rows) line=rows-1;
    return cell_at(x,line);
}

static int point_selected(int x,int y)
{
    int x0=sel_x0,y0=sel_y0,x1=sel_x1,y1=sel_y1;
    if (!selecting && !selection_text) return 0;
    if(y0>y1 || (y0==y1 && x0>x1)){int t=x0;x0=x1;x1=t;t=y0;y0=y1;y1=t;}
    return y>=y0 && y<=y1 && (y!=y0 || x>=x0) && (y!=y1 || x<=x1);
}

static void redraw(void)
{
    int x, y;
    unsigned long fg = term_fg;
    unsigned long bg = term_bg;
    XSetForeground(dpy, gc, bg);
    XFillRectangle(dpy, win, gc, 0, 0, (unsigned)(cols * cw), (unsigned)(rows * ch));
    for (y = 0; y < rows; ++y) {
        for (x = 0; x < cols; ++x) {
            Cell *c = display_cell(x, y);
            char s[1];
            s[0] = (char)c->ch;
            if (point_selected(x,y)) {
                XSetForeground(dpy,gc,select_bg); XFillRectangle(dpy,win,gc,x*cw,y*ch,(unsigned)cw,(unsigned)ch);
                if(c->ch!=' '){XSetForeground(dpy,gc,select_fg);XDrawString(dpy,win,gc,x*cw,y*ch+ascent,s,1);} continue;
            }
            if (c->ch == ' ') continue;
            if (c->inverse) {
                XSetForeground(dpy, gc, fg);
                XFillRectangle(dpy, win, gc, x*cw, y*ch, (unsigned)cw, (unsigned)ch);
                XSetForeground(dpy, gc, bg);
            } else XSetForeground(dpy, gc, c->bold ? bold_pixel : fg);
            XDrawString(dpy, win, gc, x*cw, y*ch + ascent, s, 1);
            if (c->bold) XDrawString(dpy, win, gc, x*cw + 1, y*ch + ascent, s, 1);
        }
    }
    if (view_offset) { XFlush(dpy); return; }
    XSetForeground(dpy, gc, cursor_bg);
    XFillRectangle(dpy, win, gc, cx*cw, cy*ch, (unsigned)cw, (unsigned)ch);
    { Cell *cc=cell_at(cx,cy); char s[1]={(char)cc->ch}; if(cc->ch!=' '){ XSetForeground(dpy,gc,cursor_fg); XDrawString(dpy,win,gc,cx*cw,cy*ch+ascent,s,1); } }
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

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

static void make_selection(void)
{
    int x0=sel_x0, y0=sel_y0, x1=sel_x1, y1=sel_y1, y;
    size_t cap=(size_t)(rows*(cols+1)+1), n=0;
    free(selection_text); selection_text=malloc(cap);
    if (!selection_text) return;
    if (y0>y1 || (y0==y1 && x0>x1)) { int t=x0;x0=x1;x1=t; t=y0;y0=y1;y1=t; }
    for (y=y0;y<=y1;++y) {
        int a=(y==y0?x0:0), b=(y==y1?x1:cols-1), x, end=b;
        while (end>=a && cell_at(end,y)->ch==' ') --end;
        for (x=a;x<=end && n+2<cap;++x) selection_text[n++]=(char)cell_at(x,y)->ch;
        if (y!=y1 && n+2<cap) selection_text[n++]='\n';
    }
    selection_text[n]=0;
    XSetSelectionOwner(dpy, XA_PRIMARY, win, CurrentTime);
    XSetSelectionOwner(dpy, xa_clipboard, win, CurrentTime);
}

static int urlchar(int c)
{
    return c>0x20 && c!='"' && c!='\'' && c!='<' && c!='>' && c!='(' && c!=')';
}

static void filter_url_at(int x,int y)
{
    Cell *row; int a=x,b=x; char url[2048],cmd[4096]; size_t n; pid_t p;
    if(y<0||y>=rows||x<0||x>=cols)return;
    row=display_cell(0,y);
    while(a>0 && urlchar(row[a-1].ch))--a;
    while(b<cols && urlchar(row[b].ch))++b;
    n=(size_t)(b-a); if(!n||n>=sizeof(url))return;
    memcpy(url,&row[a].ch,n); url[n]=0;
    if(strncmp(url,"http://",7)&&strncmp(url,"https://",8)&&strncmp(url,"ftp://",6)&&strncmp(url,"www.",4))return;
    free(selection_text); selection_text=strdup(url);
    if(!selection_text)return;
    XSetSelectionOwner(dpy,XA_PRIMARY,win,CurrentTime);
    if(snprintf(cmd,sizeof(cmd),filter_cmd1,url)>=(int)sizeof(cmd))return;
    p=fork(); if(p==0){execl("/bin/sh","sh","-c",cmd,(char*)0);_exit(127);}
}

static void request_paste(Atom which)
{
    XConvertSelection(dpy, which, xa_utf8, xa_utf8, win, CurrentTime);
}

static void selection_request(XSelectionRequestEvent *r)
{
    XSelectionEvent e;
    memset(&e,0,sizeof(e)); e.type=SelectionNotify; e.display=r->display;
    e.requestor=r->requestor; e.selection=r->selection; e.target=r->target;
    e.time=r->time; e.property=None;
    if (selection_text && (r->target==xa_utf8 || r->target==XA_STRING)) {
        Atom prop=r->property ? r->property : r->target;
        XChangeProperty(dpy,r->requestor,prop,r->target,8,PropModeReplace,
                        (unsigned char*)selection_text,(int)strlen(selection_text));
        e.property=prop;
    }
    XSendEvent(dpy,r->requestor,False,0,(XEvent*)&e);
}

static void selection_notify(XSelectionEvent *e)
{
    Atom type; int format; unsigned long n,left; unsigned char *p=0;
    if (e->property==None) return;
    if (XGetWindowProperty(dpy,win,e->property,0,1<<20,True,AnyPropertyType,
                           &type,&format,&n,&left,&p)==Success && p) {
        if (format==8 && n) write(master_fd,p,n);
        XFree(p);
    }
}

static void set_view(int off)
{
    if(off<0)off=0; if(off>history_count)off=history_count; view_offset=off; redraw();
}

static void send_key(XKeyEvent *e)
{
    char buf[64];
    KeySym ks;
    int n = XLookupString(e, buf, sizeof(buf), &ks, 0);
    const char *seq = 0;
    if ((e->state & ControlMask) && (e->state & ShiftMask) && (ks==XK_V || ks==XK_v)) { request_paste(xa_clipboard); return; }
    if ((e->state & ShiftMask) && ks==XK_Insert) { request_paste(XA_PRIMARY); return; }
    /* XWsh keyMapping1: these are local wsh functions, not escape sequences. */
    if (ks==XK_Home) { set_view(history_count); return; }
    if (ks==XK_End) { set_view(0); return; }
    if (ks==XK_Prior) { set_view(view_offset + rows); return; }
    if (ks==XK_Next) { set_view(view_offset - rows); return; }
    if (ks==XK_F4) { if(n>0)write(master_fd,buf,(size_t)n); return; }
    switch (ks) {
    case XK_Left: seq = app_cursor ? "\033OD" : "\033[D"; break;
    case XK_Right: seq = app_cursor ? "\033OC" : "\033[C"; break;
    case XK_Up: seq = app_cursor ? "\033OA" : "\033[A"; break;
    case XK_Down: seq = app_cursor ? "\033OB" : "\033[B"; break;
    case XK_KP_Insert: if(app_keypad)seq="\033Op"; break;
    case XK_KP_End: if(app_keypad)seq="\033Oq"; break;
    case XK_KP_Down: if(app_keypad)seq="\033Or"; break;
    case XK_KP_Next: if(app_keypad)seq="\033Os"; break;
    case XK_KP_Left: if(app_keypad)seq="\033Ot"; break;
    case XK_KP_Begin: if(app_keypad)seq="\033Ou"; break;
    case XK_KP_Right: if(app_keypad)seq="\033Ov"; break;
    case XK_KP_Home: if(app_keypad)seq="\033Ow"; break;
    case XK_KP_Up: if(app_keypad)seq="\033Ox"; break;
    case XK_KP_Prior: if(app_keypad)seq="\033Oy"; break;
    case XK_KP_Subtract: if(app_keypad)seq="\033Om"; break;
    case XK_KP_Separator: case XK_KP_Add: if(app_keypad)seq="\033Ol"; break;
    case XK_KP_Delete: if(app_keypad)seq="\033On"; break;
    case XK_KP_Enter: if(app_keypad)seq="\033OM"; break;
    case XK_F9: if(app_keypad)seq = "\033OP"; break;
    case XK_F10: if(app_keypad)seq = "\033OQ"; break;
    case XK_F11: if(app_keypad)seq = "\033OR"; break;
    case XK_F12: if(app_keypad)seq = "\033OS"; break;
    case XK_Delete: seq = "\033[3~"; break;
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
    int i, gx=40, gy=40, gw=0, gh=0;
    const char *geom=0;

    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-e") && i + 1 < argc) { cmd = &argv[i+1]; break; }
        else if ((!strcmp(argv[i],"-title") || !strcmp(argv[i],"-t")) && i+1<argc) opt_title=argv[++i];
        else if ((!strcmp(argv[i],"-fn") || !strcmp(argv[i],"-f")) && i+1<argc) opt_font=argv[++i];
        else if (!strcmp(argv[i],"-fg") && i+1<argc) opt_fg=argv[++i];
        else if (!strcmp(argv[i],"-bg") && i+1<argc) opt_bg=argv[++i];
        else if (!strcmp(argv[i],"-bold") && i+1<argc) opt_bold=argv[++i];
        else if (!strcmp(argv[i],"-selfg") && i+1<argc) opt_selfg=argv[++i];
        else if (!strcmp(argv[i],"-selbg") && i+1<argc) opt_selbg=argv[++i];
        else if ((!strcmp(argv[i],"-geometry") || !strcmp(argv[i],"-geom")) && i+1<argc) geom=argv[++i];
        else if ((!strcmp(argv[i],"-sl") || !strcmp(argv[i],"-retain")) && i+1<argc) { ++i; /* fixed backing store remains 1000 for now */ }
        else if (!strcmp(argv[i],"-hold") || !strcmp(argv[i],"-holdonerror")) hold_on_exit=1;
    }

    dpy = XOpenDisplay(0);
    if (!dpy) { fprintf(stderr, "xwsh: cannot open display\n"); return 1; }
    { Colormap cm=DefaultColormap(dpy,DefaultScreen(dpy)); XColor a,b;
      term_fg=WhitePixel(dpy,DefaultScreen(dpy)); term_bg=BlackPixel(dpy,DefaultScreen(dpy));
      bold_pixel=term_fg; cursor_fg=term_bg; cursor_bg=term_fg; select_fg=term_bg; select_bg=term_fg;
      if(XAllocNamedColor(dpy,cm,opt_fg,&a,&b))term_fg=a.pixel;
      if(XAllocNamedColor(dpy,cm,opt_bg,&a,&b))term_bg=a.pixel;
      if(XAllocNamedColor(dpy,cm,opt_bold,&a,&b))bold_pixel=a.pixel;
      if(XAllocNamedColor(dpy,cm,"#00003f",&a,&b))cursor_fg=a.pixel;
      if(XAllocNamedColor(dpy,cm,"#00ff00",&a,&b))cursor_bg=a.pixel;
      if(XAllocNamedColor(dpy,cm,opt_selfg,&a,&b))select_fg=a.pixel;
      if(XAllocNamedColor(dpy,cm,opt_selbg,&a,&b))select_bg=a.pixel;
    }
    font = XLoadQueryFont(dpy, opt_font);
    if (!font) { fprintf(stderr, "xwsh: cannot load fixed font\n"); return 1; }
    cw = font->max_bounds.width;
    ch = font->ascent + font->descent;
    ascent = font->ascent;
    if (geom) { int mask=XParseGeometry(geom,&gx,&gy,(unsigned*)&gw,(unsigned*)&gh); if(mask&WidthValue)cols=gw; if(mask&HeightValue)rows=gh; }
    cells = calloc((size_t)cols * rows, sizeof(*cells));
    history = calloc((size_t)RETAIN_LIMIT * cols, sizeof(*history));
    if (!cells || !history) return 1;
    clear_all();

    win = XCreateSimpleWindow(dpy, DefaultRootWindow(dpy), gx, gy,
                              (unsigned)(cols*cw), (unsigned)(rows*ch), 1,
                              term_fg, term_bg);
    XStoreName(dpy, win, opt_title);
    XSetIconName(dpy, win, opt_title);
    { XSizeHints h; memset(&h,0,sizeof(h)); h.flags=PResizeInc|PBaseSize; h.width_inc=cw; h.height_inc=ch; h.base_width=0; h.base_height=0; XSetWMNormalHints(dpy,win,&h); }
    wm_delete=XInternAtom(dpy,"WM_DELETE_WINDOW",False); XSetWMProtocols(dpy,win,&wm_delete,1);
    xa_clipboard=XInternAtom(dpy,"CLIPBOARD",False); xa_utf8=XInternAtom(dpy,"UTF8_STRING",False);
    XSelectInput(dpy, win, ExposureMask|KeyPressMask|StructureNotifyMask|ButtonPressMask|ButtonReleaseMask|PointerMotionMask);
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
            view_offset=0;
            redraw();
        }
        if (FD_ISSET(xfd, &rfds)) {
            while (XPending(dpy)) {
                XNextEvent(dpy, &ev);
                if (ev.type == Expose) redraw();
                else if (ev.type == KeyPress) send_key(&ev.xkey);
                else if (ev.type == ButtonPress && ev.xbutton.button == Button1 && (ev.xbutton.state & ControlMask)) {
                    filter_url_at(clampi(ev.xbutton.x/cw,0,cols-1),clampi(ev.xbutton.y/ch,0,rows-1));
                } else if (ev.type == ButtonPress && ev.xbutton.button == Button1) {
                    selecting=1; free(selection_text); selection_text=0; sel_x0=sel_x1=clampi(ev.xbutton.x/cw,0,cols-1); sel_y0=sel_y1=clampi(ev.xbutton.y/ch,0,rows-1); redraw();
                } else if (ev.type == MotionNotify && selecting) {
                    sel_x1=clampi(ev.xmotion.x/cw,0,cols-1); sel_y1=clampi(ev.xmotion.y/ch,0,rows-1); redraw();
                } else if (ev.type == ButtonRelease && ev.xbutton.button == Button1) {
                    sel_x1=clampi(ev.xbutton.x/cw,0,cols-1); sel_y1=clampi(ev.xbutton.y/ch,0,rows-1); make_selection(); selecting=0; redraw();
                } else if (ev.type == ButtonPress && ev.xbutton.button == Button2) request_paste(XA_PRIMARY);
                else if (ev.type == SelectionRequest) selection_request(&ev.xselectionrequest);
                else if (ev.type == SelectionNotify) selection_notify(&ev.xselection);
                else if (ev.type == ClientMessage && (Atom)ev.xclient.data.l[0] == wm_delete) goto done;
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
                        free(history); history=calloc((size_t)RETAIN_LIMIT*cols,sizeof(*history)); history_count=view_offset=0;
                        scroll_top=0; scroll_bottom=rows-1;
                        normalize_cursor();
                        resize_pty();
                        redraw();
                    }
                }
            }
        }
        if (waitpid(child, 0, WNOHANG) == child) { if(hold_on_exit){child=-1;} else break; }
    }
done:
    if (child > 0) kill(child, SIGHUP);
    free(selection_text);
    free(history);
    return 0;
}
