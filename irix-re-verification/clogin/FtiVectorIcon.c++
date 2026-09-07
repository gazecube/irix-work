#include "FtiVectorIcon.h"

#include <Xm/Xm.h>
#include <X11/Xlib.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>

FtiVectorIcon::FtiVectorIcon()
{
}

std::string FtiVectorIcon::trim(const std::string &value)
{
    std::string::size_type first = 0;
    while (first < value.size() &&
           std::isspace(static_cast<unsigned char>(value[first])))
        ++first;

    std::string::size_type last = value.size();
    while (last > first &&
           std::isspace(static_cast<unsigned char>(value[last - 1])))
        --last;

    return value.substr(first, last - first);
}

bool FtiVectorIcon::parseCallArgument(
    const std::string &line,
    const char *call,
    std::string *argument)
{
    const std::string prefix = std::string(call) + "(";
    if (line.compare(0, prefix.size(), prefix) != 0)
        return false;

    std::string::size_type close = line.find(')', prefix.size());
    if (close == std::string::npos)
        return false;

    if (argument)
        *argument = trim(line.substr(prefix.size(), close - prefix.size()));
    return true;
}

bool FtiVectorIcon::parseVertex(const std::string &line, Point *point)
{
    if (!point)
        return false;

    double x = 0.0;
    double y = 0.0;
    if (std::sscanf(line.c_str(), "vertex(%lf, %lf);", &x, &y) != 2)
        return false;

    point->x = x;
    point->y = y;
    return true;
}

bool FtiVectorIcon::load(const std::string &path)
{
    _primitives.clear();

    std::ifstream input(path.c_str());
    if (!input)
        return false;

    std::string activeColor = "iconcolor";
    Primitive current;
    bool inPrimitive = false;

    std::string rawLine;
    while (std::getline(input, rawLine))
    {
        const std::string line = trim(rawLine);
        if (line.empty() || line[0] == '#')
            continue;

        std::string argument;
        if (parseCallArgument(line, "color", &argument))
        {
            activeColor = argument;
            continue;
        }

        if (line == "bgnpolygon();" ||
            line == "bgnoutlinepolygon();" ||
            line == "bgnline();")
        {
            current = Primitive();
            current.fillColor = activeColor;
            current.outlineColor = activeColor;
            current.points.clear();

            if (line == "bgnpolygon();")
                current.kind = Polygon;
            else if (line == "bgnoutlinepolygon();")
                current.kind = OutlinePolygon;
            else
                current.kind = Line;

            inPrimitive = true;
            continue;
        }

        if (!inPrimitive)
            continue;

        Point point;
        if (parseVertex(line, &point))
        {
            current.points.push_back(point);
            continue;
        }

        if (line == "endpolygon();")
        {
            if (current.points.size() >= 3)
                _primitives.push_back(current);
            inPrimitive = false;
            continue;
        }

        if (parseCallArgument(line, "endoutlinepolygon", &argument))
        {
            current.outlineColor = argument;
            if (current.points.size() >= 3)
                _primitives.push_back(current);
            inPrimitive = false;
            continue;
        }

        if (line == "endline();")
        {
            if (current.points.size() >= 2)
                _primitives.push_back(current);
            inPrimitive = false;
            continue;
        }
    }

    return !_primitives.empty();
}

namespace
{
Pixel allocRgb(Display *display, Colormap colormap,
               unsigned short red, unsigned short green, unsigned short blue,
               Pixel fallback)
{
    XColor color;
    color.red = red;
    color.green = green;
    color.blue = blue;
    color.flags = DoRed | DoGreen | DoBlue;
    color.pixel = fallback;
    if (XAllocColor(display, colormap, &color))
        return color.pixel;
    return fallback;
}
}

