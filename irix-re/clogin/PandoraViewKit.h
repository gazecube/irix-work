#ifndef IRIX_RE_CLOGIN_PANDORA_VIEWKIT_H
#define IRIX_RE_CLOGIN_PANDORA_VIEWKIT_H

#include <Vk/VkComponent.h>
#include <Vk/VkSimpleWindow.h>

class PandoraComponent : public VkComponent
{
public:
    PandoraComponent(const char *name, Widget parent);
    virtual ~PandoraComponent();

    virtual const char *className();

    Widget panelWindow() const { return _panelWindow; }
    Widget userText() const { return _userText; }
    Widget passwordText() const { return _passwordText; }

private:
    Widget _panelWindow;
    Widget _warningLabel;
    Widget _userFullNameLabel;
    Widget _userLabel;
    Widget _userText;
    Widget _passwordLabel;
    Widget _passwordText;
    Widget _hostLabel;
    Widget _helpButton;
    Widget _loginButton;
};

class PandoraWindow : public VkSimpleWindow
{
public:
    PandoraWindow(const char *name);
    virtual ~PandoraWindow();

    virtual const char *className();

    PandoraComponent *component() const { return _component; }

private:
    PandoraComponent *_component;
};

#endif /* IRIX_RE_CLOGIN_PANDORA_VIEWKIT_H */
