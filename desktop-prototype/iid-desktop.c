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

#include "sgi_icon.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#ifndef NAME_MAX
#define NAME_MAX 255
#endif

#define MAX_OBJECTS 256
#define OBJECT_W 96
#define OBJECT_H 66
#define ICON_BOX_W 46
#define ICON_BOX_H 36

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
    char icon_path[PATH_MAX];
    int x;
    int y;
    unsigned int width;
    unsigned int height;
    SgiIconImage *icon;
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

static unsigned long named_pixel(Display *dpy, int screen,
                                 const char *name, unsigned long fallback)
{
    XColor exact, color;
    Colormap cmap = DefaultColormap(dpy, screen);

    if (name && *name && XAllocNamedColor(dpy, cmap, name, &color, &exact))
        return color.pixel;
    return fallback;
}

static DesktopObject *append_object(DesktopObjectKind kind,
                                    const char *name, const char *path)
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

static const char *kind_icon_name(DesktopObjectKind kind)
{
    switch (kind) {
    case OBJ_HOME: return "home.icon";
    case OBJ_DUMPSTER: return "dumpster.icon";
    case OBJ_DIRECTORY: return "directory.icon";
    case OBJ_EXECUTABLE: return "executable.icon";
    case OBJ_FILE: return "file.icon";
    case OBJ_SYMLINK: return "symlink.icon";
    default: return "other.icon";
    }
}

static int has_sgi_icon_suffix(const char *path)
{
    size_t n = strlen(path);
    return (n >= 5 && strcmp(path + n - 5, ".icon") == 0) ||
           (n >= 4 && strcmp(path + n - 4, ".rgb") == 0) ||
           (n >= 4 && strcmp(path + n - 4, ".sgi") == 0);
}

static void resolve_object_icon(DesktopObject *obj, const char *icon_dir)
{
    if (obj->path[0] && has_sgi_icon_suffix(obj->path)) {
        strncpy(obj->icon_path, obj->path, sizeof(obj->icon_path) - 1);
        obj->icon_path[sizeof(obj->icon_path) - 1] = '\0';
    } else if (icon_dir && *icon_dir) {
        if (snprintf(obj->icon_path, sizeof(obj->icon_path), "%s/%s",
                     icon_dir, kind_icon_name(obj->kind)) >=
            (int)sizeof(obj->icon_path))
            obj->icon_path[0] = '\0';
    }

    if (obj->icon_path[0]) {
        obj->icon = sgi_icon_load(obj->icon_path);
        if (!obj->icon)
            obj->icon_path[0] = '\0';
    }
}

static void resolve_all_icons(void)
{
    const char *icon_dir = getenv("IID_ICON_DIR");
    int i;

    for (i = 0; i < object_count; ++i)
        resolve_object_icon(&objects[i], icon_dir);
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

    XShapeCombineRectangles(dpy, win, ShapeBounding, 0, 0,
                            rects, n, ShapeSet, Unsorted);
    XShapeCombineRectangles(dpy, win, ShapeInput, 0, 0,
                            rects, n, ShapeSet, Unsorted);
}

static Window create_background_window(Display *dpy, Window root,
                                       unsigned int width, unsigned int height,
                                       unsigned long pixel)
{
    XSetWindowAttributes attrs;

    memset(&attrs, 0, sizeof(attrs));
    attrs.background_pixel = pixel;
    attrs.override_redirect = True;
    attrs.event_mask = ExposureMask | StructureNotifyMask;

    return XCreateWindow(dpy, root, 0, 0, width, height, 0,
                         CopyFromParent, InputOutput, CopyFromParent,
                         CWBackPixel | CWOverrideRedirect | CWEventMask,
                         &attrs);
}

static Window create_background_input_window(Display *dpy, Window root,
                                             unsigned int width,
                                             unsigned int height)
{
    XSetWindowAttributes attrs;
    Window win;

    memset(&attrs, 0, sizeof(attrs));
    attrs.override_redirect = True;
    attrs.event_mask = 0;

    win = XCreateWindow(dpy, root, 0, 0, width, height, 0,
                        0, InputOnly, CopyFromParent,
                        CWOverrideRedirect | CWEventMask, &attrs);

    /*
     * Real fm has a separate InputOnly window for background interaction.
     * Use passive grabs for Button1/Button2 so Button3 is not selected by
     * this window and can continue upward to the root/5Dwm menu machinery.
     */
    XGrabButton(dpy, Button1, AnyModifier, win, False,
                ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                GrabModeAsync, GrabModeAsync, None, None);
    XGrabButton(dpy, Button2, AnyModifier, win, False,
                ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                GrabModeAsync, GrabModeAsync, None, None);

    return win;
}

