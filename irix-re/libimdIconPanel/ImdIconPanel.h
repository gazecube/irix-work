#ifndef IRIX_RE_LIBIMDICONPANEL_IMDICONPANEL_H
#define IRIX_RE_LIBIMDICONPANEL_IMDICONPANEL_H

#include "ImdString.h"

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
    ImdIconView();
    virtual ~ImdIconView();

    /*
     * Original exported symbols:
     *   setFlag__11ImdIconViewGQ2_10ImdFtrIcon10EIconStateb
     *   getFlag__11ImdIconViewGQ2_10ImdFtrIcon10EIconState
     *
     * The original parameter type is ImdFtrIcon::EIconState. The portable
     * reconstruction carries the recovered bitmask value as unsigned int until
     * the FTR enum declaration itself is reconstructed.
     */
    virtual void setFlag(unsigned int flag, bool value);
    virtual bool getFlag(unsigned int flag) const;

    /* Original exported label accessors. */
    virtual ImdString getLabel() const;
    virtual void setLabel(const ImdString &label);

    /* RE portable inspection only. */
    unsigned int portableFlags() const { return _portableFlags; }

private:
    /*
     * Source-level storage only; do not infer SGI offsets from these members.
     * Original N32 disassembly proves:
     *   +0x28 : icon-state bitmask
     *   +0x2c : ImdString label
     */
    unsigned int _portableFlags;
    ImdString _portableLabel;
};

class ImdIconPanel;

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

/*
 * Original overloads recovered from libimdIconPanel.so:
 *   ImdViewPanelAdoptView(Widget, ImdView *, ImdView *)
 *   ImdViewPanelAdoptView(Widget, ImdView *, ImdIconPanel *)
 */
void ImdViewPanelAdoptView(Widget, ImdView *, ImdView *);
void ImdViewPanelAdoptView(Widget, ImdView *, ImdIconPanel *);

class ImdIconPanel
{
public:
    ImdIconPanel(const char *name, Widget parent, Arg *args, int count);
    virtual ~ImdIconPanel();

    Widget baseWidget() const { return _baseWidget; }

    /*
     * Exact original symbol:
     *   adoptView__12ImdIconPanelGP11ImdIconViewT1
     *
     * Local vtable/disassembly proof resolves the second argument as the
     * parent view. A null parent selects the ImdIconPanel ownership overload
     * of ImdViewPanelAdoptView().
     */
    virtual void adoptView(ImdIconView *view, ImdIconView *parentView);

    /*
     * Vtable slot +656 reached by adoptView() resolves exactly to the original
     * applySelectStrings(ImdIconView &) method.
     */
    virtual void applySelectStrings(ImdIconView &view);

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
