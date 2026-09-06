#ifndef IRIX_RE_LIBIMDICONPANEL_IMDICONPANEL_H
#define IRIX_RE_LIBIMDICONPANEL_IMDICONPANEL_H

#include <X11/Intrinsic.h>
#include <X11/Xutil.h>
#include <vector>

/*
 * Original libimdIconPanel class names recovered from DWARF/symbols.
 * These are intentionally minimal portable base types. No unproven virtual
 * API is assigned here; they only let reconstructed subclasses use the
 * recovered ownership/adoption interfaces without reinterpret_cast bridges.
 */
class ImdRenderer
{
public:
    virtual ~ImdRenderer() {}
};

class ImdIconRenderer : public ImdRenderer
{
public:
    virtual ~ImdIconRenderer() {}
};

class ImdInvertedTeeRenderer : public ImdIconRenderer
{
public:
    virtual ~ImdInvertedTeeRenderer() {}
};

class ImdLayout
{
public:
    explicit ImdLayout(Widget widget = 0) : _widget(widget) {}
    virtual ~ImdLayout() {}

    Widget widget() const { return _widget; }

protected:
    Widget _widget;
};

class ImdView
{
public:
    virtual ~ImdView() {}
};

class ImdIconView : public ImdView
{
public:
    virtual ~ImdIconView() {}
};

extern WidgetClass imdViewPanelWidgetClass;

extern "C" Widget ImdCreateViewPanel(
    Widget parent,
    char *name,
    Arg *args,
    Cardinal count);

extern "C" void ImdViewPanelAdoptRenderer(Widget, ImdRenderer *);
extern "C" void ImdViewPanelAdoptLayout(Widget, ImdLayout *);
extern "C" void ImdViewPanelSetScale(Widget, float, long, long);
extern "C" void ImdViewPanelBeginBlockChanges(Widget);
extern "C" void ImdViewPanelEndBlockChanges(Widget);

class ImdIconPanel
{
public:
    ImdIconPanel(const char *name, Widget parent, Arg *args, int count);
    virtual ~ImdIconPanel();

    Widget baseWidget() const { return _baseWidget; }

    /* Original public method name recovered from libimdIconPanel DWARF. */
    void adoptView(ImdView *view);

    /* Portable inspection only; not claimed as original public interface. */
    const std::vector<ImdView *> &portableViews() const { return _views; }

    /*
     * Portable ownership seam used while ImdViewPanelDeleteAllViews() and the
     * original ImdIconPanel::deleteView() contract are still being restored.
     * Callers must opt in to destroying adopted views.
     */
    void portableResetViews(bool destroyViews);

private:
    Widget _baseWidget;
    std::vector<ImdView *> _views;
};

#endif /* IRIX_RE_LIBIMDICONPANEL_IMDICONPANEL_H */
