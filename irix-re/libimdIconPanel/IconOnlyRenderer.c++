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
     * object.  Host ImdRenderer::realize() is currently a no-op. */
    ImdRenderer::realize(widget);
    _widget = widget;

    if (_gc && widget)
        XFreeGC(XtDisplay(widget), _gc);
    _gc = 0;

    if (widget && XtIsRealized(widget))
        _gc = XCreateGC(XtDisplay(widget), XtWindow(widget), 0, 0);
}

void ImdIconOnlyRenderer::draw(ImdView &view, long x, long y)
{
    (void)view;
    (void)x;
    (void)y;
    /* The original draw path is recovered at 0x0968b7f8 and will land in
     * the renderer pass.  Keeping the binary-backed class/lifecycle here is
     * enough for ImdIconFinder's original constructor sequence now. */
}
