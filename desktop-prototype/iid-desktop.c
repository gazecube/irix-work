#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/extensions/shape.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#ifndef NAME_MAX
#define NAME_MAX 255
#endif

#define MAX_OBJECTS 256
#define OBJECT_W 96
#define OBJECT_H 66

typedef enum DesktopObjectKind {
    OBJ_HOME,
    OBJ_DUMPSTER,
    OBJ_DIRECTORY,
    OBJ_EXECUTABLE,
    OBJ_FILE,
    OBJ_SYMLINK,
    OBJ_OTHER
} DesktopObjectKind;

typedef struct DesktopObject {
    DesktopObjectKind kind;
    char name[NAME_MAX + 1];
    char path[PATH_MAX];
    int x;
    int y;
    unsigned int width;
    unsigned int height;
} DesktopObject;

static DesktopObject objects[MAX_OBJECTS];
static int object_count;

static Atom intern_atom(Display *dpy, const char *name)
{
    return XInternAtom(dpy, name, False);
}

static void set_desktop_properties(Display *dpy, Window win)
{
    Atom wm_type = intern_atom(dpy, "_NET_WM_WINDOW_TYPE");
    Atom desktop_type = intern_atom(dpy, "_NET_WM_WINDOW_TYPE_DESKTOP");
    Atom utf8 = intern_atom(dpy, "UTF8_STRING");
    Atom net_name = intern_atom(dpy, "_NET_WM_NAME");
    const char *name = "Indigo Magic Desktop";

    XChangeProperty(dpy, win, wm_type, XA_ATOM, 32, PropModeReplace,
                    (unsigned char *)&desktop_type, 1);

    XStoreName(dpy, win, name);
    XChangeProperty(dpy, win, net_name, utf8, 8, PropModeReplace,
                    (const unsigned char *)name, (int)strlen(name));
}

static DesktopObject *append_object(DesktopObjectKind kind,
                                    const char *name,
                                    const char *path)
{
    DesktopObject *obj;

    if (object_count >= MAX_OBJECTS)
        return NULL;

    obj = &objects[object_count++];
    memset(obj, 0, sizeof(*obj));
    obj->kind = kind;
    obj->width = OBJECT_W;
    obj->height = OBJECT_H;

    if (name)
        snprintf(obj->name, sizeof(obj->name), "%s", name);
    if (path)
        snprintf(obj->path, sizeof(obj->path), "%s", path);

    return obj;
}

static DesktopObjectKind classify_path(const char *path, const struct stat *st)
{
    if (S_ISLNK(st->st_mode))
        return OBJ_SYMLINK;
    if (S_ISDIR(st->st_mode))
        return OBJ_DIRECTORY;
    if (S_ISREG(st->st_mode)) {
        if (access(path, X_OK) == 0)
            return OBJ_EXECUTABLE;
        return OBJ_FILE;
    }
    return OBJ_OTHER;
}

static void add_fixed_objects(void)
{
    const char *home = getenv("HOME");

    append_object(OBJ_HOME, "Home", home ? home : "");
    append_object(OBJ_DUMPSTER, "Dumpster", "");

    objects[0].x = 24;
    objects[0].y = 28;
    objects[1].x = 24;
    objects[1].y = 104;
}

static void layout_file_objects(unsigned int screen_width,
                                unsigned int screen_height)
{
    int x = (int)screen_width - 114;
    int y = 28;
    int i;

    for (i = 2; i < object_count; ++i) {
        if (y + OBJECT_H > (int)screen_height - 8) {
            y = 28;
            x -= 110;
        }

        if (x < 130)
            x = 130;

        objects[i].x = x;
        objects[i].y = y;
        y += 76;
    }
}

