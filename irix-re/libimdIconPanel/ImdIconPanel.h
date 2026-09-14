#ifndef IRIX_RE_LIBIMDICONPANEL_IMDICONPANEL_H
#define IRIX_RE_LIBIMDICONPANEL_IMDICONPANEL_H

#include "ImdString.h"

#include <X11/Intrinsic.h>
#include <X11/Xutil.h>
#include <vector>

class ImdView;
class ImdIconPanel;
class ImdDragManager;

class ImdRenderer
{
public:
    virtual ~ImdRenderer() {}
    virtual void realize(Widget) {}
    virtual void draw(ImdView &, long, long) {}
};

class ImdIconRenderer : public ImdRenderer
{
public:
    ImdIconRenderer();
    virtual ~ImdIconRenderer();
    virtual void realize(Widget widget);
    virtual void draw(ImdView &, long, long) {}

protected:
    Widget _portableWidget;
};

class ImdInvertedTeeRenderer : public ImdIconRenderer
{
public:
    ImdInvertedTeeRenderer();
    virtual ~ImdInvertedTeeRenderer();
    virtual void realize(Widget widget);
    virtual void draw(ImdView &view, long x, long y);

    /*
     * Host drag-composition seam. Original ImdDragManager::drawDraggedItems()
     * renders the selected view through a drag renderer rather than copying a
     * rectangle out of the live ViewPanel. This helper keeps the portable
     * renderer on that architecture by allowing the same view renderer to
     * target the drag pixmap directly.
     */
    bool portableDrawToDrawable(ImdView &view, Drawable drawable,
                                long x, long y);
};

class ImdLayout
{
public:
    explicit ImdLayout(Widget widget = 0) : _widget(widget) {}
    virtual ~ImdLayout() {}

    /*
     * Original exported symbol:
     *   moveView__9ImdLayoutGR7ImdViewlT2
     *
     * The original base implementation is an eight-byte stub. Derived icon
     * layouts own the actual move operation.
     */
    virtual bool moveView(ImdView &, long, long) { return false; }

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
     * RE host seam for the renderer-facing FTR content while the native
     * ImdFtrGalleryX/ImdFtrIconX classes are being reconstructed.
     *
     * FTR ICON rules are composited from multiple FTI includes, so retain the
     * ordered resource list. The singular accessors remain for source
     * compatibility with earlier reconstruction code.
     */
    void portableSetIconResource(const ImdString &name) {
        _portableIconResource = name;
        _portableIconResources.clear();
        if (!name.empty())
            _portableIconResources.push_back(name);
    }
    const ImdString &portableIconResource() const { return _portableIconResource; }

    void portableSetIconResources(const std::vector<ImdString> &names) {
        _portableIconResources = names;
        _portableIconResource = names.empty() ? ImdString() : names.front();
    }
    const std::vector<ImdString> &portableIconResources() const {
        return _portableIconResources;
    }

private:
    unsigned int _portableFlags;
    ImdString _portableLabel;
    ImdString _portableIconResource;
    std::vector<ImdString> _portableIconResources;
};

extern WidgetClass imdViewPanelWidgetClass;

extern "C" Widget ImdCreateViewPanel(Widget, char *, Arg *, Cardinal);
extern "C" void ImdViewPanelAdoptRenderer(Widget, ImdRenderer *);
extern "C" void ImdViewPanelAdoptLayout(Widget, ImdLayout *);
extern "C" void ImdViewPanelSetScale(Widget, float, long, long);
extern "C" void ImdViewPanelBeginBlockChanges(Widget);
extern "C" void ImdViewPanelEndBlockChanges(Widget);
extern "C" ImdLayout *ImdViewPanelGetLayout(Widget);
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

    /*
     * Original binary signature has the parentView argument. A null parent is
     * the root-adoption case. Keep a source-level default so older reconstructed
     * callers that used adoptView(view) continue to mean adoptView(view, 0)
     * without changing the recovered ABI/signature.
     */
    virtual void adoptView(ImdIconView *view, ImdIconView *parentView = 0);
    virtual void applySelectStrings(ImdIconView &view);

    /* Binary-proven ImdIconPanel selection surface. */
    virtual void select(ImdIconView *view);
    virtual void extendSelect(ImdIconView *view);
    virtual void deselect();
    virtual void toggleSelection(ImdIconView *view);
    virtual void selectNotify();
    void enableMultipleSelect(bool enabled);
    void beginSelectBlock();
    void endSelectBlock();

    /*
     * Host-only inspection seam. The original N32 getSelected()/
     * getSelectedViewList() return the SGI collection at panel +0x20; this
     * vector does not claim ABI equivalence.
     */
    const std::vector<ImdIconView *> &portableSelectedViews() const { return _portableSelectedViews; }

    /*
     * Original exported drag surface. The base dragStart() delegates to the
     * ImdDragManager stored by ImdIconPanel. The derived model-based panel
     * overrides dragStart()/dragEnd() in the original library.
     */
    virtual void dragStart(int, long, long, XEvent *);
    virtual bool dragMotion(ImdView *);
    virtual void dragEnd();

    const std::vector<ImdView *> &portableViews() const { return _views; }
    void portableResetViews(bool destroyViews);

private:
    Widget _baseWidget;
    ImdDragManager *_dragManager;
    std::vector<ImdView *> _views;

    /* Host storage for the binary-proven selection collection/state. */
    std::vector<ImdIconView *> _portableSelectedViews;
    bool _portableMultipleSelect;
    unsigned int _portableSelectBlockDepth;
    bool _portableSelectionDirty;
};

#endif /* IRIX_RE_LIBIMDICONPANEL_IMDICONPANEL_H */
