#ifndef IRIX_RE_CLOGIN_FTI_VECTOR_ICON_H
#define IRIX_RE_CLOGIN_FTI_VECTOR_ICON_H

#include <X11/Intrinsic.h>

#include <string>
#include <vector>

/*
 * Portable reader for the small vector-command subset used by the preserved
 * IRIX .fti icon resources. This is not claimed to reproduce the original
 * filetype renderer ABI. It exists so the reconstructed Clogin probe can draw
 * an immutable installed IRIX asset while the original Imd/Ftr rendering path
 * is recovered from the binaries.
 */
class FtiVectorIcon
{
public:
    FtiVectorIcon();

    bool load(const std::string &path);
    bool loaded() const { return !_primitives.empty(); }

    void draw(Widget widget, int x, int y, unsigned int extent) const;

private:
    struct Point
    {
        double x;
        double y;
    };

    enum PrimitiveKind
    {
        Polygon,
        OutlinePolygon,
        Line
    };

    struct Primitive
    {
        PrimitiveKind kind;
        std::string fillColor;
        std::string outlineColor;
        std::vector<Point> points;
    };

    static std::string trim(const std::string &value);
    static bool parseCallArgument(
        const std::string &line,
        const char *call,
        std::string *argument);
    static bool parseVertex(const std::string &line, Point *point);

    std::vector<Primitive> _primitives;
};

#endif /* IRIX_RE_CLOGIN_FTI_VECTOR_ICON_H */
