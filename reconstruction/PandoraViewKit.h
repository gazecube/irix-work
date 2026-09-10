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

typedef void (*PandoraLoginHandoff)(
    const char *username,
    const char *password,
    bool directLogin,
    XtPointer clientData);

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
    bool passwordFieldEnabled() const { return _passwordFieldEnabled; }
    bool directLoginPending() const { return _directLoginPending; }
    const std::string &directLoginUser() const { return _directLoginUser; }

    /* Host portability seam. The SGI control flow ends at LoginTheUser()/
     * reportSuccess(); Linux PAM/session startup belongs outside that model. */
    void setLoginHandoff(PandoraLoginHandoff callback, XtPointer clientData)
    {
        _loginHandoff = callback;
        _loginHandoffData = clientData;
    }

private:
    void createIconList();
    void addUser(const struct passwd *pwd, const char *loginOverride = NULL);
    void createIconAreaScaffold();
    void displayIcon();
    void showPasswordPrompt();
    void enablePwdField(bool enable);
    void doubleClicked(UserView *view);
    void doLogin();
    void handoffLogin(const char *username, const char *password, bool directLogin);

    static void userViewSelectionCB(Widget, XtPointer, XtPointer);
    static void userViewCommandCB(Widget, XtPointer, XtPointer);
    static void userTextActivateCB(Widget, XtPointer, XtPointer);
    static void passwordTextActivateCB(Widget, XtPointer, XtPointer);
    static void loginButtonCB(Widget, XtPointer, XtPointer);
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

    /*
     * Host-side storage for binary-proven pandora state. The original
     * pandora::enablePwdField(int) stores its argument as a byte at pandora
     * +0x00 after forwarding to PandoraComponent::enablePwdField(int).
     * This portable class does not claim the original pandora ABI layout.
     */
    bool _passwordFieldEnabled;

    /*
     * Portability seam at the recovered LoginTheUser() boundary. The shipped
     * binary calls LoginTheUser immediately when the copied passwd field is
     * empty. Authentication/session handoff is not reconstructed in this C++
     * path yet, so retain that decision as explicit, testable state rather
     * than inventing a replacement backend.
     */
    bool _directLoginPending;
    std::string _directLoginUser;

    PandoraLoginHandoff _loginHandoff;
    XtPointer _loginHandoffData;
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
