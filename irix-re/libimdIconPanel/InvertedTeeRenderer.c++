#include "ImdIconPanel.h"

#include <X11/StringDefs.h>
#include <X11/Xlib.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace
{
struct FtiPoint { double x; double y; };
enum DrawMode {
    kNone,
    kPolygon,
    kLine,
    kClosedLine,
    kOutlinePolygon,
    kPoint
};

/*
 * Original ImdInvertedTeeRenderer binary anchors:
 *   ctor         0x09684198
 *   setSize      0x096842f0
 *   getIconX     0x096b5b70
 *   getTextX     0x096b5bbc
 *   getTextY     0x096b5c08
 *
 * The ctor stores iconScale = scale * 0.5. The coordinate helpers prove
 * that both icon and text are horizontally centered in the ImdView width:
 *
 *   iconX = x + (scale * view.width - iconScale * 100) / 2
 *   textX = x + (scale * view.width - textWidth) / 2
 *
 * At the default scale this makes the nominal 100x100 FTI geometry 50x50.
 */
static const double kDefaultScale = 1.0;
static const double kDefaultIconScale = 0.5;
static const double kFtiNominalExtent = 100.0;

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

    if (name == "outlinecolor")
        return allocNamed(w, "black", BlackPixelOfScreen(XtScreen(w)));
    if (name == "shadowcolor")
        return allocNamed(w, "gray45", BlackPixelOfScreen(XtScreen(w)));
    if (name == "iconcolor")
        return allocNamed(w, "white", WhitePixelOfScreen(XtScreen(w)));
    return bg;
}

static unsigned long allocRgb(Widget w,
                              unsigned short red,
                              unsigned short green,
                              unsigned short blue,
                              unsigned long fallback)
{
    if (!w)
        return fallback;

    Display *dpy = XtDisplay(w);
    if (!dpy)
        return fallback;

    Colormap cmap = None;
    XtVaGetValues(w, XtNcolormap, &cmap, NULL);
    if (cmap == None)
        cmap = DefaultColormapOfScreen(XtScreen(w));

    XColor color;
    std::memset(&color, 0, sizeof(color));
    color.red = red;
    color.green = green;
    color.blue = blue;
    color.flags = DoRed | DoGreen | DoBlue;
    if (!XAllocColor(dpy, cmap, &color))
        return fallback;
    return color.pixel;
}

static XPoint toXPoint(long x, long y, const FtiPoint &p)
{
    XPoint out;
    out.x = static_cast<short>(x + p.x * kDefaultIconScale);
    out.y = static_cast<short>(y +
        (kFtiNominalExtent - p.y) * kDefaultIconScale);
    return out;
}

static std::string argument(const std::string &line)
{
    const std::string::size_type a = line.find('(');
    const std::string::size_type b = line.find(')', a == std::string::npos ? 0 : a + 1);
    if (a == std::string::npos || b == std::string::npos)
        return std::string();
    return trim(line.substr(a + 1, b - a - 1));
}

static void drawPrimitive(Widget w, GC gc, Drawable drawable, DrawMode mode,
                          const std::vector<FtiPoint> &points,
                          long x, long y,
                          unsigned long fillPixel,
                          unsigned long outlinePixel)
{
    if (points.empty()) return;

    std::vector<XPoint> xp(points.size());
    for (std::size_t i = 0; i < points.size(); ++i)
        xp[i] = toXPoint(x, y, points[i]);

    Display *dpy = XtDisplay(w);

    if (mode == kPolygon || mode == kOutlinePolygon) {
        XSetForeground(dpy, gc, fillPixel);
        XFillPolygon(dpy, drawable, gc, &xp[0],
                     static_cast<int>(xp.size()), Complex, CoordModeOrigin);
    }

    if (mode == kLine) {
        XSetForeground(dpy, gc, fillPixel);
        XDrawLines(dpy, drawable, gc, &xp[0],
                   static_cast<int>(xp.size()), CoordModeOrigin);
    } else if (mode == kClosedLine || mode == kOutlinePolygon) {
        std::vector<XPoint> closed(xp);
        closed.push_back(xp.front());
        XSetForeground(dpy, gc,
                       mode == kOutlinePolygon ? outlinePixel : fillPixel);
        XDrawLines(dpy, drawable, gc, &closed[0],
                   static_cast<int>(closed.size()), CoordModeOrigin);
    } else if (mode == kPoint) {
        XSetForeground(dpy, gc, fillPixel);
        XDrawPoints(dpy, drawable, gc, &xp[0],
                    static_cast<int>(xp.size()), CoordModeOrigin);
    }
}

