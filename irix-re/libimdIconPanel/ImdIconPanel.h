#ifndef IRIX_RE_LIBIMDICONPANEL_IMDICONPANEL_H
#define IRIX_RE_LIBIMDICONPANEL_IMDICONPANEL_H

#include <X11/Intrinsic.h>

class ImdRenderer;
class ImdLayout;

extern WidgetClass imdViewPanelWidgetClass;

extern "C" Widget ImdCreateViewPanel(
    Widget parent,
    char *name,
    Arg *args,
    Cardinal count);

extern "C" void ImdViewPanelAdoptRenderer(Widget, ImdRenderer *);
extern "C" void ImdViewPanelAdoptLayout(Widget, ImdLayout *);
extern "C" void ImdViewPanelSetScale(Widget, float, long, long);

class ImdIconPanel
{
public:
    ImdIconPanel(const char *name, Widget parent, Arg *args, int count);
    virtual ~ImdIconPanel();

    Widget baseWidget() const { return _baseWidget; }

private:
    Widget _baseWidget;
};

#endif /* IRIX_RE_LIBIMDICONPANEL_IMDICONPANEL_H */
