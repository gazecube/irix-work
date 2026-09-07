/*
 * IconLayout.c++ - portable ImdIconLayout reconstruction
 *
 * Binary anchor:
 *   libimdIconPanel.so SHA-256
 *   e23ceeede4bb684f6a397a503c148038c8329507396f6defaadf41ffc89b6309
 *
 * Original symbols:
 *   ImdIconLayout(Widget)           0x0968190c, 932 bytes
 *   moveView(ImdView&,long,long)    0x0969d1fc, 116 bytes
 *   addMapping(...)                 0x096908d4, 284 bytes
 *   getMapping(...)                 0x0967a744, 380 bytes
 *   loadMappings(ImdInitFile&)      0x09681f30, 1260 bytes
 *   saveMappings(ImdInitFile&)      0x0968ad24, 1684 bytes
 *   applyMappings(ImdView&)         0x0967c7b4, 164 bytes
 *   setSnapToGrid(bool)             0x0968241c, 64 bytes
 *   truncate(bool)                  0x0968b7f0, 8 bytes
 *   setScrollIncrement()            0x096ad058, 360 bytes
 */

#include "IconLayout.h"

#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace
{
struct LayoutResources
{
    int gridWidth;
    int staggerOffset;
    int minimumRightIconPadding;
    int baseOfIconPadding;
    int topMargin;
    int bottomMargin;
    int leftMargin;
    int rightMargin;
    int overlapLimit;
    int minimumIconWidth;
    int maximumIconWidth;
};

static XtResource layoutResources[] =
{
    { (char *)"gridWidth", (char *)"GridWidth", XtRInt, sizeof(int),
      XtOffsetOf(LayoutResources, gridWidth), XtRImmediate, (XtPointer)25 },
    { (char *)"staggerOffset", (char *)"StaggerOffset", XtRInt, sizeof(int),
      XtOffsetOf(LayoutResources, staggerOffset), XtRImmediate, (XtPointer)0 },
    { (char *)"minimumRightIconPadding", (char *)"MinimumRightIconPadding",
      XtRInt, sizeof(int), XtOffsetOf(LayoutResources, minimumRightIconPadding),
      XtRImmediate, (XtPointer)0 },
    { (char *)"baseOfIconPadding", (char *)"BaseOfIconPadding", XtRInt,
      sizeof(int), XtOffsetOf(LayoutResources, baseOfIconPadding),
      XtRImmediate, (XtPointer)0 },
    { (char *)"topMargin", (char *)"TopMargin", XtRInt, sizeof(int),
      XtOffsetOf(LayoutResources, topMargin), XtRImmediate, (XtPointer)0 },
    { (char *)"bottomMargin", (char *)"BottomMargin", XtRInt, sizeof(int),
      XtOffsetOf(LayoutResources, bottomMargin), XtRImmediate, (XtPointer)0 },
    { (char *)"leftMargin", (char *)"LeftMargin", XtRInt, sizeof(int),
      XtOffsetOf(LayoutResources, leftMargin), XtRImmediate, (XtPointer)0 },
    { (char *)"rightMargin", (char *)"RightMargin", XtRInt, sizeof(int),
      XtOffsetOf(LayoutResources, rightMargin), XtRImmediate, (XtPointer)0 },
    { (char *)"overlapLimit", (char *)"OverlapLimit", XtRInt, sizeof(int),
      XtOffsetOf(LayoutResources, overlapLimit), XtRImmediate, (XtPointer)0 },
    { (char *)"minimumIconWidth", (char *)"MinimumIconWidth", XtRInt,
      sizeof(int), XtOffsetOf(LayoutResources, minimumIconWidth),
      XtRImmediate, (XtPointer)0 },
    { (char *)"maximumIconWidth", (char *)"MaximumIconWidth", XtRInt,
      sizeof(int), XtOffsetOf(LayoutResources, maximumIconWidth),
      XtRImmediate, (XtPointer)0 }
};

static std::string
hexEncode(const std::string &value)
{
    static const char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(value.size() * 2);
    for (std::string::const_iterator it = value.begin(); it != value.end(); ++it) {
        unsigned char c = static_cast<unsigned char>(*it);
        out.push_back(digits[(c >> 4) & 0x0f]);
        out.push_back(digits[c & 0x0f]);
    }
    return out;
}

static int
hexNibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool
hexDecode(const std::string &value, std::string &out)
{
    if ((value.size() & 1U) != 0)
        return false;

    out.clear();
    out.reserve(value.size() / 2);
    for (std::string::size_type i = 0; i < value.size(); i += 2) {
        int hi = hexNibble(value[i]);
        int lo = hexNibble(value[i + 1]);
        if (hi < 0 || lo < 0)
            return false;
        out.push_back(static_cast<char>((hi << 4) | lo));
    }
    return true;
}
}

