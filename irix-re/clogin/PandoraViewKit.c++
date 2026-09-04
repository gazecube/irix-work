#include "PandoraViewKit.h"
#include "ImdIconPanel.h"
#include "Base/PwdNameList.h"

#include <Xm/Form.h>
#include <Xm/Frame.h>
#include <Xm/Label.h>
#include <Xm/Text.h>
#include <Xm/PushB.h>
#include <Xm/RowColumn.h>
#include <Xm/ScrolledW.h>

#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstring>

static String pandoraDefaults[] =
{
    "*panelWindow.shadowThickness: 0",
    "*form.XmText.blinkRate: 0",
    "*helpButton.traversalOn: False",
    "*loginButton.traversalOn: False",
    NULL
};

namespace
{
bool pathPrefixMatch(const std::string &prefix, const char *path)
{
    if (!path || prefix.empty())
        return false;

    const std::size_t length = prefix.size();
    if (std::strncmp(path, prefix.c_str(), length) != 0)
        return false;

    return path[length] == '\0' || path[length] == '/';
}

bool homeIsIgnored(const char *home)
{
    const std::vector<std::string> &ignored = PwdNameList::getIgnoreList();
    for (std::size_t i = 0; i < ignored.size(); ++i)
    {
        if (pathPrefixMatch(ignored[i], home))
            return true;
    }
    return false;
}

bool homeIsDirectory(const char *home)
{
    if (!home || !*home)
        return false;

    struct stat st;
    return stat(home, &st) == 0 && S_ISDIR(st.st_mode);
}

bool linuxProbeInteractiveShell(const char *shell)
{
    if (!shell || !*shell)
        return false;

    const char *base = std::strrchr(shell, '/');
    base = base ? base + 1 : shell;
    return std::strcmp(base, "nologin") != 0 &&
           std::strcmp(base, "false") != 0;
}
}

