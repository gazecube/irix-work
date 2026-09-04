#ifndef IRIX_RE_CLOGIN_PANDORA_PRESENTATION_H
#define IRIX_RE_CLOGIN_PANDORA_PRESENTATION_H

#include "ImdIconPanel.h"

#include <X11/Intrinsic.h>
#include <sys/types.h>
#include <string>
#include <vector>

class UserView;

/*
 * Original clogin class names recovered from symbols.
 *
 * PandoraRenderer/PandoraLayout inheritance details are not fully recovered,
 * but the binary passes them to ImdViewPanelAdoptRenderer/Layout respectively.
 * The minimal Imd base classes therefore preserve that proven type direction
 * without assigning unproven virtual slots.
 */
class PandoraRenderer : public ImdRenderer
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

    Widget container() const { return _container; }
    void addView(UserView *view);
    void update();

private:
    Widget _panelWidget;
    Widget _container;
    std::vector<UserView *> _views;
};

class UserView : public ImdView
{
public:
    /*
     * Original constructor is UserView(const ImdString &, long, long).
     * std::string is a portable stand-in until reconstructed libimdUtil's
     * ImdString ABI is linkable from the probe.
     *
     * The binary-proven addUser() path also carries the account home path and
     * uid into the presentation object. Their exact relationship to the two
     * long constructor arguments is not proven, so they are retained as
     * explicit metadata rather than being forced into arg0/arg1.
     */
    UserView(const std::string &name, long arg0, long arg1);

    const std::string &name() const { return _name; }
    const std::string &home() const { return _home; }
    uid_t uid() const { return _uid; }
    bool hasAccountMetadata() const { return _hasAccountMetadata; }
    long arg0() const { return _arg0; }
    long arg1() const { return _arg1; }
    Widget widget() const { return _widget; }

    void setAccountMetadata(const std::string &home, uid_t uid);

    void createPortableWidget(
        Widget parent,
        XtCallbackProc activateCallback,
        XtPointer clientData);

private:
    std::string _name;
    std::string _home;
    uid_t _uid;
    bool _hasAccountMetadata;
    long _arg0;
    long _arg1;
    Widget _widget;
};

#endif /* IRIX_RE_CLOGIN_PANDORA_PRESENTATION_H */
