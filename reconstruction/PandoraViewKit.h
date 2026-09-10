#ifndef IRIX_RE_CLOGIN_PANDORA_VIEWKIT_H
#define IRIX_RE_CLOGIN_PANDORA_VIEWKIT_H

#include <Vk/VkComponent.h>
#include <Vk/VkSimpleWindow.h>
#include <sys/types.h>
#include <string>
#include <vector>

class ImdIconPanel;
class PandoraRenderer;
class PandoraLayout;
class UserView;

/*
 * Reconstructed portable storage for the binary-proven per-user model
 * collection held by PandoraComponent before displayIcon() builds UserView
 * instances. The original allocation is 0x1c bytes, but its concrete class
 * name is not exported by clogin, so do not treat this name/layout as ABI.
 */
struct PandoraUserModel
{
    std::string login;
    std::string fullName;
    std::string home;
    uid_t uid;
};

class PandoraComponent : public VkComponent
{
public:
    PandoraComponent(const char *name, Widget parent);
    virtual ~PandoraComponent();

    virtual const char *className();

    Widget panelWindow() const { return _panelWindow; }
    Widget scrolledWindow() const { return _scrolledWindow; }
    Widget userText() const { return _userText; }
    Widget passwordText() const { return _passwordText; }
    const char *password() const { return _password; }

private:
    void createIconList();
    void addUser(const struct passwd *pwd, const char *loginOverride = NULL);
    void createIconAreaScaffold();
    void displayIcon();
    void showPasswordPrompt();

    static void userViewActivateCB(Widget, XtPointer, XtPointer);
    static void userTextActivateCB(Widget, XtPointer, XtPointer);
    static void passwordTextModifyVerifyCB(Widget, XtPointer, XtPointer);

    Widget _form;
    Widget _panelWindow;
    Widget _scrolledWindow;
    ImdIconPanel *_iconPanel;
    PandoraRenderer *_renderer;
    PandoraLayout *_layout;
    std::vector<PandoraUserModel> _users;
    std::vector<UserView *> _userViews;
    Widget _warningLabel;
    Widget _userFullNameLabel;
    Widget _userLabel;
    Widget _userText;
    Widget _passwordLabel;
    Widget _passwordText;
    Widget _hostLabel;
    Widget _helpButton;
    Widget _loginButton;

    /*
     * Reconstructed storage for binary-proven PandoraComponent fields.
     * Original clogin stores password length at +0x5c and password bytes
     * starting at +0x6c. The recovered modify-verify callback caps stored
     * input at 21 bytes and keeps it separate from the displayed XmText data.
     */
    int _passwordLength;
    char _password[22];
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