PandoraComponent::PandoraComponent(const char *name, Widget parent)
    : VkComponent(name),
      _form(NULL),
      _panelWindow(NULL),
      _scrolledWindow(NULL),
      _iconPanel(NULL),
      _userIconContainer(NULL),
      _warningLabel(NULL),
      _userFullNameLabel(NULL),
      _userLabel(NULL),
      _userText(NULL),
      _passwordLabel(NULL),
      _passwordText(NULL),
      _hostLabel(NULL),
      _helpButton(NULL),
      _loginButton(NULL)
{
    Widget managed[6];

    setDefaultResources(parent, pandoraDefaults);

    /*
     * Binary-proven from PandoraComponentUI::PandoraComponentUI at
     * 0x10012bbc: the VkComponent base widget is an XmFrame, not the form.
     * The child form is created managed inside that frame.
     */
    _baseWidget = XtVaCreateWidget(
        name,
        xmFrameWidgetClass,
        parent,
        XmNshadowType, 8,
        XtNwidth, 846,
        XtNheight, 744,
        NULL);
    installDestroyHandler();

    _form = XtVaCreateManagedWidget(
        "form",
        xmFormWidgetClass,
        _baseWidget,
        XtNwidth, 844,
        XtNheight, 742,
        NULL);

    _passwordLabel = XtVaCreateWidget(
        "passwordLabel",
        xmLabelWidgetClass,
        _form,
        XtNmappedWhenManaged, False,
        XmNalignment, XmALIGNMENT_END,
        XmNrecomputeSize, True,
        XmNtopAttachment, XmATTACH_NONE,
        XmNbottomAttachment, XmATTACH_FORM,
        XmNleftAttachment, XmATTACH_FORM,
        XmNrightAttachment, XmATTACH_NONE,
        NULL);

    _userLabel = XtVaCreateWidget(
        "userLabel",
        xmLabelWidgetClass,
        _form,
        XmNalignment, XmALIGNMENT_END,
        XmNrecomputeSize, True,
        XmNtopAttachment, XmATTACH_NONE,
        XmNbottomAttachment, XmATTACH_WIDGET,
        XmNbottomWidget, _passwordLabel,
        XmNleftAttachment, XmATTACH_FORM,
        XmNrightAttachment, XmATTACH_NONE,
        NULL);

    _userText = XtVaCreateWidget(
        "userText",
        xmTextWidgetClass,
        _form,
        XmNeditMode, XmSINGLE_LINE_EDIT,
        XmNtopAttachment, XmATTACH_NONE,
        XmNbottomAttachment, XmATTACH_OPPOSITE_WIDGET,
        XmNbottomWidget, _userLabel,
        XmNleftAttachment, XmATTACH_WIDGET,
        XmNleftWidget, _userLabel,
        XmNrightAttachment, XmATTACH_NONE,
        NULL);

    _warningLabel = XtVaCreateWidget(
        "warningLabel",
        xmLabelWidgetClass,
        _form,
        XmNalignment, XmALIGNMENT_BEGINNING,
        XmNtopAttachment, XmATTACH_OPPOSITE_WIDGET,
        XmNtopWidget, _passwordLabel,
        XmNbottomAttachment, XmATTACH_NONE,
        XmNleftAttachment, XmATTACH_FORM,
        XmNrightAttachment, XmATTACH_NONE,
        NULL);

    _passwordText = XtVaCreateWidget(
        "passwordText",
        xmTextWidgetClass,
        _form,
        XmNuserData, (XtPointer)this,
        XmNeditMode, XmSINGLE_LINE_EDIT,
        XmNtopAttachment, XmATTACH_NONE,
        XmNbottomAttachment, XmATTACH_OPPOSITE_WIDGET,
        XmNbottomWidget, _passwordLabel,
        XmNleftAttachment, XmATTACH_OPPOSITE_WIDGET,
        XmNleftWidget, _userText,
        XmNrightAttachment, XmATTACH_NONE,
        NULL);

    _userFullNameLabel = XtVaCreateWidget(
        "userFullNameLabel",
        xmLabelWidgetClass,
        _form,
        XmNalignment, XmALIGNMENT_BEGINNING,
        XmNrecomputeSize, True,
        XmNtopAttachment, XmATTACH_NONE,
        XmNbottomAttachment, XmATTACH_OPPOSITE_WIDGET,
        XmNbottomWidget, _userLabel,
        XmNleftAttachment, XmATTACH_WIDGET,
        XmNleftWidget, _userText,
        XmNrightAttachment, XmATTACH_NONE,
        NULL);

    _panelWindow = XtVaCreateWidget(
        "panelWindow",
        xmFrameWidgetClass,
        _form,
        XmNtopAttachment, XmATTACH_FORM,
        XmNbottomAttachment, XmATTACH_WIDGET,
        XmNbottomWidget, _userLabel,
        XmNleftAttachment, XmATTACH_FORM,
        XmNrightAttachment, XmATTACH_FORM,
        NULL);

    _helpButton = XtVaCreateWidget(
        "helpButton",
        xmPushButtonWidgetClass,
        _form,
        XmNrecomputeSize, False,
        XmNtopAttachment, XmATTACH_NONE,
        XmNbottomAttachment, XmATTACH_FORM,
        XmNleftAttachment, XmATTACH_NONE,
        XmNrightAttachment, XmATTACH_FORM,
        NULL);

    _loginButton = XtVaCreateWidget(
        "loginButton",
        xmPushButtonWidgetClass,
        _form,
        XmNrecomputeSize, False,
        XmNtopAttachment, XmATTACH_NONE,
        XmNbottomAttachment, XmATTACH_FORM,
        XmNleftAttachment, XmATTACH_NONE,
        XmNrightAttachment, XmATTACH_WIDGET,
        XmNrightWidget, _helpButton,
        NULL);

    _hostLabel = XtVaCreateWidget(
        "hostLabel",
        xmLabelWidgetClass,
        _form,
        XmNalignment, XmALIGNMENT_BEGINNING,
        XmNrecomputeSize, True,
        XmNtopAttachment, XmATTACH_NONE,
        XmNbottomAttachment, XmATTACH_FORM,
        XmNleftAttachment, XmATTACH_FORM,
        XmNrightAttachment, XmATTACH_NONE,
        NULL);

    managed[0] = _loginButton;
    managed[1] = _helpButton;
    managed[2] = _panelWindow;
    managed[3] = _userText;
    managed[4] = _userLabel;
    managed[5] = _passwordLabel;
    XtManageChildren(managed, 6);

    /*
     * Binary-proven constructor callback: userText XmNactivateCallback.
     * The original callback enters the same login-name transition which can
     * reveal the password UI without first disclosing whether the name exists.
     */
    XtAddCallback(
        _userText,
        XmNactivateCallback,
        &PandoraComponent::userTextActivateCB,
        (XtPointer)this);

    createIconAreaScaffold();
}

void PandoraComponent::createIconAreaScaffold()
{
    Arg args[8];
    int count = 0;

    _scrolledWindow = XmCreateScrolledWindow(
        _panelWindow,
        (char *)"scrolledWindow",
        NULL,
        0);
    XtManageChild(_scrolledWindow);

    XtSetArg(args[count], (char *)"installColormap", False); ++count;
    XtSetArg(args[count], (char *)"ditherBackground", False); ++count;
    XtSetArg(args[count], (char *)"horzScrollPolicy", 0); ++count;
    XtSetArg(args[count], (char *)"vertScrollPolicy", 1); ++count;
    XtSetArg(args[count], (char *)"dropEnabled", 0); ++count;
    XtSetArg(args[count], (char *)"dragEnabled", 0); ++count;
    XtSetArg(args[count], (char *)"boxSelectEnabled", 0); ++count;
    XtSetArg(args[count], (char *)"renameEnabled", 0); ++count;

    _iconPanel = new ImdIconPanel(
        "panel",
        _scrolledWindow,
        args,
        count);

    if (_iconPanel && _iconPanel->baseWidget())
    {
        ImdViewPanelSetScale(_iconPanel->baseWidget(), 1.5f, 0, 0);
        XtManageChild(_iconPanel->baseWidget());
        displayIcon();
    }
}

