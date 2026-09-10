from pathlib import Path

root = Path('export/reconstruction')
h = root / 'PandoraViewKit.h'
c = root / 'PandoraViewKit.c++'

hs = h.read_text()
cs = c.read_text()

old = 'class UserView;\n\n/*\n * Reconstructed portable storage'
new = 'class UserView;\n\ntypedef void (*PandoraLoginHandoff)(\n    const char *username,\n    const char *password,\n    bool directLogin,\n    XtPointer clientData);\n\n/*\n * Reconstructed portable storage'
assert old in hs
hs = hs.replace(old, new, 1)

old = '    const std::string &directLoginUser() const { return _directLoginUser; }\n\nprivate:'
new = '''    const std::string &directLoginUser() const { return _directLoginUser; }\n\n    /* Host portability seam. The SGI control flow ends at LoginTheUser()/\n     * reportSuccess(); Linux PAM/session startup belongs outside that model. */\n    void setLoginHandoff(PandoraLoginHandoff callback, XtPointer clientData)\n    {\n        _loginHandoff = callback;\n        _loginHandoffData = clientData;\n    }\n\nprivate:'''
assert old in hs
hs = hs.replace(old, new, 1)

old = '    void enablePwdField(bool enable);\n    void doubleClicked(UserView *view);\n\n    static void userViewSelectionCB'
new = '''    void enablePwdField(bool enable);\n    void doubleClicked(UserView *view);\n    void doLogin();\n    void handoffLogin(const char *username, const char *password, bool directLogin);\n\n    static void userViewSelectionCB'''
assert old in hs
hs = hs.replace(old, new, 1)

old = '    static void userTextActivateCB(Widget, XtPointer, XtPointer);\n    static void passwordTextModifyVerifyCB(Widget, XtPointer, XtPointer);'
new = '''    static void userTextActivateCB(Widget, XtPointer, XtPointer);\n    static void passwordTextActivateCB(Widget, XtPointer, XtPointer);\n    static void loginButtonCB(Widget, XtPointer, XtPointer);\n    static void passwordTextModifyVerifyCB(Widget, XtPointer, XtPointer);'''
assert old in hs
hs = hs.replace(old, new, 1)

old = '    bool _directLoginPending;\n    std::string _directLoginUser;\n};'
new = '''    bool _directLoginPending;\n    std::string _directLoginUser;\n\n    PandoraLoginHandoff _loginHandoff;\n    XtPointer _loginHandoffData;\n};'''
assert old in hs
hs = hs.replace(old, new, 1)

old = '#include "Filesystem/DirectoryCheck.h"\n'
new = '#include "Filesystem/DirectoryCheck.h"\n#include "Login/LoginTheUserModel.h"\n'
assert old in cs
cs = cs.replace(old, new, 1)

old = '      _directLoginPending(false),\n      _directLoginUser()\n'
new = '      _directLoginPending(false),\n      _directLoginUser(),\n      _loginHandoff(NULL),\n      _loginHandoffData(NULL)\n'
assert old in cs
cs = cs.replace(old, new, 1)

old = '''    XtAddCallback(\n        _userText,\n        XmNactivateCallback,\n        &PandoraComponent::userTextActivateCB,\n        (XtPointer)this);\n\n    /*\n     * Binary-proven callback at 0x10010eec.'''
new = '''    XtAddCallback(\n        _userText,\n        XmNactivateCallback,\n        &PandoraComponent::userTextActivateCB,\n        (XtPointer)this);\n\n    /* Both original callbacks feed PandoraComponent::doLogin(). */\n    XtAddCallback(\n        _passwordText,\n        XmNactivateCallback,\n        &PandoraComponent::passwordTextActivateCB,\n        (XtPointer)this);\n    XtAddCallback(\n        _loginButton,\n        XmNactivateCallback,\n        &PandoraComponent::loginButtonCB,\n        (XtPointer)this);\n\n    /*\n     * Binary-proven callback at 0x10010eec.'''
assert old in cs
cs = cs.replace(old, new, 1)

