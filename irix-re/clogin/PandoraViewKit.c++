#include "PandoraViewKit.h"

#include <Xm/Form.h>
#include <Xm/Label.h>
#include <Xm/LabelG.h>
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
    setDefaultResources(parent, pandoraDefaults);

    /*
     * VkComponent contract: the derived class owns one single-rooted widget
     * subtree and leaves the base widget unmanaged for the container/window
     * to manage through addView()/show().
     */
    _baseWidget = XtVaCreateWidget(
        "form",
        xmFormWidgetClass,
        parent,
        XmNshadowThickness, 0,
        NULL);
    installDestroyHandler();

    _warningLabel = XtVaCreateManagedWidget(
        "warningLabel",
        xmLabelWidgetClass,
        _baseWidget,
        XmNtopAttachment, XmATTACH_FORM,
        XmNleftAttachment, XmATTACH_FORM,
        XmNalignment, XmALIGNMENT_BEGINNING,
        XmNmappedWhenManaged, False,
        NULL);

    _userFullNameLabel = XtVaCreateManagedWidget(
        "userFullNameLabel",
        xmLabelWidgetClass,
        _baseWidget,
        XmNleftAttachment, XmATTACH_FORM,
        XmNbottomAttachment, XmATTACH_FORM,
        XmNalignment, XmALIGNMENT_BEGINNING,
        XmNmappedWhenManaged, False,
        NULL);

    _userLabel = XtVaCreateManagedWidget(
        "userLabel",
        xmLabelGadgetClass,
        _baseWidget,
        XmNleftAttachment, XmATTACH_FORM,
        XmNbottomAttachment, XmATTACH_FORM,
        XmNalignment, XmALIGNMENT_END,
        XtVaTypedArg, XmNlabelString, XmRString,
        "Login name:", 12,
        NULL);

    _userText = XtVaCreateManagedWidget(
        "userText",
        xmTextWidgetClass,
        _baseWidget,
        XmNleftAttachment, XmATTACH_WIDGET,
        XmNleftWidget, _userLabel,
        XmNbottomAttachment, XmATTACH_FORM,
        XmNeditMode, XmSINGLE_LINE_EDIT,
        XmNblinkRate, 0,
        NULL);

    _passwordLabel = XtVaCreateManagedWidget(
        "passwordLabel",
        xmLabelGadgetClass,
        _baseWidget,
        XmNleftAttachment, XmATTACH_FORM,
        XmNbottomAttachment, XmATTACH_WIDGET,
        XmNbottomWidget, _userLabel,
        XmNalignment, XmALIGNMENT_END,
        XtVaTypedArg, XmNlabelString, XmRString,
        "Password:", 10,
        NULL);

    _passwordText = XtVaCreateManagedWidget(
        "passwordText",
        xmTextWidgetClass,
        _baseWidget,
        XmNleftAttachment, XmATTACH_WIDGET,
        XmNleftWidget, _passwordLabel,
        XmNbottomAttachment, XmATTACH_WIDGET,
        XmNbottomWidget, _userText,
        XmNeditMode, XmSINGLE_LINE_EDIT,
        XmNblinkRate, 0,
        NULL);

    _panelWindow = XtVaCreateManagedWidget(
        "panelWindow",
        xmFormWidgetClass,
        _baseWidget,
        XmNtopAttachment, XmATTACH_FORM,
        XmNleftAttachment, XmATTACH_FORM,
        XmNrightAttachment, XmATTACH_FORM,
        XmNbottomAttachment, XmATTACH_WIDGET,
        XmNbottomWidget, _passwordLabel,
        XmNshadowThickness, 0,
        NULL);

    _hostLabel = XtVaCreateManagedWidget(
        "hostLabel",
        xmLabelWidgetClass,
        _baseWidget,
        XmNleftAttachment, XmATTACH_FORM,
        XmNbottomAttachment, XmATTACH_FORM,
        NULL);

    _loginButton = XtVaCreateManagedWidget(
        "loginButton",
        xmPushButtonWidgetClass,
        _baseWidget,
        XmNrightAttachment, XmATTACH_FORM,
        XmNbottomAttachment, XmATTACH_FORM,
        XtVaTypedArg, XmNlabelString, XmRString,
        "Log In", 7,
        NULL);

    _helpButton = XtVaCreateManagedWidget(
        "helpButton",
        xmPushButtonWidgetClass,
        _baseWidget,
        XmNrightAttachment, XmATTACH_WIDGET,
        XmNrightWidget, _loginButton,
        XmNbottomAttachment, XmATTACH_FORM,
        XtVaTypedArg, XmNlabelString, XmRString,
        "Help", 5,
        NULL);

    /*
     * The attachment graph above is reconstructed from the original widget
     * hierarchy and currently preserved portable Pandora behavior. Exact SGI
     * offsets, widths, fonts and labels remain resource-driven through the
     * Clogin app-defaults database.
     */
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
