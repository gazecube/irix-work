#include "PandoraViewKit.h"
#include "PandoraPresentation.h"
#include "ImdIconPanel.h"
#include "ImdFtrVisualCompat.h"
#include "Base/PwdNameList.h"

#include <Xm/Form.h>
#include <Xm/Frame.h>
#include <Xm/Label.h>
#include <Xm/Text.h>
#include <Xm/PushB.h>
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
      _renderer(NULL),
      _layout(NULL),
      _warningLabel(NULL),
      _userFullNameLabel(NULL),
      _userLabel(NULL),
      _userText(NULL),
      _passwordLabel(NULL),
      _passwordText(NULL),
      _hostLabel(NULL),
      _helpButton(NULL),
      _loginButton(NULL),
      _passwordLength(0)
{
    Widget managed[6];
    std::memset(_password, 0, sizeof(_password));

    setDefaultResources(parent, pandoraDefaults);

    /*
     * Binary-proven from PandoraComponentUI::PandoraComponentUI at
     * 0x10012bbc: the VkComponent base widget is an XmFrame, not the form.
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

    XtAddCallback(
        _userText,
        XmNactivateCallback,
        &PandoraComponent::userTextActivateCB,
        (XtPointer)this);

    /*
     * Binary-proven callback at 0x10010eec. The real password is stored in
     * component-owned memory while inserted display bytes become ASCII space.
     */
    XtAddCallback(
        _passwordText,
        XmNmodifyVerifyCallback,
        &PandoraComponent::passwordTextModifyVerifyCB,
        (XtPointer)this);

    createIconAreaScaffold();
}

void PandoraComponent::createIconAreaScaffold()
{
    Arg args[16];
    Cardinal count = 0;
    int auxVisualValue = 0;
    Boolean haveChosenVisual;

    _scrolledWindow = XmCreateScrolledWindow(
        _panelWindow,
        (char *)"scrolledWindow",
        NULL,
        0);
    XtManageChild(_scrolledWindow);

    /*
     * Binary-proven from PandoraComponent::readyToDisplay at 0x100115e8:
     * immediately after managing scrolledWindow, Clogin calls
     * imdFtrChooseVisualArgs(baseWidget, 2, args, &count, &aux). On success
     * that routine appends visual, colormap, and depth in exactly that order.
     * The returned Boolean is then used as the value for BOTH of the first
     * two Imd panel resources: installColormap and ditherBackground.
     */
    haveChosenVisual = imdFtrChooseVisualArgs(
        _baseWidget,
        2,
        args,
        &count,
        &auxVisualValue);

    XtSetArg(args[count], (char *)"installColormap", haveChosenVisual); ++count;
    XtSetArg(args[count], (char *)"ditherBackground", haveChosenVisual); ++count;
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
        static_cast<int>(count));

    if (_iconPanel && _iconPanel->baseWidget())
    {
        Widget panelWidget = _iconPanel->baseWidget();

        /*
         * Binary-proven readyToDisplay() order: construct PandoraRenderer,
         * adopt renderer, construct PandoraLayout(panelWidget), adopt layout,
         * then apply the 1.5 panel scale.
         */
        _renderer = new PandoraRenderer();
        ImdViewPanelAdoptRenderer(panelWidget, _renderer);

        const char *configPath = "/var/sysadm/config/clogin.conf";
        const bool haveIrixConfig = access(configPath, R_OK) == 0;
        PwdNameList config(haveIrixConfig ? configPath : NULL);

        if (config.displayFaces())
            _renderer->setGallery(true);

        _layout = new PandoraLayout(panelWidget);
        _layout->setActivateCallback(
            &PandoraComponent::userViewActivateCB,
            (XtPointer)this);
        ImdViewPanelAdoptLayout(panelWidget, _layout);

        ImdViewPanelSetScale(panelWidget, 1.5f, 0, 0);
        _renderer->setScale(2.0f);

        XtManageChild(panelWidget);
        displayIcon();
    }
}