ImdIconLayout::ImdIconLayout(Widget widget)
    : ImdLayout(widget),
      _snapToGrid(false),
      _truncateRequested(false),
      _gridWidth(25),
      _staggerOffset(0),
      _minimumRightIconPadding(0),
      _baseOfIconPadding(0),
      _topMargin(0),
      _bottomMargin(0),
      _leftMargin(0),
      _rightMargin(0),
      _overlapLimit(0),
      _minimumIconWidth(0),
      _maximumIconWidth(0),
      _portableMappingPath(),
      _portableMappings()
{
    LayoutResources resources;

    resources.gridWidth = 25;
    resources.staggerOffset = 0;
    resources.minimumRightIconPadding = 0;
    resources.baseOfIconPadding = 0;
    resources.topMargin = 0;
    resources.bottomMargin = 0;
    resources.leftMargin = 0;
    resources.rightMargin = 0;
    resources.overlapLimit = 0;
    resources.minimumIconWidth = 0;
    resources.maximumIconWidth = 0;

    if (widget)
        XtGetApplicationResources(widget,
                                  (XtPointer)&resources,
                                  layoutResources,
                                  XtNumber(layoutResources),
                                  0,
                                  0);

    if (resources.gridWidth < 25)
        resources.gridWidth = 25;

    _gridWidth = resources.gridWidth;
    _staggerOffset = resources.staggerOffset;
    _minimumRightIconPadding = resources.minimumRightIconPadding;
    _baseOfIconPadding = resources.baseOfIconPadding;
    _topMargin = resources.topMargin;
    _bottomMargin = resources.bottomMargin;
    _leftMargin = resources.leftMargin;
    _rightMargin = resources.rightMargin;
    _overlapLimit = resources.overlapLimit;
    _minimumIconWidth = resources.minimumIconWidth;
    _maximumIconWidth = resources.maximumIconWidth;
}

ImdIconLayout::~ImdIconLayout()
{
}

std::string
ImdIconLayout::portableMappingName(const ImdView &view) const
{
    const ImdIconView *iconView = dynamic_cast<const ImdIconView *>(&view);
    if (!iconView)
        return std::string();

    /*
     * Original getMapping() obtains a view-derived ImdString and feeds it to
     * ImdLayout::getMappingName(). The exact bool-dependent decoration in
     * getMappingName() is still pending. The visible icon label is the proven
     * stable view string available in the reconstructed base class, so use it
     * as the temporary host cache key without claiming it is the exact IRIX
     * serialized mapping name.
     */
    return iconView->getLabel().stdString();
}

void
ImdIconLayout::portableRememberView(const ImdView &view)
{
    const std::string name = portableMappingName(view);
    if (name.empty())
        return;

    _portableMappings[name] = PortableMapping(view.portableX(),
                                              view.portableY(),
                                              0);
}

bool
ImdIconLayout::moveView(ImdView &view, long x, long y)
{
    /*
     * Original ImdIconLayout::moveView() @ 0x0969d1fc is the derived layout
     * entry reached by ImdViewPanelMoveView(). It forwards through another
     * layout virtual that owns the real positioning policy, then caches the
     * result in original layout state.
     */
    view.portableSetPosition(x, y);
    portableRememberView(view);
    return true;
}

bool
ImdIconLayout::portableLoadMappings(const char *path)
{
    _portableMappings.clear();
    _portableMappingPath = path ? path : "";

    if (_portableMappingPath.empty())
        return false;

    std::ifstream in(_portableMappingPath.c_str());
    if (!in.good())
        return true;

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#')
            continue;

        std::istringstream record(line);
        std::string section;
        std::string encodedName;
        long x = 0;
        long y = 0;
        int align = 0;

        if (!(record >> section >> encodedName >> x >> y >> align))
            continue;
        if (section != "IconMappings")
            continue;

        std::string name;
        if (!hexDecode(encodedName, name) || name.empty())
            continue;

        _portableMappings[name] = PortableMapping(x, y, align);
    }

    return true;
}

bool
ImdIconLayout::portableApplyMapping(ImdView &view) const
{
    const std::string name = portableMappingName(view);
    if (name.empty())
        return false;

    std::map<std::string, PortableMapping>::const_iterator it =
        _portableMappings.find(name);
    if (it == _portableMappings.end())
        return false;

    view.portableSetPosition(it->second.x, it->second.y);
    return true;
}

bool
ImdIconLayout::portableSaveMappings() const
{
    if (_portableMappingPath.empty())
        return false;

    const std::string tmpPath = _portableMappingPath + ".tmp";
    std::ofstream out(tmpPath.c_str(), std::ios::out | std::ios::trunc);
    if (!out.good())
        return false;

    out << "# IID RE portable presentation-state cache\n";
    out << "# Original fm uses FmState/IconMappings through ImdInitFile.\n";
    out << "# This text encoding is temporary and intentionally separate.\n";

    for (std::map<std::string, PortableMapping>::const_iterator it =
             _portableMappings.begin();
         it != _portableMappings.end(); ++it) {
        out << "IconMappings "
            << hexEncode(it->first) << ' '
            << it->second.x << ' '
            << it->second.y << ' '
            << it->second.align << '\n';
    }

    out.close();
    if (!out.good()) {
        std::remove(tmpPath.c_str());
        return false;
    }

    if (std::rename(tmpPath.c_str(), _portableMappingPath.c_str()) != 0) {
        std::remove(tmpPath.c_str());
        return false;
    }

    return true;
}

void
ImdIconLayout::setSnapToGrid(bool value)
{
    _snapToGrid = value;
}

void
ImdIconLayout::truncate(bool value)
{
    (void)value;
    _truncateRequested = false;
}

void
ImdIconLayout::setScrollIncrement()
{
}
