#include "PandoraViewKit.h"

#include <Xm/Form.h>
#include <Xm/Frame.h>
#include <Xm/Label.h>
#include <Xm/Text.h>
#include <Xm/PushB.h>

static String pandoraDefaults[] =
{
    "*panelWindow.shadowThickness: 0",
    "*form.XmText.blinkRate: 0",
    "*helpButton.traversalOn: False",
    "*loginButton.traversalOn: False",
    NULL
};

PandoraComponent::PandoraComponent(const char *name, Widget parent)
    : VkComponent(name),
      _form(NULL),
      _panelWindow(NULL),
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

    /*
     * Binary-proven XtManageChildren call at the end of the constructor.
     * warningLabel, passwordText, userFullNameLabel and hostLabel remain
     * unmanaged initially.
     */
    managed[0] = _loginButton;
    managed[1] = _helpButton;
    managed[2] = _panelWindow;
    managed[3] = _userText;
    managed[4] = _userLabel;
    managed[5] = _passwordLabel;
    XtManageChildren(managed, 6);
}

PandoraComponent::~PandoraComponent()
{
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