old = '''    if (copiedPasswd.empty())\n    {\n        _directLoginPending = true;\n        _directLoginUser = view->name();\n        return;\n    }'''
new = '''    if (copiedPasswd.empty())\n    {\n        _directLoginPending = true;\n        _directLoginUser = view->name();\n        handoffLogin(view->name().c_str(), "", true);\n        return;\n    }'''
assert old in cs
cs = cs.replace(old, new, 1)

anchor = '''void PandoraComponent::userViewSelectionCB(\n'''
assert anchor in cs
insert = r'''void PandoraComponent::handoffLogin(
    const char *username,
    const char *password,
    bool directLogin)
{
    if (!username || !*username)
        return;

    /*
     * Portability boundary only. The original successful path is
     * LoginTheUser() -> setupEnvironment() -> reportSuccess(). Linux PAM and
     * session launching must be supplied by the host, not disguised as SGI
     * behavior in this class.
     */
    if (_loginHandoff)
        _loginHandoff(username, password ? password : "", directLogin,
                      _loginHandoffData);
}

void PandoraComponent::doLogin()
{
    if (!_userText)
        return;

    char *typedName = XmTextGetString(_userText);
    const std::string login = typedName ? typedName : "";
    if (typedName)
        XtFree(typedName);

    if (login.empty())
        return;

    struct passwd *pwd = getpwnam(login.c_str());

    /*
     * pandora::doLogin() deliberately does not reveal an unknown account on
     * the first submit. It enables the password field first and reaches the
     * generic failure path only after credentials have been supplied.
     */
    if (!pwd)
    {
        if (!_passwordFieldEnabled)
            enablePwdField(true);
        else if (_passwordLength != 0)
            handoffLogin(login.c_str(), _password, false);
        return;
    }

    const std::string copiedPasswd = pwd->pw_passwd ? pwd->pw_passwd : "";

    if (_passwordLength != 0)
    {
        _directLoginPending = false;
        _directLoginUser.clear();
        handoffLogin(login.c_str(), _password, false);
        return;
    }

    if (copiedPasswd.empty())
    {
        _directLoginPending = true;
        _directLoginUser = login;
        handoffLogin(login.c_str(), "", true);
        return;
    }

    if (!_passwordFieldEnabled)
        enablePwdField(true);
}

'''
cs = cs.replace(anchor, insert + anchor, 1)

old = '''void PandoraComponent::userTextActivateCB(\n    Widget,\n    XtPointer clientData,\n    XtPointer)\n{\n    PandoraComponent *self = static_cast<PandoraComponent *>(clientData);\n    if (!self)\n        return;\n\n    self->showPasswordPrompt();\n}\n\nvoid PandoraComponent::passwordTextModifyVerifyCB'''
new = '''void PandoraComponent::userTextActivateCB(\n    Widget,\n    XtPointer clientData,\n    XtPointer)\n{\n    PandoraComponent *self = static_cast<PandoraComponent *>(clientData);\n    if (!self)\n        return;\n\n    /* Username Enter follows the prompt-transition path. */\n    self->showPasswordPrompt();\n}\n\nvoid PandoraComponent::passwordTextActivateCB(\n    Widget, XtPointer clientData, XtPointer)\n{\n    PandoraComponent *self = static_cast<PandoraComponent *>(clientData);\n    if (self)\n        self->doLogin();\n}\n\nvoid PandoraComponent::loginButtonCB(\n    Widget, XtPointer clientData, XtPointer)\n{\n    PandoraComponent *self = static_cast<PandoraComponent *>(clientData);\n    if (self)\n        self->doLogin();\n}\n\nvoid PandoraComponent::passwordTextModifyVerifyCB'''
assert old in cs
cs = cs.replace(old, new, 1)

h.write_text(hs)
c.write_text(cs)
