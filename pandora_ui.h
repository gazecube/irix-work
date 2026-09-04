/*
 * pandora_ui.h - portable reconstruction of the original IRIX clogin
 * Pandora widget hierarchy.
 *
 * Ground truth:
 *   - PandoraComponentUI::PandoraComponentUI(const char *, Widget)
 *   - /usr/lib/X11/app-defaults/Clogin
 *   - exported PandoraComponent/PandoraWindow symbols from target clogin
 *
 * This file deliberately uses the original widget instance names so SGI
 * app-defaults resources can map naturally onto the portable Motif tree.
 */
#ifndef DVLOGIN_PANDORA_UI_H
#define DVLOGIN_PANDORA_UI_H

typedef struct PandoraWidgets
{
	Widget form;
	Widget panelWindow;
	Widget scrolledWindow;
	Widget warningLabel;
	Widget userFullNameLabel;
	Widget userLabel;
	Widget userText;
	Widget passwordLabel;
	Widget passwordText;
	Widget hostLabel;
	Widget helpButton;
	Widget loginButton;
} PandoraWidgets;

static PandoraWidgets g_pandora_widgets;

static Widget pandora_create_label(Widget parent, const char *name,
	const char *text)
{
	return XtVaCreateManagedWidget(name,
		xmLabelGadgetClass, parent,
		XmNalignment, XmALIGNMENT_END,
		XtVaTypedArg, XmNlabelString, XmRString,
			text, strlen(text) + 1,
		NULL);
}

static void pandora_bind_legacy_handles(void)
{
	/*
	 * Keep the already-working auth/session callbacks untouched while the UI
	 * hierarchy is replaced underneath them.
	 */
	mainForm = g_pandora_widgets.form;
	username = g_pandora_widgets.userLabel;
	textBox1 = g_pandora_widgets.userText;
	password = g_pandora_widgets.passwordLabel;
	textBox2 = g_pandora_widgets.passwordText;
	hostLabel = g_pandora_widgets.hostLabel;
	helpButton = g_pandora_widgets.helpButton;
	scrolledWin0 = g_pandora_widgets.scrolledWindow;
}

