/*
 * pandora_ui.h - portable reconstruction of the original IRIX clogin
 * Pandora widget hierarchy.
 *
 * Ground truth:
 *   - PandoraComponentUI::PandoraComponentUI(const char *, Widget)
 *   - /usr/lib/X11/app-defaults/Clogin
 *   - exported PandoraComponent/PandoraWindow symbols from target clogin
 *
 * The original SGI instance names are retained so the shipped Cl­ogin
 * app-defaults resource namespace maps naturally onto this Motif tree.
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
	/* Preserve the already-working auth/session implementation. */
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

	ui->form = XtVaCreateManagedWidget("form",
		xmFormWidgetClass, parent,
		XmNshadowThickness, 0,
		NULL);

	ui->warningLabel = XtVaCreateManagedWidget("warningLabel",
		xmLabelWidgetClass, ui->form,
		XmNtopAttachment, XmATTACH_FORM,
		XmNleftAttachment, XmATTACH_FORM,
		XmNalignment, XmALIGNMENT_BEGINNING,
		XmNmappedWhenManaged, False,
		NULL);

	ui->userFullNameLabel = XtVaCreateManagedWidget("userFullNameLabel",
		xmLabelWidgetClass, ui->form,
		XmNleftAttachment, XmATTACH_FORM,
		XmNbottomAttachment, XmATTACH_FORM,
		XmNalignment, XmALIGNMENT_BEGINNING,
		XmNmappedWhenManaged, False,
		NULL);

	ui->userLabel = pandora_create_label(ui->form, "userLabel", "Login name:");
	XtVaSetValues(ui->userLabel,
		XmNleftAttachment, XmATTACH_FORM,
		XmNbottomAttachment, XmATTACH_FORM,
		XmNleftOffset, 45,
		XmNbottomOffset, 18,
		XmNwidth, 120,
		NULL);

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
		XmNhighlightThickness, 1,
		NULL);

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

	ui->passwordText = XtVaCreateManagedWidget("passwordText",
		xmTextWidgetClass, ui->form,
		XmNleftAttachment, XmATTACH_WIDGET,
		XmNleftWidget, ui->passwordLabel,
		XmNbottomAttachment, XmATTACH_WIDGET,
		XmNbottomWidget, ui->userText,
		XmNleftOffset, 6,
		XmNbottomOffset, 12,
		XmNwidth, 225,
		XmNeditMode, XmSINGLE_LINE_EDIT,
		XmNblinkRate, 0,
		XmNhighlightThickness, 1,
		NULL);

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

	/* Original uses the Imd icon panel here. XmScrolledWindow is the
	 * current portability boundary until libimdIconPanel is reconstructed. */
	ui->scrolledWindow = XtVaCreateManagedWidget("scrolledWindow",
		xmScrolledWindowWidgetClass, ui->panelWindow,
		XmNtopAttachment, XmATTACH_FORM,
		XmNleftAttachment, XmATTACH_FORM,
		XmNrightAttachment, XmATTACH_FORM,
		XmNbottomAttachment, XmATTACH_FORM,
		XmNscrollingPolicy, XmAUTOMATIC,
		XmNvisualPolicy, XmAUTOMATIC,
		XmNscrollBarDisplayPolicy, XmAS_NEEDED,
		XmNtraversalOn, False,
		XmNshadowThickness, 0,
		NULL);

	ui->hostLabel = XtVaCreateManagedWidget("hostLabel",
		xmLabelWidgetClass, ui->form,
		XmNleftAttachment, XmATTACH_FORM,
		XmNleftOffset, 33,
		XmNbottomAttachment, XmATTACH_FORM,
		XmNbottomOffset, 22,
		NULL);

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