static void draw_document_glyph(Display *dpy, Window win, GC gc, int x, int y)
{
    XDrawRectangle(dpy, win, gc, x, y, 42, 34);
    XDrawLine(dpy, win, gc, x + 8, y + 9, x + 34, y + 9);
    XDrawLine(dpy, win, gc, x + 8, y + 15, x + 34, y + 15);
    XDrawLine(dpy, win, gc, x + 8, y + 21, x + 28, y + 21);
}

static void draw_folder_glyph(Display *dpy, Window win, GC gc, int x, int y)
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

    if (obj->icon) {
        sgi_icon_draw(dpy, win, gc, obj->icon,
                      x, y, ICON_BOX_W, ICON_BOX_H);
    } else {
        XSetForeground(dpy, gc, WhitePixel(dpy, screen));
        XFillRectangle(dpy, win, gc, x, y, ICON_BOX_W, ICON_BOX_H);
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
        default:
            draw_document_glyph(dpy, win, gc, x, y);
            break;
        }
    }

    XSetForeground(dpy, gc, BlackPixel(dpy, screen));
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
    Window background;
    Window background_input;
    Window win;
    XSetWindowAttributes attrs;
    XEvent ev;
    GC gc;
    unsigned int width, height;
    int shape_event_base, shape_error_base;
    char default_desktop[PATH_MAX];
    const char *desktop_dir;
    const char *background_name;
    unsigned long background_pixel;

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
    resolve_all_icons();

    fprintf(stderr, "iid-desktop: desktop directory: %s\n", desktop_dir);
    fprintf(stderr, "iid-desktop: %d desktop objects\n", object_count);
    if (getenv("IID_ICON_DIR"))
        fprintf(stderr, "iid-desktop: SGI icon directory: %s\n",
                getenv("IID_ICON_DIR"));

    background_name = getenv("IID_BACKGROUND");
    if (!background_name || !*background_name)
        background_name = "#4f718a";
    background_pixel = named_pixel(dpy, screen, background_name,
                                   BlackPixel(dpy, screen));

    background = create_background_window(dpy, root, width, height,
                                          background_pixel);
    background_input = create_background_input_window(dpy, root, width, height);

    memset(&attrs, 0, sizeof(attrs));
    attrs.background_pixmap = ParentRelative;
    attrs.event_mask = ExposureMask | ButtonPressMask | StructureNotifyMask;
    attrs.override_redirect = True;

    win = XCreateWindow(dpy, root, 0, 0, width, height, 0,
                        CopyFromParent, InputOutput, CopyFromParent,
                        CWBackPixmap | CWEventMask | CWOverrideRedirect,
                        &attrs);

    set_desktop_properties(dpy, win);
    rebuild_desktop_shape(dpy, win);

    XMapWindow(dpy, background);
    XMapWindow(dpy, background_input);
    XMapWindow(dpy, win);

    /* Put all IID desktop layers below normal application windows while
     * keeping their internal order: icon surface > input layer > background.
     */
    XLowerWindow(dpy, win);
    XLowerWindow(dpy, background_input);
    XLowerWindow(dpy, background);

    gc = XCreateGC(dpy, win, 0, NULL);
    XFlush(dpy);

    fprintf(stderr, "iid-desktop: background owner active (%s)\n",
            background_name);
    fprintf(stderr, "iid-desktop: background InputOnly layer active\n");

    for (;;) {
        XNextEvent(dpy, &ev);

        if (ev.type == Expose && ev.xexpose.window == win &&
            ev.xexpose.count == 0) {
            redraw(dpy, win, gc);
        } else if (ev.type == ButtonPress && ev.xbutton.window == win) {
            DesktopObject *obj = object_at(ev.xbutton.x, ev.xbutton.y);
            if (obj) {
                fprintf(stderr,
                        "iid-desktop: click '%s' path='%s' kind=%d icon='%s'\n",
                        obj->name, obj->path, (int)obj->kind, obj->icon_path);
            }
        } else if (ev.type == ButtonPress &&
                   ev.xbutton.window == background_input) {
            fprintf(stderr,
                    "iid-desktop: background button %u at %d,%d\n",
                    ev.xbutton.button, ev.xbutton.x, ev.xbutton.y);
        }
    }
}
