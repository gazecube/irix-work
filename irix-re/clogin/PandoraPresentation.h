#ifndef IRIX_RE_CLOGIN_PANDORA_PRESENTATION_H
#define IRIX_RE_CLOGIN_PANDORA_PRESENTATION_H

#include "ImdIconPanel.h"

#include <X11/Intrinsic.h>
#include <sys/types.h>
#include <string>
#include <vector>

class UserView;

/*
 * Original direct base-constructor chains recovered from the fresh IRIX
 * /usr/Cadmin/bin/clogin binary:
 *
 *   PandoraRenderer::PandoraRenderer()
 *     -> ImdInvertedTeeRenderer::ImdInvertedTeeRenderer()
 *
 *   PandoraLayout::PandoraLayout(Widget)
 *     -> ImdLayout::ImdLayout(Widget)
 *
 *   UserView::UserView(...)
 *     -> ImdIconView::ImdIconView()
 *
 * The portable base types do not yet reproduce the complete original vtables;
 * they preserve the recovered class direction while the renderer ABI is
 * reconstructed incrementally.
 */
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

private:
    static void panelStructureEvent(
        Widget widget,
        XtPointer clientData,
        XEvent *event,
        Boolean *continueToDispatch);

    Widget _panelWidget;
    Widget _container;
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
     *
     * displayIcon() passes the model's login ImdString, uid field, and home
     * ImdString directly. std::string remains the portable stand-in until the
     * reconstructed ImdString ABI is linked into this probe.
     */
    UserView(const std::string &name, long uid, const std::string &home);

    /*
     * Transitional source-compatibility constructor for the current probe.
     * It resolves the binary-proven uid/home payload from passwd by name so
     * older call sites keep working until displayIcon() is updated directly.
     */
    UserView(const std::string &name, long legacyArg0, long legacyArg1);

    const std::string &name() const { return _name; }
    const std::string &home() const { return _home; }
    uid_t uid() const { return _uid; }
    Widget widget() const { return _widget; }

    void createPortableWidget(
        Widget parent,
        XtCallbackProc activateCallback,
        XtPointer clientData);

private:
    std::string _name;
    std::string _home;
    uid_t _uid;
    Widget _widget;
};

#endif /* IRIX_RE_CLOGIN_PANDORA_PRESENTATION_H */