void PandoraComponent::displayIcon()
{
    if (!_iconPanel || !_iconPanel->baseWidget() || !_layout)
        return;

    Widget panelWidget = _iconPanel->baseWidget();
    ImdViewPanelBeginBlockChanges(panelWidget);

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
        if (entry && (entry->flags &
            (PwdNameListEntry::kNoShow | PwdNameListEntry::kNoIcon)))
            continue;
        if (homeIsIgnored(pwd->pw_dir))
            continue;
        if (!homeIsDirectory(pwd->pw_dir))
            continue;

        if (!haveIrixConfig && !linuxProbeInteractiveShell(pwd->pw_shell))
            continue;

        /*
         * Binary-proven UserView constructor payload from displayIcon():
         * (login ImdString, uid, home ImdString).
         */
        UserView *view = new UserView(
            pwd->pw_name,
            static_cast<long>(pwd->pw_uid),
            pwd->pw_dir);

        /*
         * Original displayIcon() adopts Imd views into the panel; it does not
         * create one Xt widget per account. The portable probe now follows the
         * same ownership shape. PandoraLayout stores reconstructed geometry
         * until the full ImdView fields/layout ABI are wired.
         */
        _layout->addView(view);
        _iconPanel->adoptView(view);
        _userViews.push_back(view);
    }
    endpwent();

    _layout->update();
    ImdViewPanelEndBlockChanges(panelWidget);
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

    XtSetMappedWhenManaged(_passwordLabel, True);

    if (!XtIsManaged(_passwordText))
        XtManageChild(_passwordText);
    else
        XtSetMappedWhenManaged(_passwordText, True);

    XmProcessTraversal(_passwordText, XmTRAVERSE_CURRENT);
}

void PandoraComponent::userViewActivateCB(
    Widget,
    XtPointer clientData,
    XtPointer callData)
{
    PandoraComponent *self = static_cast<PandoraComponent *>(clientData);
    UserView *view = static_cast<UserView *>(callData);
    if (!self || !self->_userText || !view)
        return;

    /*
     * The original Clogin selectCB obtains the selected ImdView from
     * getSelectedViewList(), extracts the login string, fills the XmText, and
     * reveals the password prompt. The portable panel callback receives the
     * already hit-tested UserView directly until the selected-view list ABI is
     * reconstructed.
     */
    XmTextSetString(
        self->_userText,
        const_cast<char *>(view->name().c_str()));
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

    self->showPasswordPrompt();
}

void PandoraComponent::passwordTextModifyVerifyCB(
    Widget,
    XtPointer clientData,
    XtPointer callData)
{
    PandoraComponent *self = static_cast<PandoraComponent *>(clientData);
    XmTextVerifyCallbackStruct *verify =
        static_cast<XmTextVerifyCallbackStruct *>(callData);

    if (!self || !verify || !verify->text || !verify->text->ptr)
    {
        if (verify)
            verify->doit = False;
        return;
    }

    if (verify->text->length >= 2)
    {
        verify->doit = False;
        return;
    }

    if (verify->text->length == 0)
    {
        verify->doit = True;
        if (self->_passwordLength > 0)
            --self->_passwordLength;
        self->_password[self->_passwordLength] = '\0';
        return;
    }

    if (self->_passwordLength < 21)
    {
        self->_password[self->_passwordLength] = verify->text->ptr[0];
        ++self->_passwordLength;
        self->_password[self->_passwordLength] = '\0';
    }

    verify->text->ptr[0] = ' ';
    verify->doit = True;
}

PandoraComponent::~PandoraComponent()
{
    for (std::size_t i = 0; i < _userViews.size(); ++i)
        delete _userViews[i];
    _userViews.clear();

    delete _layout;
    _layout = NULL;

    delete _renderer;
    _renderer = NULL;

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