void FtiVectorIcon::draw(
    Widget widget,
    int x,
    int y,
    unsigned int extent) const
{
    if (!widget || !XtIsRealized(widget) || _primitives.empty() || extent == 0)
        return;

    Display *display = XtDisplay(widget);
    Window window = XtWindow(widget);
    Screen *screen = XtScreen(widget);
    Colormap colormap = DefaultColormapOfScreen(screen);
    XtVaGetValues(widget, XmNcolormap, &colormap, NULL);

    Pixel foreground = BlackPixelOfScreen(screen);
    XtVaGetValues(widget, XmNforeground, &foreground, NULL);

    /*
     * Binary-proven special-color definitions from IRIX iconsmith:
     *
     *   iconcolor    = -127 = 0x7f
     *   outlinecolor = 0
     *   shadowcolor  = -136 = 0x88
     *
     * ImdFtrIconXParams::setFillStyle() interprets a negative value as:
     *
     *   low nibble  -> foreground palette index
     *   high nibble -> background palette index
     *   fill style  -> opaque stipple
     *
     * initColormapInfo() has a 16-color fallback palette. The entries used by
     * Users.Token.fti are:
     *
     *   [7]  = 0xffff,0xffff,0xffff
     *   [8]  = 0x5555,0x5555,0x5555
     *   [15] = 0xaaaa,0xaaaa,0xaaaa
     *
     * The original 8x8 stipple bytes at libimdFtrIcon.so sdata 0x09825cb0
     * are 55 aa 55 aa 55 aa 55 aa. This portable FTI test backend reproduces
     * those semantics directly. It is still not a replacement for the real
     * ImdFtrIconX renderer/cache/mask path.
     */
    const Pixel white = allocRgb(
        display, colormap, 0xffff, 0xffff, 0xffff,
        WhitePixelOfScreen(screen));
    const Pixel darkGray = allocRgb(
        display, colormap, 0x5555, 0x5555, 0x5555,
        foreground);
    const Pixel lightGray = allocRgb(
        display, colormap, 0xaaaa, 0xaaaa, 0xaaaa,
        white);

    static const char stippleBits[8] = {
        static_cast<char>(0x55), static_cast<char>(0xaa),
        static_cast<char>(0x55), static_cast<char>(0xaa),
        static_cast<char>(0x55), static_cast<char>(0xaa),
        static_cast<char>(0x55), static_cast<char>(0xaa)
    };
    Pixmap stipple = XCreateBitmapFromData(
        display, window, stippleBits, 8, 8);

    XGCValues values;
    values.foreground = foreground;
    GC outlineGC = XCreateGC(display, window, GCForeground, &values);

    values.foreground = lightGray;
    values.background = white;
    values.stipple = stipple;
    values.fill_style = FillOpaqueStippled;
    GC iconGC = XCreateGC(
        display,
        window,
        GCForeground | GCBackground | GCStipple | GCFillStyle,
        &values);

    values.foreground = darkGray;
    GC shadowGC = XCreateGC(display, window, GCForeground, &values);

    const double scale = static_cast<double>(extent) / 100.0;

    for (std::size_t i = 0; i < _primitives.size(); ++i)
    {
        const Primitive &primitive = _primitives[i];
        if (primitive.points.empty())
            continue;

        std::vector<XPoint> points;
        points.reserve(primitive.points.size());
        for (std::size_t p = 0; p < primitive.points.size(); ++p)
        {
            XPoint point;
            point.x = static_cast<short>(
                x + primitive.points[p].x * scale + 0.5);
            point.y = static_cast<short>(
                y + (100.0 - primitive.points[p].y) * scale + 0.5);
            points.push_back(point);
        }

        GC fillGC = iconGC;
        if (primitive.fillColor == "outlinecolor")
            fillGC = outlineGC;
        else if (primitive.fillColor == "shadowcolor")
            fillGC = shadowGC;

        if (primitive.kind == Polygon || primitive.kind == OutlinePolygon)
        {
            XFillPolygon(
                display,
                window,
                fillGC,
                &points[0],
                static_cast<int>(points.size()),
                Complex,
                CoordModeOrigin);
        }

        if (primitive.kind == OutlinePolygon)
        {
            GC edgeGC = outlineGC;
            if (primitive.outlineColor == "iconcolor")
                edgeGC = iconGC;
            else if (primitive.outlineColor == "shadowcolor")
                edgeGC = shadowGC;

            std::vector<XPoint> closed(points);
            closed.push_back(points[0]);
            XDrawLines(
                display,
                window,
                edgeGC,
                &closed[0],
                static_cast<int>(closed.size()),
                CoordModeOrigin);
        }
        else if (primitive.kind == Line)
        {
            XDrawLines(
                display,
                window,
                fillGC,
                &points[0],
                static_cast<int>(points.size()),
                CoordModeOrigin);
        }
    }

    XFreeGC(display, shadowGC);
    XFreeGC(display, iconGC);
    XFreeGC(display, outlineGC);
    if (stipple != None)
        XFreePixmap(display, stipple);
}
