#ifndef IRIX_RE_CLOGIN_PANDORA_PRESENTATION_H
#define IRIX_RE_CLOGIN_PANDORA_PRESENTATION_H

#include "imd/ImdIconPanel.h"

#include <X11/Intrinsic.h>
#include <Xm/Xm.h>
#include <sys/types.h>
#include <string>
#include <vector>

class UserView;

class PandoraRenderer : public ImdInvertedTeeRenderer
{
public:
    PandoraRenderer();

    void setGallery(bool gallery);
    void setScale(float scale);

    bool gallery() const { return _gallery; }
    float scale() const { return _scale; }

private:
    bool _gallery;
    float _scale;
};

class PandoraLayout : public ImdLayout
{
public:
    explicit PandoraLayout(Widget panelWidget);
    ~PandoraLayout();

    Widget container() const { return _container; }
    void addView(UserView *view);
    void update();
    void resize();
    void setSelectionCallback(XtCallbackProc callback, XtPointer clientData);
    void setCommandCallback(XtCallbackProc callback, XtPointer clientData);

private:
    static void panelStructureEvent(
        Widget widget,
        XtPointer clientData,
        XEvent *event,
        Boolean *continueToDispatch);
    static void panelButtonEvent(
        Widget widget,
        XtPointer clientData,
        XEvent *event,
        Boolean *continueToDispatch);
    static void panelExposeCallback(
        Widget widget,
        XtPointer clientData,
        XtPointer callData);

    void draw();
    UserView *viewAt(Position x, Position y) const;
    XmFontList fontList() const;

    Widget _panelWidget;
    Widget _container;
    Widget _fontProbe;
    XtCallbackProc _selectionCallback;
    XtPointer _selectionClientData;
    XtCallbackProc _commandCallback;
    XtPointer _commandClientData;
    UserView *_selectedView;
    Time _lastClickTime;
    std::vector<UserView *> _views;
};

class UserView : public ImdIconView
{
public:
    /*
     * Binary-proven original constructor payload:
     *
     *   UserView(const ImdString &login, long uid, const ImdString &home)
     *
     * The old SGI mangling is:
     *   __ct__8UserViewGRC9ImdStringlT1
     */
    UserView(const std::string &name, long uid, const std::string &home);
    UserView(const std::string &name, long legacyArg0, long legacyArg1);

    const std::string &name() const { return _name; }
    const std::string &home() const { return _home; }
    uid_t uid() const { return _uid; }

    Position x() const { return _x; }
    Position y() const { return _y; }
    Dimension width() const { return _width; }
    Dimension height() const { return _height; }

    void preparePortableGeometry(XmFontList fontList);
    void setPortablePosition(Position x, Position y);

    /*
     * Portable counterpart of ImdIconRenderer::intersects(). The original
     * checks icon and label regions separately instead of accepting the whole
     * ImdView bounding rectangle.
     */
    bool intersectsPortable(Position x, Position y, XmFontList fontList) const;

    void drawPortableContents(Widget panelWidget, XmFontList fontList) const;

private:
    std::string _name;
    std::string _home;
    uid_t _uid;
    Position _x;
    Position _y;
    Dimension _width;
    Dimension _height;
};

#endif /* IRIX_RE_CLOGIN_PANDORA_PRESENTATION_H */