static Widget pandora_create_component_ui(Widget parent)
{
	PandoraWidgets *ui = &g_pandora_widgets;

	memset(ui, 0, sizeof(*ui));

	/* Original instance name: form */
	ui->form = XtVaCreateManagedWidget("form",
		xmFormWidgetClass, parent,
		XmNshadowThickness, 0,
		NULL);

	/* Original instance name: warningLabel */
	ui->warningLabel = XtVaCreateManagedWidget("warningLabel",
		xmLabelWidgetClass, ui->form,
		XmNtopAttachment, XmATTACH_FORM,
		XmNleftAttachment, XmATTACH_FORM,
		XmNalignment, XmALIGNMENT_BEGINNING,
		NULL);

	/* Original instance name: userFullNameLabel */
	ui->userFullNameLabel = XtVaCreateManagedWidget("userFullNameLabel",
		xmLabelWidgetClass, ui->form,
		XmNleftAttachment, XmATTACH_FORM,
		XmNbottomAttachment, XmATTACH_FORM,
		XmNalignment, XmALIGNMENT_BEGINNING,
		NULL);

	/* Original instance name: userLabel */
	ui->userLabel = pandora_create_label(ui->form, "userLabel", "Login name:");
	XtVaSetValues(ui->userLabel,
		XmNleftAttachment, XmATTACH_FORM,
		XmNbottomAttachment, XmATTACH_FORM,
		XmNleftOffset, 45,
		XmNbottomOffset, 18,
		XmNwidth, 120,
		NULL);

	/* Original instance name: userText */
	ui->userText = XtVaCreateManagedWidget("userText",
		xmTextWidgetClass, ui->form,
		XmNleftAttachment, XmATTACH_WIDGET,
		XmNleftWidget, ui->userLabel,
		XmNbottomAttachment, XmATTACH_FORM,
		XmNleftOffset, 6,
		XmNbottomOffset, 13,
		XmNwidth, 225,
		XmNeditMode, XmSINGLE_LINE_EDIT,
		XmNblinkRate, 0,
		NULL);

	/* Original instance name: passwordLabel */
	ui->passwordLabel = pandora_create_label(ui->form,
		"passwordLabel", "Password:");
	XtVaSetValues(ui->passwordLabel,
		XmNleftAttachment, XmATTACH_FORM,
		XmNbottomAttachment, XmATTACH_WIDGET,
		XmNbottomWidget, ui->userLabel,
		XmNleftOffset, 45,
		XmNbottomOffset, 18,
		XmNwidth, 120,
		NULL);

	/* Original instance name: passwordText */
	ui->passwordText = XtVaCreateManagedWidget("passwordText",
		xmTextWidgetClass, ui->form,
		XmNleftAttachment, XmATTACH_WIDGET,
		XmNleftWidget, ui->passwordLabel,
		XmNbottomAttachment, XmATTACH_WIDGET,
		XmNbottomWidget, ui->userText,
		XmNleftOffset, 0,
		XmNbottomOffset, 12,
		XmNwidth, 225,
		XmNeditMode, XmSINGLE_LINE_EDIT,
		XmNblinkRate, 0,
		NULL);

	/* Original instance name: panelWindow */
	ui->panelWindow = XtVaCreateManagedWidget("panelWindow",
		xmFormWidgetClass, ui->form,
		XmNtopAttachment, XmATTACH_FORM,
		XmNtopOffset, 35,
		XmNleftAttachment, XmATTACH_FORM,
		XmNleftOffset, 35,
		XmNrightAttachment, XmATTACH_FORM,
		XmNrightOffset, 30,
		XmNbottomAttachment, XmATTACH_WIDGET,
		XmNbottomWidget, ui->passwordLabel,
		XmNbottomOffset, 25,
		XmNshadowThickness, 0,
		NULL);

	/*
	 * Original clogin uses an Imd icon panel here. The portable boundary uses
	 * XmScrolledWindow until libimdIconPanel is reconstructed.
	 */
	ui->scrolledWindow = XtVaCreateManagedWidget("scrolledWindow",
		xmScrolledWindowWidgetClass, ui->panelWindow,
		XmNscrollingPolicy, XmAUTOMATIC,
		XmNvisualPolicy, XmAUTOMATIC,
		XmNscrollBarDisplayPolicy, XmAS_NEEDED,
		XmNtraversalOn, False,
		XmNshadowThickness, 0,
		NULL);

	/* Original instance name: hostLabel */
	ui->hostLabel = XtVaCreateManagedWidget("hostLabel",
		xmLabelWidgetClass, ui->form,
		XmNleftAttachment, XmATTACH_FORM,
		XmNleftOffset, 33,
		XmNbottomAttachment, XmATTACH_FORM,
		XmNbottomOffset, 22,
		NULL);

	/* Original instance name: loginButton */
	ui->loginButton = XtVaCreateManagedWidget("loginButton",
		xmPushButtonWidgetClass, ui->form,
		XmNrightAttachment, XmATTACH_FORM,
		XmNrightOffset, 8,
		XmNbottomAttachment, XmATTACH_FORM,
		XmNbottomOffset, 25,
		XmNwidth, 72,
		XmNmarginHeight, 3,
		XmNtraversalOn, False,
		XtVaTypedArg, XmNlabelString, XmRString, "Log In", 7,
		NULL);

	/* Original instance name: helpButton */
	ui->helpButton = XtVaCreateManagedWidget("helpButton",
		xmPushButtonWidgetClass, ui->form,
		XmNrightAttachment, XmATTACH_WIDGET,
		XmNrightWidget, ui->loginButton,
		XmNrightOffset, 22,
		XmNbottomAttachment, XmATTACH_FORM,
		XmNbottomOffset, 25,
		XmNwidth, 72,
		XmNmarginHeight, 3,
		XmNtraversalOn, False,
		XtVaTypedArg, XmNlabelString, XmRString, "Help", 5,
		NULL);

	pandora_bind_legacy_handles();
	return ui->form;
}

#endif /* DVLOGIN_PANDORA_UI_H */
