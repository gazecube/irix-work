#ifndef IRIX_RE_LIBIMDICONPANEL_ICONONLYRENDERER_H
#define IRIX_RE_LIBIMDICONPANEL_ICONONLYRENDERER_H

#include "ImdIconPanel.h"

#include <X11/Xlib.h>

/*
 * ImdIconOnlyRenderer
 *
 * Binary anchor: libimdIconPanel.so
 *   ctor        0x0968245c, 0x4c bytes
 *   realize     0x096824a8, 0x58 bytes
 *   setSize     0x0968b8a0, 0x10 bytes
 *   intersects  0x09690f08, 0x54 bytes
 *
 * The original class derives directly from ImdRenderer.  The constructor
 * calls ImdRenderer::ImdRenderer(), installs its vtable, and clears the GC
 * member at N32 offset +0x38.  realize() calls ImdRenderer::realize() and
 * creates a zero-valuemask GC for the renderer drawable.
 */
class ImdIconOnlyRenderer : public ImdRenderer
{
public:
    ImdIconOnlyRenderer();
    virtual ~ImdIconOnlyRenderer();
    virtual void realize(Widget widget);
    virtual void draw(ImdView &view, long x, long y);

    void setSize(ImdView &view);
    bool intersects(ImdView &view, long x, long y);

    GC portableGC() const { return _gc; }

private:
    Widget _widget;
    GC _gc;
};

#endif
