#include "IconOnlyRenderer.h"

ImdIconOnlyRenderer::ImdIconOnlyRenderer()
    : ImdRenderer(),
      _widget(0),
      _gc(0)
{
}

ImdIconOnlyRenderer::~ImdIconOnlyRenderer()
{
    /* Original destructor @ 0x09690fdc frees the GC before chaining to
     * ImdRenderer::~ImdRenderer(). */
    if (_gc && _widget && XtDisplay(_widget))
        XFreeGC(XtDisplay(_widget), _gc);
    _gc = 0;
    _widget = 0;
}

void ImdIconOnlyRenderer::realize(Widget widget)
{
    /* Original realize() first calls ImdRenderer::realize(widget), then
     * XCreateGC(display, drawable, 0, 0), storing the GC in the derived
     * object. Host ImdRenderer::realize() is currently a no-op. */
    ImdRenderer::realize(widget);
    _widget = widget;

    if (_gc && widget)
        XFreeGC(XtDisplay(widget), _gc);
    _gc = 0;

    if (widget && XtIsRealized(widget))
        _gc = XCreateGC(XtDisplay(widget), XtWindow(widget), 0, 0);
}

void ImdIconOnlyRenderer::setSize(ImdView &view)
{
    /* Exact original @ 0x0968b8a0: width=100, height=100. */
    view.portableSetSize(100, 100);
}

bool ImdIconOnlyRenderer::intersects(ImdView &view, long x, long y)
{
    /* Exact inclusive bounds test from 0x09690f08. */
    if (x < view.portableX())
        return false;
    if (x > view.portableX() + view.portableWidth())
        return false;
    if (y < view.portableY())
        return false;
    if (y > view.portableY() + view.portableHeight())
        return false;
    return true;
}

void ImdIconOnlyRenderer::draw(ImdView &view, long x, long y)
{
    (void)view;
    (void)x;
    (void)y;
    /* Original draw path is at 0x0968b7f8. It depends on the lower renderer
     * drawing helpers and the original view state bits; land it once those
     * collaborators are recovered rather than inventing a host drawing path. */
}