static bool drawFti(Widget w, Drawable drawable,
                    const std::string &relative, long x, long y)
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
        std::fprintf(stderr,
                     "ImdInvertedTeeRenderer: using SGI filetype resources from %s\n",
                     root.c_str());
        reportedRoot = true;
    }

    const std::string path = root + "/" + relative;
    std::ifstream in(path.c_str());
    if (!in.good() || drawable == None) return false;

    Display *dpy = XtDisplay(w);
    GC gc = XCreateGC(dpy, drawable, 0, 0);

    DrawMode mode = kNone;
    std::vector<FtiPoint> points;
    unsigned long pixel = BlackPixelOfScreen(XtScreen(w));
    unsigned long outlinePixel = colorFor(w, "outlinecolor");
    bool havePen = false;
    FtiPoint pen = { 0.0, 0.0 };
    std::string line;

    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;

        if (starts(line, "color(")) {
            pixel = colorFor(w, argument(line));
        } else if (starts(line, "bgnpolygon(")) {
            mode = kPolygon; points.clear();
        } else if (starts(line, "bgnoutlinepolygon(")) {
            mode = kOutlinePolygon; points.clear();
        } else if (starts(line, "bgnclosedline(")) {
            mode = kClosedLine; points.clear();
        } else if (starts(line, "bgnline(")) {
            mode = kLine; points.clear();
        } else if (starts(line, "bgnpoint(")) {
            mode = kPoint; points.clear();
        } else if (starts(line, "vertex(")) {
            double px = 0.0, py = 0.0;
            if (std::sscanf(line.c_str(), "vertex(%lf, %lf)", &px, &py) == 2)
                points.push_back(FtiPoint{px, py});
        } else if (starts(line, "endoutlinepolygon(")) {
            const std::string border = argument(line);
            outlinePixel = border.empty() ? colorFor(w, "outlinecolor")
                                          : colorFor(w, border);
            drawPrimitive(w, gc, drawable, mode, points, x, y, pixel, outlinePixel);
            mode = kNone;
            points.clear();
        } else if (starts(line, "endpolygon(") ||
                   starts(line, "endclosedline(") ||
                   starts(line, "endline(") ||
                   starts(line, "endpoint(")) {
            drawPrimitive(w, gc, drawable, mode, points, x, y, pixel, outlinePixel);
            mode = kNone;
            points.clear();
        } else if (starts(line, "move(")) {
            double px = 0.0, py = 0.0;
            if (std::sscanf(line.c_str(), "move(%lf, %lf)", &px, &py) == 2) {
                pen.x = px; pen.y = py; havePen = true;
            }
        } else if (starts(line, "draw(")) {
            double px = 0.0, py = 0.0;
            if (havePen && std::sscanf(line.c_str(), "draw(%lf, %lf)", &px, &py) == 2) {
                const FtiPoint next = { px, py };
                XPoint seg[2] = { toXPoint(x, y, pen), toXPoint(x, y, next) };
                XSetForeground(dpy, gc, pixel);
                XDrawLines(dpy, drawable, gc, seg, 2, CoordModeOrigin);
                pen = next;
            }
        } else if (starts(line, "pmv(")) {
            double px = 0.0, py = 0.0;
            if (std::sscanf(line.c_str(), "pmv(%lf, %lf)", &px, &py) == 2) {
                mode = kPolygon;
                points.clear();
                points.push_back(FtiPoint{px, py});
            }
        } else if (starts(line, "pdr(")) {
            double px = 0.0, py = 0.0;
            if (std::sscanf(line.c_str(), "pdr(%lf, %lf)", &px, &py) == 2)
                points.push_back(FtiPoint{px, py});
        } else if (starts(line, "pclos(")) {
            drawPrimitive(w, gc, drawable, kPolygon, points, x, y,
                          pixel, outlinePixel);
            mode = kNone;
            points.clear();
        } else if (starts(line, "bclos(")) {
            const std::string border = argument(line);
            const unsigned long borderPixel = border.empty()
                ? colorFor(w, "outlinecolor") : colorFor(w, border);
            drawPrimitive(w, gc, drawable, kOutlinePolygon, points, x, y,
                          pixel, borderPixel);
            mode = kNone;
            points.clear();
        }
    }

    XFreeGC(dpy, gc);
    return true;
}

