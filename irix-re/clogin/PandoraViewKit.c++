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
        XmNalignment, XmALIGNMENT_BEGINNING,
        NULL);

    _userFullNameLabel = XtVaCreateManagedWidget(
        "userFullNameLabel",
        xmLabelWidgetClass,
        _baseWidget,
        XmNalignment, XmALIGNMENT_BEGINNING,
        NULL);

    _userLabel = XtVaCreateManagedWidget(
        "userLabel",
        xmLabelGadgetClass,
        _baseWidget,
        XmNalignment, XmALIGNMENT_END,
        XtVaTypedArg, XmNlabelString, XmRString,
        "Login name:", 12,
        NULL);

    _userText = XtVaCreateManagedWidget(
        "userText",
        xmTextWidgetClass,
        _baseWidget,
        XmNeditMode, XmSINGLE_LINE_EDIT,
        XmNblinkRate, 0,
        NULL);

    _passwordLabel = XtVaCreateManagedWidget(
        "passwordLabel",
        xmLabelGadgetClass,
        _baseWidget,
        XmNalignment, XmALIGNMENT_END,
        XtVaTypedArg, XmNlabelString, XmRString,
        "Password:", 10,
        NULL);

    _passwordText = XtVaCreateManagedWidget(
        "passwordText",
        xmTextWidgetClass,
        _baseWidget,
        XmNeditMode, XmSINGLE_LINE_EDIT,
        XmNblinkRate, 0,
        NULL);

    _panelWindow = XtVaCreateManagedWidget(
        "panelWindow",
        xmFormWidgetClass,
        _baseWidget,
        XmNshadowThickness, 0,
        NULL);

    _hostLabel = XtVaCreateManagedWidget(
        "hostLabel",
        xmLabelWidgetClass,
        _baseWidget,
        NULL);

    _helpButton = XtVaCreateManagedWidget(
        "helpButton",
        xmPushButtonWidgetClass,
        _baseWidget,
        XtVaTypedArg, XmNlabelString, XmRString,
        "Help", 5,
        NULL);

    _loginButton = XtVaCreateManagedWidget(
        "loginButton",
        xmPushButtonWidgetClass,
        _baseWidget,
        XtVaTypedArg, XmNlabelString, XmRString,
        "Log In", 7,
        NULL);
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
