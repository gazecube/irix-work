from pathlib import Path

root = Path('export')
h = root / 'reconstruction/PandoraViewKit.h'
c = root / 'reconstruction/PandoraViewKit.c++'

hs = h.read_text()
cs = c.read_text()

hs = hs.replace(
'''    const char *password() const { return _password; }\n''',
'''    const char *password() const { return _password; }\n    bool passwordFieldEnabled() const { return _passwordFieldEnabled; }\n    bool directLoginPending() const { return _directLoginPending; }\n    const std::string &directLoginUser() const { return _directLoginUser; }\n''')

hs = hs.replace(
'''    void displayIcon();\n    void showPasswordPrompt();\n''',
'''    void displayIcon();\n    void showPasswordPrompt();\n    void enablePwdField(bool enable);\n    void doubleClicked(UserView *view);\n''')

hs = hs.replace(
'''    int _passwordLength;\n    char _password[22];\n''',
'''    int _passwordLength;\n    char _password[22];\n\n    /*\n     * Host-side storage for binary-proven pandora state. The original\n     * pandora::enablePwdField(int) stores its argument as a byte at pandora\n     * +0x00 after forwarding to PandoraComponent::enablePwdField(int).\n     * This portable class does not claim the original pandora ABI layout.\n     */\n    bool _passwordFieldEnabled;\n\n    /*\n     * Portability seam at the recovered LoginTheUser() boundary. The shipped\n     * binary calls LoginTheUser immediately when the copied passwd field is\n     * empty. Authentication/session handoff is not reconstructed in this C++\n     * path yet, so retain that decision as explicit, testable state rather\n     * than inventing a replacement backend.\n     */\n    bool _directLoginPending;\n    std::string _directLoginUser;\n''')

cs = cs.replace(
'''      _loginButton(NULL),\n      _passwordLength(0)\n''',
'''      _loginButton(NULL),\n      _passwordLength(0),\n      _passwordFieldEnabled(false),\n      _directLoginPending(false),\n      _directLoginUser()\n''')

old_show = '''void PandoraComponent::showPasswordPrompt()\n{\n    if (!_userText || !_passwordLabel || !_passwordText)\n        return;\n\n    char *name = XmTextGetString(_userText);\n    const bool haveName = name && *name;\n    if (name)\n        XtFree(name);\n\n    if (!haveName)\n        return;\n\n    XtSetMappedWhenManaged(_passwordLabel, True);\n\n    if (!XtIsManaged(_passwordText))\n        XtManageChild(_passwordText);\n    else\n        XtSetMappedWhenManaged(_passwordText, True);\n\n    XmProcessTraversal(_passwordText, XmTRAVERSE_CURRENT);\n}\n'''
new_show = '''void PandoraComponent::enablePwdField(bool enable)\n{\n    if (!_passwordLabel || !_passwordText)\n        return;\n\n    /*\n     * Binary anchor: pandora::enablePwdField(int) @ 0x10014e84 forwards the\n     * requested state to PandoraComponent::enablePwdField(int), then stores\n     * that state at pandora +0x00. This is the portable UI/state equivalent.\n     */\n    _passwordFieldEnabled = enable;\n    XtSetMappedWhenManaged(_passwordLabel, enable ? True : False);\n\n    if (enable)\n    {\n        if (!XtIsManaged(_passwordText))\n            XtManageChild(_passwordText);\n        else\n            XtSetMappedWhenManaged(_passwordText, True);\n        XmProcessTraversal(_passwordText, XmTRAVERSE_CURRENT);\n    }\n    else\n    {\n        XtSetMappedWhenManaged(_passwordText, False);\n    }\n}\n\nvoid PandoraComponent::showPasswordPrompt()\n{\n    if (!_userText)\n        return;\n\n    char *name = XmTextGetString(_userText);\n    const bool haveName = name && *name;\n    if (name)\n        XtFree(name);\n\n    if (haveName)\n        enablePwdField(true);\n}\n\nvoid PandoraComponent::doubleClicked(UserView *view)\n{\n    if (!view)\n        return;\n\n    /*\n     * Binary-proven pandora::doubleClicked(char *, char *) @ 0x10014704:\n     *   - resolve the selected login with getpwnam()\n     *   - copy the returned passwd record with pwd0_copyPwd()\n     *   - strlen(copy->pw_passwd) decides the branch\n     *   - empty passwd -> LoginTheUser(copy, NULL)\n     *   - non-empty passwd and byte +0 already set -> return\n     *   - non-empty passwd and byte +0 clear -> enablePwdField(1)\n     *\n     * libc passwd storage is transient. std::string is the portable stable\n     * copy for the one field needed by this recovered decision.\n     */\n    struct passwd *pwd = getpwnam(view->name().c_str());\n    if (!pwd)\n        return;\n\n    const std::string copiedPasswd = pwd->pw_passwd ? pwd->pw_passwd : "";\n\n    if (copiedPasswd.empty())\n    {\n        _directLoginPending = true;\n        _directLoginUser = view->name();\n        return;\n    }\n\n    _directLoginPending = false;\n    _directLoginUser.clear();\n\n    if (_passwordFieldEnabled)\n        return;\n\n    enablePwdField(true);\n}\n'''
if old_show not in cs:
    raise SystemExit('showPasswordPrompt block not found')
cs = cs.replace(old_show, new_show)

old_cmd = '''void PandoraComponent::userViewCommandCB(\n    Widget,\n    XtPointer clientData,\n    XtPointer callData)\n{\n    PandoraComponent *self = static_cast<PandoraComponent *>(clientData);\n    UserView *view = static_cast<UserView *>(callData);\n    if (!self || !view)\n        return;\n\n    /*\n     * Binary-proven commandCB resolves the command view's login and passes it\n     * to pandora::doubleClicked(). That state machine is the next RE target.\n     * Preserve the separate command boundary now; the portable action remains\n     * limited to keeping the selected login/prompt active until doubleClicked\n     * is reconstructed from the binary.\n     */\n    if (self->_userText)\n        XmTextSetString(\n            self->_userText, const_cast<char *>(view->name().c_str()));\n    self->showPasswordPrompt();\n}\n'''
new_cmd = '''void PandoraComponent::userViewCommandCB(\n    Widget,\n    XtPointer clientData,\n    XtPointer callData)\n{\n    PandoraComponent *self = static_cast<PandoraComponent *>(clientData);\n    UserView *view = static_cast<UserView *>(callData);\n    if (!self || !view)\n        return;\n\n    /*\n     * Binary-proven commandCB @ 0x10011090 resolves the command UserView login\n     * and forwards it to pandora::doubleClicked(). Keep that command boundary\n     * separate from selectCB; do not synthesize another selection action here.\n     */\n    self->doubleClicked(view);\n}\n'''
if old_cmd not in cs:
    raise SystemExit('command callback block not found')
cs = cs.replace(old_cmd, new_cmd)

h.write_text(hs)
c.write_text(cs)