struct LabelMetrics
{
    int width;
    int ascent;
    int descent;

    LabelMetrics() : width(0), ascent(0), descent(0) {}
};

static LabelMetrics measureLabel(Display *dpy, GC gc, const ImdString &label)
{
    LabelMetrics result;
    if (!dpy || !gc || label.empty()) return result;

    XFontStruct *font = XQueryFont(dpy, XGContextFromGC(gc));
    if (!font) return result;

    result.width = XTextWidth(font, label.c_str(),
                              static_cast<int>(label.stdString().size()));
    result.ascent = font->ascent;
    result.descent = font->descent;
    XFreeFontInfo(0, font, 1);
    return result;
}

static void drawSgiSelectedBorder(Widget widget,
                                  Drawable drawable,
                                  GC gc,
                                  long x,
                                  long y,
                                  long width,
                                  long height,
                                  unsigned int iconState)
{
    /*
     * Original libimdFtrGallery.so drawIcon__16ImdFtrImageXImpl... checks:
     *
     *     iconState & 0x2e
     *
     * and selects ImdFtrImageParams::selectedBorderWidth when any of those
     * state bits are present. ImdIconPanel::select() uses state bit 0x08.
     *
     * __ct__17ImdFtrImageParamsGv proves the defaults:
     *     borderWidth         = 1
     *     selectedBorderWidth = 2
     *
     * The same draw routine then emits selectedBorderWidth concentric
     * XDrawRectangle calls, moving x/y inward one pixel and shrinking width/
     * height by two after each rectangle.
     */
    if (!widget || drawable == None || !gc || (iconState & 0x2eu) == 0u)
        return;

    Display *dpy = XtDisplay(widget);
    if (!dpy || width <= 0 || height <= 0)
        return;

    /*
     * ImdFtrIconXParams::setFillStyle(-127), used by the selected/special
     * border path, is binary-proven to select:
     *
     *   foreground colormap entry 15 = #AAAAAA
     *   background colormap entry  7 = #FFFFFF
     *   8x8 stipple bytes           = 55 aa 55 aa 55 aa 55 aa
     *   fill style                  = FillOpaqueStippled
     */
    static const char stippleBits[8] = {
        static_cast<char>(0x55), static_cast<char>(0xaa),
        static_cast<char>(0x55), static_cast<char>(0xaa),
        static_cast<char>(0x55), static_cast<char>(0xaa),
        static_cast<char>(0x55), static_cast<char>(0xaa)
    };

    const unsigned long gray = allocRgb(widget, 0xaaaa, 0xaaaa, 0xaaaa,
                                         BlackPixelOfScreen(XtScreen(widget)));
    const unsigned long white = allocRgb(widget, 0xffff, 0xffff, 0xffff,
                                          WhitePixelOfScreen(XtScreen(widget)));
    Pixmap stipple = XCreateBitmapFromData(dpy,
                                            drawable,
                                            stippleBits,
                                            8,
                                            8);
    if (stipple == None)
        return;

    XSetForeground(dpy, gc, gray);
    XSetBackground(dpy, gc, white);
    XSetStipple(dpy, gc, stipple);
    XSetFillStyle(dpy, gc, FillOpaqueStippled);

    const int borderWidth = 2;
    for (int i = 0; i < borderWidth; ++i) {
        const long rectWidth = width - 1 - (2 * i);
        const long rectHeight = height - 1 - (2 * i);
        if (rectWidth < 0 || rectHeight < 0)
            break;
        XDrawRectangle(dpy,
                       drawable,
                       gc,
                       static_cast<int>(x + i),
                       static_cast<int>(y + i),
                       static_cast<unsigned int>(rectWidth),
                       static_cast<unsigned int>(rectHeight));
    }

    XSetFillStyle(dpy, gc, FillSolid);
    XFreePixmap(dpy, stipple);
}
}