static int scan_desktop_directory(const char *directory,
                                  unsigned int screen_width,
                                  unsigned int screen_height)
{
    DIR *dir;
    struct dirent *ent;
    int added = 0;

    dir = opendir(directory);
    if (!dir) {
        fprintf(stderr, "iid-desktop: cannot open desktop directory '%s': %s\n",
                directory, strerror(errno));
        return 0;
    }

    while ((ent = readdir(dir)) != NULL) {
        char path[PATH_MAX];
        struct stat st;
        DesktopObjectKind kind;

        if (ent->d_name[0] == '.')
            continue;
        if (object_count >= MAX_OBJECTS)
            break;

        if (snprintf(path, sizeof(path), "%s/%s", directory, ent->d_name) >=
            (int)sizeof(path))
            continue;

        if (lstat(path, &st) != 0)
            continue;

        kind = classify_path(path, &st);
        if (append_object(kind, ent->d_name, path))
            ++added;
    }

    closedir(dir);
    layout_file_objects(screen_width, screen_height);
    return added;
}

static int desktop_object_rects(XRectangle *out, int cap)
{
    int i;
    int n = object_count;

    if (n > cap)
        n = cap;

    for (i = 0; i < n; ++i) {
        out[i].x = (short)(objects[i].x - 6);
        out[i].y = (short)(objects[i].y - 8);
        out[i].width = (unsigned short)objects[i].width;
        out[i].height = (unsigned short)objects[i].height;
    }

    return n;
}

static void rebuild_desktop_shape(Display *dpy, Window win)
{
    XRectangle rects[MAX_OBJECTS];
    int n = desktop_object_rects(rects, MAX_OBJECTS);

    /*
     * The recovered fm BackLayout uses ShapeBounding for the desktop view.
     * These prototype rectangles are a coarse renderer bound, so use Unsorted
     * here; the original renderer later supplies properly banded rectangles.
     */
    XShapeCombineRectangles(dpy, win, ShapeBounding, 0, 0,
                            rects, n, ShapeSet, Unsorted);
    XShapeCombineRectangles(dpy, win, ShapeInput, 0, 0,
                            rects, n, ShapeSet, Unsorted);
}

static void draw_document_glyph(Display *dpy, Window win, GC gc,
                                int x, int y)
{
    XDrawRectangle(dpy, win, gc, x, y, 42, 34);
    XDrawLine(dpy, win, gc, x + 8, y + 9, x + 34, y + 9);
    XDrawLine(dpy, win, gc, x + 8, y + 15, x + 34, y + 15);
    XDrawLine(dpy, win, gc, x + 8, y + 21, x + 28, y + 21);
}

static void draw_folder_glyph(Display *dpy, Window win, GC gc,
                              int x, int y)
{
    XDrawRectangle(dpy, win, gc, x, y + 7, 44, 27);
    XDrawLine(dpy, win, gc, x + 4, y + 7, x + 4, y + 2);
    XDrawLine(dpy, win, gc, x + 4, y + 2, x + 20, y + 2);
    XDrawLine(dpy, win, gc, x + 20, y + 2, x + 25, y + 7);
}

static void draw_icon(Display *dpy, Window win, GC gc,
                      const DesktopObject *obj)
{
    int screen = DefaultScreen(dpy);
    int x = obj->x;
    int y = obj->y;
    int label_len = (int)strlen(obj->name);

    XSetForeground(dpy, gc, WhitePixel(dpy, screen));
    XFillRectangle(dpy, win, gc, x, y, 46, 36);

    XSetForeground(dpy, gc, BlackPixel(dpy, screen));

    switch (obj->kind) {
    case OBJ_HOME:
    case OBJ_DIRECTORY:
        draw_folder_glyph(dpy, win, gc, x, y);
        break;
    case OBJ_DUMPSTER:
        XDrawRectangle(dpy, win, gc, x + 5, y + 5, 32, 28);
        XDrawLine(dpy, win, gc, x + 2, y + 5, x + 40, y + 5);
        XDrawLine(dpy, win, gc, x + 12, y + 1, x + 30, y + 1);
        break;
    case OBJ_EXECUTABLE:
        draw_document_glyph(dpy, win, gc, x, y);
        XFillRectangle(dpy, win, gc, x + 15, y + 12, 12, 10);
        break;
    case OBJ_SYMLINK:
        draw_document_glyph(dpy, win, gc, x, y);
        XDrawLine(dpy, win, gc, x + 5, y + 29, x + 18, y + 23);
        XDrawLine(dpy, win, gc, x + 5, y + 29, x + 13, y + 32);
        break;
    case OBJ_FILE:
    case OBJ_OTHER:
    default:
        draw_document_glyph(dpy, win, gc, x, y);
        break;
    }

    if (label_len > 15)
        label_len = 15;
    XDrawString(dpy, win, gc, x, y + 50, obj->name, label_len);
}

