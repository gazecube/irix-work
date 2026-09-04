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

    Pixel foreground = BlackPixelOfScreen(screen);
    Pixel background = WhitePixelOfScreen(screen);
    Pixel shadow = foreground;
    XtVaGetValues(
        widget,
        XmNforeground, &foreground,
        XmNbackground, &background,
        XmNbottomShadowColor, &shadow,
        NULL);

    XGCValues values;
    values.foreground = foreground;
    GC outlineGC = XCreateGC(display, window, GCForeground, &values);

    values.foreground = WhitePixelOfScreen(screen);
    GC iconGC = XCreateGC(display, window, GCForeground, &values);

    values.foreground = shadow;
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
}
