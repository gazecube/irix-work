#include "ImdIconPanel.h"

#include <X11/StringDefs.h>
#include <X11/Xlib.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace
{
struct FtiPoint { double x; double y; };
enum DrawMode { kNone, kPolygon, kLine, kOutlinePolygon };

static std::string trim(const std::string &s)
{
    const std::string::size_type a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return std::string();
    const std::string::size_type b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static bool starts(const std::string &s, const char *p)
{
    return s.compare(0, std::strlen(p), p) == 0;
}

static bool hasGenericFolder(const std::string &root)
{
    std::ifstream in((root + "/iconlib/generic.folder.closed.fti").c_str());
    return in.good();
}

static std::string resolveFiletypeRoot()
{
    const char *env = std::getenv("SGI_FILETYPE_DIR");
    if (env && *env && hasGenericFolder(env)) return env;

    const char *irixRoot = std::getenv("IRIX_ROOT");
    if (irixRoot && *irixRoot) {
        const std::string root = std::string(irixRoot) + "/usr/lib/filetype";
        if (hasGenericFolder(root)) return root;
    }

    static const char *const roots[] = {
        "/usr/lib/filetype",
        "/usr/share/sgi/filetype",
        "/opt/MaXX/usr/lib/filetype",
        "/opt/MaXX/share/filetype",
        0
    };
    for (int i = 0; roots[i]; ++i)
        if (hasGenericFolder(roots[i])) return roots[i];

    return std::string();
}

static unsigned long allocNamed(Widget w, const char *name, unsigned long fallback)
{
    XColor exact, screen;
    Display *dpy = XtDisplay(w);
    Colormap cmap = DefaultColormapOfScreen(XtScreen(w));
    if (XAllocNamedColor(dpy, cmap, name, &screen, &exact)) return screen.pixel;
    return fallback;
}

static unsigned long colorFor(Widget w, const std::string &name)
{
    Pixel bg = 0;
    XtVaGetValues(w, XtNbackground, &bg, NULL);
    if (name == "outlinecolor") return allocNamed(w, "black", BlackPixelOfScreen(XtScreen(w)));
    if (name == "shadowcolor") return allocNamed(w, "gray45", BlackPixelOfScreen(XtScreen(w)));
    if (name == "iconcolor") return allocNamed(w, "gray80", WhitePixelOfScreen(XtScreen(w)));
    return bg;
}

static void drawPrimitive(Widget w, GC gc, Window win, DrawMode mode,
                          const std::vector<FtiPoint> &points,
                          long x, long y, unsigned long pixel)
{
    if (points.empty()) return;
    std::vector<XPoint> xp(points.size());
    const double scale = 0.54;
    for (std::size_t i = 0; i < points.size(); ++i) {
        xp[i].x = static_cast<short>(x + points[i].x * scale);
        xp[i].y = static_cast<short>(y + (100.0 - points[i].y) * scale);
    }
    XSetForeground(XtDisplay(w), gc, pixel);
    if (mode == kPolygon || mode == kOutlinePolygon)
        XFillPolygon(XtDisplay(w), win, &xp[0], static_cast<int>(xp.size()), Complex, CoordModeOrigin);
    if (mode == kLine || mode == kOutlinePolygon)
        XDrawLines(XtDisplay(w), win, &xp[0], static_cast<int>(xp.size()), CoordModeOrigin);
}

static bool drawFti(Widget w, const std::string &relative, long x, long y)
{
    static bool reportedMissingRoot = false;
    static bool reportedRoot = false;
    const std::string root = resolveFiletypeRoot();
    if (root.empty()) {
        if (!reportedMissingRoot) {
            std::fprintf(stderr,
                         "ImdInvertedTeeRenderer: no SGI filetype tree found; set SGI_FILETYPE_DIR or IRIX_ROOT for original .fti icons\n");
            reportedMissingRoot = true;
        }
        return false;
    }
    if (!reportedRoot) {
        std::fprintf(stderr, "ImdInvertedTeeRenderer: using SGI filetype resources from %s\n", root.c_str());
        reportedRoot = true;
    }

    const std::string path = root + "/" + relative;
    std::ifstream in(path.c_str());
    if (!in.good()) return false;

    Display *dpy = XtDisplay(w);
    Window win = XtWindow(w);
    if (win == None) return false;
    GC gc = XCreateGC(dpy, win, 0, 0);

    DrawMode mode = kNone;
    std::vector<FtiPoint> points;
    unsigned long pixel = BlackPixelOfScreen(XtScreen(w));
    std::string line;

    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        if (starts(line, "color(")) {
            const std::string::size_type a = line.find('(');
            const std::string::size_type b = line.find(')', a + 1);
            if (a != std::string::npos && b != std::string::npos)
                pixel = colorFor(w, trim(line.substr(a + 1, b - a - 1)));
        } else if (starts(line, "bgnpolygon(")) {
            mode = kPolygon; points.clear();
        } else if (starts(line, "bgnoutlinepolygon(")) {
            mode = kOutlinePolygon; points.clear();
        } else if (starts(line, "bgnline(")) {
            mode = kLine; points.clear();
        } else if (starts(line, "vertex(")) {
            double px = 0.0, py = 0.0;
            if (std::sscanf(line.c_str(), "vertex(%lf, %lf)", &px, &py) == 2)
                points.push_back(FtiPoint{px, py});
        } else if (starts(line, "endpolygon(") || starts(line, "endoutlinepolygon(") || starts(line, "endline(")) {
            drawPrimitive(w, gc, win, mode, points, x, y, pixel);
            mode = kNone; points.clear();
        }
    }

    XFreeGC(dpy, gc);
    return true;
}
}

ImdIconRenderer::ImdIconRenderer() : _portableWidget(0) {}
ImdIconRenderer::~ImdIconRenderer() {}
void ImdIconRenderer::realize(Widget widget) { _portableWidget = widget; }

ImdInvertedTeeRenderer::ImdInvertedTeeRenderer() : ImdIconRenderer() {}
ImdInvertedTeeRenderer::~ImdInvertedTeeRenderer() {}
void ImdInvertedTeeRenderer::realize(Widget widget) { ImdIconRenderer::realize(widget); }

void ImdInvertedTeeRenderer::draw(ImdView &view, long x, long y)
{
    if (!_portableWidget || !XtIsRealized(_portableWidget)) return;
    ImdIconView *iconView = dynamic_cast<ImdIconView *>(&view);
    if (!iconView) return;

    const std::string resource = iconView->portableIconResource().stdString();
    if (resource.empty() || !drawFti(_portableWidget, resource, x, y)) return;

    const ImdString label = iconView->getLabel();
    if (!label.empty()) {
        Display *dpy = XtDisplay(_portableWidget);
        Window win = XtWindow(_portableWidget);
        GC gc = XCreateGC(dpy, win, 0, 0);
        XSetForeground(dpy, gc, BlackPixelOfScreen(XtScreen(_portableWidget)));
        XDrawString(dpy, win, gc,
                    static_cast<int>(x), static_cast<int>(y + 70),
                    label.c_str(), static_cast<int>(label.stdString().size()));
        XFreeGC(dpy, gc);
    }
}
