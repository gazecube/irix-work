/*
 * IconLayout.c++ - portable ImdIconLayout reconstruction
 *
 * Binary anchor:
 *   libimdIconPanel.so SHA-256
 *   e23ceeede4bb684f6a397a503c148038c8329507396f6defaadf41ffc89b6309
 *
 * Original symbols:
 *   ImdIconLayout(Widget)      0x0968190c, 932 bytes
 *   setSnapToGrid(bool)        0x0968241c, 64 bytes
 *   truncate(bool)             0x0968b7f0, 8 bytes
 *   setScrollIncrement()       0x096ad058, 360 bytes
 */

#include "IconLayout.h"

#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>

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

/*
 * Resource names/classes are present verbatim in the original library.
 * The original constructor also clamps one recovered integer field to at
 * least 25; the field is consistent with gridWidth and the FileManager
 * app-default sets gridWidth to 30. Defaults other than that clamp remain
 * deliberately neutral until the original resource table is decoded.
 */
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
      _maximumIconWidth(0)
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

void
ImdIconLayout::setSnapToGrid(bool value)
{
    /* Original stores the Boolean at n32 object offset +0x44. */
    _snapToGrid = value;
}

void
ImdIconLayout::truncate(bool value)
{
    /*
     * The original eight-byte body ignores its Boolean argument and stores
     * zero at object offset +0x10. Do not invent label mutation here before
     * that field's ownership is recovered.
     */
    (void)value;
    _truncateRequested = false;
}

void
ImdIconLayout::setScrollIncrement()
{
    /*
     * The original body is known (0x096ad058, 360 bytes), but its scroller
     * member/call targets are not yet named. Keep this seam live and inert
     * rather than guessing the scrollbar contract.
     */
}