ImdIconRenderer::ImdIconRenderer() : _portableWidget(0) {}
ImdIconRenderer::~ImdIconRenderer() {}
void ImdIconRenderer::realize(Widget widget) { _portableWidget = widget; }

ImdInvertedTeeRenderer::ImdInvertedTeeRenderer() : ImdIconRenderer() {}
ImdInvertedTeeRenderer::~ImdInvertedTeeRenderer() {}
void ImdInvertedTeeRenderer::realize(Widget widget)
{
    ImdIconRenderer::realize(widget);
}

bool ImdInvertedTeeRenderer::portableDrawToDrawable(ImdView &view,
                                                     Drawable drawable,
                                                     long x, long y)
{
    if (!_portableWidget || !XtIsRealized(_portableWidget) || drawable == None)
        return false;

    ImdIconView *iconView = dynamic_cast<ImdIconView *>(&view);
    if (!iconView) return false;

    Display *dpy = XtDisplay(_portableWidget);
    if (!dpy) return false;

    GC gc = XCreateGC(dpy, drawable, 0, 0);
    if (!gc) return false;

    const ImdString label = iconView->getLabel();
    const LabelMetrics metrics = measureLabel(dpy, gc, label);

    const long iconExtent = static_cast<long>(
        kFtiNominalExtent * kDefaultIconScale);
    const long viewWidth = std::max(iconExtent,
                                    static_cast<long>(metrics.width));
    const long viewHeight = iconExtent +
        static_cast<long>(metrics.ascent + metrics.descent);

    view.portableSetSize(viewWidth, viewHeight);

    const long iconX = x + static_cast<long>(
        (kDefaultScale * viewWidth - iconExtent) * 0.5);
    const long textX = x + static_cast<long>(
        (kDefaultScale * viewWidth - metrics.width) * 0.5);
    const long textBaselineY = y + iconExtent + metrics.ascent;

    const std::vector<ImdString> &resources = iconView->portableIconResources();
    for (std::size_t i = 0; i < resources.size(); ++i) {
        const std::string resource = resources[i].stdString();
        if (!resource.empty())
            drawFti(_portableWidget, drawable, resource, iconX, y);
    }

    /*
     * Preserve the original FTR selected-state border semantics around the
     * icon raster itself. The source binary applies the same special border to
     * state mask 0x2e; ordinary selected icons enter here through state 0x08.
     */
    drawSgiSelectedBorder(_portableWidget,
                          drawable,
                          gc,
                          iconX,
                          y,
                          iconExtent,
                          iconExtent,
                          iconView->portableFlags());

    if (!label.empty()) {
        XSetFillStyle(dpy, gc, FillSolid);
        XSetForeground(dpy, gc, BlackPixelOfScreen(XtScreen(_portableWidget)));
        XDrawString(dpy, drawable, gc,
                    static_cast<int>(textX),
                    static_cast<int>(textBaselineY),
                    label.c_str(),
                    static_cast<int>(label.stdString().size()));
    }

    XFreeGC(dpy, gc);
    return true;
}

void ImdInvertedTeeRenderer::draw(ImdView &view, long x, long y)
{
    if (!_portableWidget || !XtIsRealized(_portableWidget)) return;
    const Window win = XtWindow(_portableWidget);
    if (win == None) return;
    (void)portableDrawToDrawable(view, win, x, y);
}