static void redraw(Display *dpy, Window win, GC gc)
{
    int i;

    for (i = 0; i < object_count; ++i)
        draw_icon(dpy, win, gc, &objects[i]);
}

static DesktopObject *object_at(int x, int y)
{
    int i;

    for (i = object_count - 1; i >= 0; --i) {
        int left = objects[i].x - 6;
        int top = objects[i].y - 8;
        int right = left + (int)objects[i].width;
        int bottom = top + (int)objects[i].height;

        if (x >= left && x < right && y >= top && y < bottom)
            return &objects[i];
    }

    return NULL;
}

static const char *desktop_directory_from_args(int argc, char **argv,
                                                char *fallback,
                                                size_t fallback_size)
{
    const char *env;
    const char *home;

    if (argc > 1)
        return argv[1];

    env = getenv("IID_DESKTOP_DIR");
    if (env && *env)
        return env;

    home = getenv("HOME");
    if (!home)
        return ".";

    snprintf(fallback, fallback_size, "%s/Desktop", home);
    return fallback;
}

int main(int argc, char **argv)
{
    Display *dpy;
    int screen;
    Window root;
    Window win;
    XSetWindowAttributes attrs;
    XEvent ev;
    GC gc;
    Colormap cmap;
    XColor exact, color;
    unsigned long bg;
    unsigned int width, height;
    int shape_event_base, shape_error_base;
    char default_desktop[PATH_MAX];
    const char *desktop_dir;

    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "iid-desktop: cannot open display\n");
        return 1;
    }

    if (!XShapeQueryExtension(dpy, &shape_event_base, &shape_error_base)) {
        fprintf(stderr, "iid-desktop: X Shape extension is required\n");
        XCloseDisplay(dpy);
        return 1;
    }

    screen = DefaultScreen(dpy);
    root = RootWindow(dpy, screen);
    width = (unsigned int)DisplayWidth(dpy, screen);
    height = (unsigned int)DisplayHeight(dpy, screen);

    object_count = 0;
    add_fixed_objects();
    desktop_dir = desktop_directory_from_args(argc, argv,
                                              default_desktop,
                                              sizeof(default_desktop));
    scan_desktop_directory(desktop_dir, width, height);

    fprintf(stderr, "iid-desktop: desktop directory: %s\n", desktop_dir);
    fprintf(stderr, "iid-desktop: %d desktop objects\n", object_count);

    cmap = DefaultColormap(dpy, screen);
    bg = BlackPixel(dpy, screen);
    if (XAllocNamedColor(dpy, cmap, "#4f718a", &color, &exact))
        bg = color.pixel;

    XSetWindowBackground(dpy, root, bg);
    XClearWindow(dpy, root);

    attrs.background_pixel = bg;
    attrs.event_mask = ExposureMask | ButtonPressMask | StructureNotifyMask;
    attrs.override_redirect = True;

    win = XCreateWindow(dpy, root, 0, 0, width, height, 0,
                        CopyFromParent, InputOutput, CopyFromParent,
                        CWBackPixel | CWEventMask | CWOverrideRedirect,
                        &attrs);

    set_desktop_properties(dpy, win);
    rebuild_desktop_shape(dpy, win);
    XMapWindow(dpy, win);
    XLowerWindow(dpy, win);

    gc = XCreateGC(dpy, win, 0, NULL);
    XFlush(dpy);

    for (;;) {
        XNextEvent(dpy, &ev);

        if (ev.type == Expose && ev.xexpose.count == 0) {
            redraw(dpy, win, gc);
        } else if (ev.type == ButtonPress) {
            DesktopObject *obj = object_at(ev.xbutton.x, ev.xbutton.y);
            if (obj) {
                fprintf(stderr, "iid-desktop: click '%s' path='%s' kind=%d\n",
                        obj->name, obj->path, (int)obj->kind);
            }
        }
    }

    return 0;
}
