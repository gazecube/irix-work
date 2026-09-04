#ifndef IRIX_RE_CLOGIN_PANDORA_PRESENTATION_H
#define IRIX_RE_CLOGIN_PANDORA_PRESENTATION_H

#include <X11/Intrinsic.h>
#include <string>
#include <vector>

class UserView;

/*
 * Original clogin class names recovered from symbols.
 *
 * Their complete inheritance/vtable ABI is not reconstructed yet. These
 * portable classes preserve the original object boundaries while keeping the
 * Linux Motif backing implementation private and explicitly provisional.
 */
class PandoraRenderer
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

class PandoraLayout
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

class UserView
{
public:
    /*
     * Original constructor is UserView(const ImdString &, long, long).
     * std::string is a portable stand-in until reconstructed libimdUtil's
     * ImdString ABI is linkable from the probe.
     */
    UserView(const std::string &name, long arg0, long arg1);

    const std::string &name() const { return _name; }
    long arg0() const { return _arg0; }
    long arg1() const { return _arg1; }
    Widget widget() const { return _widget; }

    void createPortableWidget(
        Widget parent,
        XtCallbackProc activateCallback,
        XtPointer clientData);

private:
    std::string _name;
    long _arg0;
    long _arg1;
    Widget _widget;
};

#endif /* IRIX_RE_CLOGIN_PANDORA_PRESENTATION_H */
