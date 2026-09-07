#ifndef IRIX_RE_LIBIMDICONPANEL_IMDICONPANEL_H
#define IRIX_RE_LIBIMDICONPANEL_IMDICONPANEL_H

#include "ImdString.h"

#include <X11/Intrinsic.h>
#include <X11/Xutil.h>
#include <vector>

class ImdRenderer
{
public:
    virtual ~ImdRenderer() {}
    virtual void realize(Widget) {}
    virtual void draw(class ImdView &, long, long) {}
};

class ImdIconRenderer : public ImdRenderer
{
public:
    ImdIconRenderer();
    virtual ~ImdIconRenderer();
    virtual void realize(Widget widget);
    virtual void draw(class ImdView &, long, long) {}

protected:
    Widget _portableWidget;
};

class ImdInvertedTeeRenderer : public ImdIconRenderer
{
public:
    ImdInvertedTeeRenderer();
    virtual ~ImdInvertedTeeRenderer();
    virtual void realize(Widget widget);
    virtual void draw(class ImdView &view, long x, long y);
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
    ImdView()
        : _portableX(0), _portableY(0),
          _portableWidth(0), _portableHeight(0),
          _portableVisible(true) {}
    virtual ~ImdView() {}

    /*
     * Portable source-level storage for original ImdView geometry.
     * Local disassembly proves the N32 fields used by
     * ImdViewPanelFindViewByLocation():
     *   +0x04 x
     *   +0x08 y
     *   +0x0c width
     *   +0x10 height
     *   +0x14 bit 31 visible/participating
     *
     * These host members intentionally do not claim N32 ABI layout.
     */
    void portableSetPosition(long x, long y) { _portableX = x; _portableY = y; }
    long portableX() const { return _portableX; }
    long portableY() const { return _portableY; }
    void portableSetSize(long width, long height) {
        _portableWidth = width;
        _portableHeight = height;
    }
    long portableWidth() const { return _portableWidth; }
    long portableHeight() const { return _portableHeight; }
    void portableSetVisible(bool visible) { _portableVisible = visible; }
    bool portableVisible() const { return _portableVisible; }

private:
    long _portableX;
    long _portableY;
    long _portableWidth;
    long _portableHeight;
    bool _portableVisible;
};

class ImdIconView : public ImdView
{
public:
    ImdIconView();
    virtual ~ImdIconView();

    virtual void setFlag(unsigned int flag, bool value);
    virtual bool getFlag(unsigned int flag) const;
    virtual ImdString getLabel() const;
    virtual void setLabel(const ImdString &label);

    unsigned int portableFlags() const { return _portableFlags; }

    /*
     * RE host seam for the renderer-facing FTR resource while the native
     * ImdFtrGalleryX/ImdFtrIconX classes are being reconstructed. The selected
     * resource names are taken from the original SGI filetype database.
     */
    void portableSetIconResource(const ImdString &name) { _portableIconResource = name; }
    const ImdString &portableIconResource() const { return _portableIconResource; }

private:
    unsigned int _portableFlags;
    ImdString _portableLabel;
    ImdString _portableIconResource;
};

class ImdIconPanel;

extern WidgetClass imdViewPanelWidgetClass;

extern "C" Widget ImdCreateViewPanel(Widget, char *, Arg *, Cardinal);
extern "C" void ImdViewPanelAdoptRenderer(Widget, ImdRenderer *);
extern "C" void ImdViewPanelAdoptLayout(Widget, ImdLayout *);
extern "C" void ImdViewPanelSetScale(Widget, float, long, long);
extern "C" void ImdViewPanelBeginBlockChanges(Widget);
extern "C" void ImdViewPanelEndBlockChanges(Widget);
void ImdViewPanelAdoptView(Widget, ImdView *, ImdView *);
void ImdViewPanelAdoptView(Widget, ImdView *, ImdIconPanel *);
void ImdViewPanelRedraw(Widget);

/* Original exported ViewPanel geometry operations. */
ImdView *ImdViewPanelFindViewByLocation(Widget, long, long);
bool ImdViewPanelMoveView(Widget, ImdView &, long, long);

class ImdIconPanel
{
public:
    ImdIconPanel(const char *name, Widget parent, Arg *args, int count);
    virtual ~ImdIconPanel();
    Widget baseWidget() const { return _baseWidget; }
    virtual void adoptView(ImdIconView *view, ImdIconView *parentView);
    virtual void applySelectStrings(ImdIconView &view);
    const std::vector<ImdView *> &portableViews() const { return _views; }
    void portableResetViews(bool destroyViews);

private:
    Widget _baseWidget;
    std::vector<ImdView *> _views;
};

#endif /* IRIX_RE_LIBIMDICONPANEL_IMDICONPANEL_H */