void PandoraComponent::displayIcon()
{
    if (!_iconPanel || !_iconPanel->baseWidget())
        return;

    /*
     * The original displayIcon() at 0x10011b8c blocks Imd panel changes,
     * iterates the already-built user model, constructs UserView objects and
     * inserts them into the panel. The model/view classes are still being
     * reconstructed, so this probe keeps that function boundary but uses a
     * Motif RowColumn as the temporary Linux rendering backend.
     */
    _userIconContainer = XtVaCreateManagedWidget(
        "userViewContainer",
        xmRowColumnWidgetClass,
        _iconPanel->baseWidget(),
        XmNorientation, XmHORIZONTAL,
        XmNpacking, XmPACK_COLUMN,
        XmNnumColumns, 4,
        XmNspacing, 12,
        XmNmarginWidth, 16,
        XmNmarginHeight, 16,
        NULL);

    const char *configPath = "/var/sysadm/config/clogin.conf";
    const bool haveIrixConfig = access(configPath, R_OK) == 0;
    PwdNameList config(haveIrixConfig ? configPath : NULL);

    setpwent();
    for (struct passwd *pwd = getpwent(); pwd; pwd = getpwent())
    {
        if (!pwd->pw_name || !*pwd->pw_name)
            continue;
        if (!pwd->pw_dir || !*pwd->pw_dir)
            continue;
        if (pwd->pw_passwd && std::strcmp(pwd->pw_passwd, "*") == 0)
            continue;

        const PwdNameListEntry *entry = config.findEntry(pwd->pw_name);
        if (entry && (entry->flags & (PwdNameListEntry::kNoShow | PwdNameListEntry::kNoIcon)))
            continue;
        if (homeIsIgnored(pwd->pw_dir))
            continue;
        if (!homeIsDirectory(pwd->pw_dir))
            continue;

        /*
         * Reconstructed Linux probe fallback only. Stock Debian does not ship
         * SGI passwd.sgi/clogin metadata for service accounts. If the real
         * IRIX clogin.conf is absent, suppress nologin/false service accounts
         * rather than inventing a UID threshold, which the IRIX binary does
         * not use.
         */
        if (!haveIrixConfig && !linuxProbeInteractiveShell(pwd->pw_shell))
            continue;

        XmString label = XmStringCreateLocalized(pwd->pw_name);
        Widget user = XtVaCreateManagedWidget(
            pwd->pw_name,
            xmPushButtonWidgetClass,
            _userIconContainer,
            XmNlabelString, label,
            XmNuserData, (XtPointer)this,
            XmNwidth, 128,
            XmNheight, 96,
            XmNrecomputeSize, False,
            NULL);
        XmStringFree(label);

        XtAddCallback(
            user,
            XmNactivateCallback,
            &PandoraComponent::userIconActivateCB,
            (XtPointer)this);
    }
    endpwent();
}

void PandoraComponent::showPasswordPrompt()
{
    if (!_userText || !_passwordLabel || !_passwordText)
        return;

    char *name = XmTextGetString(_userText);
    const bool haveName = name && *name;
    if (name)
        XtFree(name);

    if (!haveName)
        return;

    /*
     * Binary-proven UI state: passwordLabel starts managed but with
     * mappedWhenManaged=False, while passwordText starts unmanaged. The
     * original select/login-name path later reveals the row. Keep that exact
     * initial-state distinction and perform the delayed transition here.
     */
    XtSetMappedWhenManaged(_passwordLabel, True);

    if (!XtIsManaged(_passwordText))
        XtManageChild(_passwordText);
    else
        XtSetMappedWhenManaged(_passwordText, True);

    XmProcessTraversal(_passwordText, XmTRAVERSE_CURRENT);
}

void PandoraComponent::userIconActivateCB(
    Widget widget,
    XtPointer clientData,
    XtPointer)
{
    PandoraComponent *self = static_cast<PandoraComponent *>(clientData);
    if (!self || !self->_userText)
        return;

    XmTextSetString(self->_userText, const_cast<char *>(XtName(widget)));
    self->showPasswordPrompt();
}

void PandoraComponent::userTextActivateCB(
    Widget,
    XtPointer clientData,
    XtPointer)
{
    PandoraComponent *self = static_cast<PandoraComponent *>(clientData);
    if (!self)
        return;

    /*
     * Do not gate this on getpwnam(). The recovered original login flow can
     * reveal the password field for an unknown typed name so account validity
     * is not disclosed before a password attempt.
     */
    self->showPasswordPrompt();
}

PandoraComponent::~PandoraComponent()
{
    delete _iconPanel;
    _iconPanel = NULL;
}

const char *PandoraComponent::className()
{
    return "PandoraComponent";
}

PandoraWindow::PandoraWindow(const char *name)
    : VkSimpleWindow(name), _component(NULL)
{
    _component = new PandoraComponent("pandora", mainWindowWidget());
    addView(_component);
}

PandoraWindow::~PandoraWindow()
{
    delete _component;
    _component = NULL;
}

const char *PandoraWindow::className()
{
    return "PandoraWindow";
}