static void pandora_wire_callbacks(void)
{
	PandoraWidgets *ui = &g_pandora_widgets;

	XtAddCallback(ui->userText, XmNfocusCallback,
		textWidgetGainedFocus, ui->userText);
	XtAddCallback(ui->userText, XmNlosingFocusCallback,
		textWidgetLostFocus, ui->userText);
	XtAddCallback(ui->userText, XmNvalueChangedCallback,
		textInputMotionCB, NULL);
	XtAddCallback(ui->userText, XmNvalueChangedCallback,
		clogin_username_changed_cb, NULL);
	XtAddCallback(ui->userText, XmNvalueChangedCallback,
		clogin_ui_username_changed_cb, NULL);
	XtAddCallback(ui->userText, XmNactivateCallback,
		textInputActivatedCB, NULL);

	XtAddCallback(ui->passwordText, XmNfocusCallback,
		textWidgetGainedFocus, ui->passwordText);
	XtAddCallback(ui->passwordText, XmNlosingFocusCallback,
		textWidgetLostFocus, ui->passwordText);
	XtAddCallback(ui->passwordText, XmNmodifyVerifyCallback,
		passwordModifyVerifyCB, NULL);
	XtAddCallback(ui->passwordText, XmNvalueChangedCallback,
		textInputMotionCB, NULL);
	XtAddCallback(ui->passwordText, XmNactivateCallback,
		textInputActivatedCB, NULL);

	XtAddCallback(ui->loginButton, XmNactivateCallback,
		loginButtonCB, NULL);
	XtAddCallback(ui->helpButton, XmNactivateCallback,
		helpButtonCB, NULL);
}

static void pandora_set_hostname(void)
{
	char hostname[1024];

	if(gethostname(hostname, sizeof(hostname)) != 0)
		strcpy(hostname, "localhost");
	hostname[sizeof(hostname) - 1] = '\0';

	XtVaSetValues(g_pandora_widgets.hostLabel,
		XtVaTypedArg, XmNlabelString, XmRString,
			hostname, strlen(hostname) + 1,
		NULL);
}

static void pandora_center_shell(void)
{
	Display *dpy;
	Screen *screen;
	Dimension width, height;
	Position x, y;

	if(topLevel == NULL)
		return;

	dpy = XtDisplay(topLevel);
	screen = XtScreen(topLevel);
	XtVaGetValues(topLevel, XmNwidth, &width, XmNheight, &height, NULL);

	x = (Position)((WidthOfScreen(screen) - width) / 2);
	y = (Position)((HeightOfScreen(screen) - height) / 2);
	XtVaSetValues(topLevel, XmNx, x, XmNy, y, NULL);
	(void)dpy;
}

/* Live portable entry point for the reconstructed Pandora greeter. */
static void run_pandora_gui(int argc, char *argv[])
{
	Widget icon_view = NULL;

	XtSetLanguageProc(NULL, NULL, NULL);

	/* "Clogin" is the original application resource class. */
	topLevel = XtVaAppInitialize(&app, "Clogin",
		NULL, 0, &argc, argv, fb_xres, NULL);

	XtVaSetValues(topLevel,
		XmNtitle, "Clogin",
		XmNmwmInputMode, MWM_INPUT_SYSTEM_MODAL,
		NULL);

	pandora_create_component_ui(topLevel);
	pandora_wire_callbacks();
	pandora_set_hostname();

	/* Populate the original displayIcon() portability boundary using the
	 * reconstructed PwdNameList/user-picture policy. */
	clogin_add_shortcuts(g_pandora_widgets.scrolledWindow);
	XtVaGetValues(g_pandora_widgets.scrolledWindow,
		XmNworkWindow, &icon_view, NULL);
	g_clogin_icon_view = icon_view;

	clogin_load_policy();
	clogin_ui_apply_window_policy();
	clogin_set_password_visible(False, False);

	XtRealizeWidget(topLevel);
	pandora_center_shell();
	XmProcessTraversal(g_pandora_widgets.userText, XmTRAVERSE_CURRENT);

	XtAppMainLoop(app);
}

#endif /* DVLOGIN_PANDORA_UI_H */
