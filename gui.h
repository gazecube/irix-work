/* fallback Xresources */
static String fb_xres[] =
{
	/* Use the original SGI Screen core X bitmap font throughout. */
	"*font: -sgi-screen-medium-r-normal--14-140-72-72-m-70-iso8859-1",
	"*fontList: -sgi-screen-medium-r-normal--14-140-72-72-m-70-iso8859-1",

	"Xcursor.theme: DeskViewCursors",
	"Xcursor.size: 24",

	"*XmScrolledWindow*highlightThickness: 0",
	"*XmScrolledWindow*spacing: 0",
	"*XmScrolledWindow*sliderMark: XmTHUMB_MARK",

	"?.TipShell.TipLabel.foreground: black",
	"?.TipShell.TipLabel.background: white",
	"?.TipShell.TipLabel.marginWidth: 10",
	"?.TipShell.TipLabel.marginHeight: 6",

	NULL,
};

Widget topLevel, mainForm, username, password, sep, Cancel, Enter, Power, Reboot;


/*	Open the dtlogin manual in CDE's native help viewer.
	
	Using dthelpview avoids opening an unauthenticated terminal or pager
	from the greeter while still presenting the real dtlogin man page.
*/
static void helpButtonCB(Widget w, XtPointer client_data, XtPointer call_data)
{
	pid_t pid;
	pid_t grandchild;

	pid = fork();
	if(pid < 0)
	{
		fprintf(stderr, "dvlogin: could not fork help viewer: %s\n", strerror(errno));
		return;
	}

	if(pid == 0)
	{
		grandchild = fork();
		if(grandchild < 0)
		{
			_exit(127);
		}
		if(grandchild > 0)
		{
			_exit(0);
		}

		setsid();
		execlp("dthelpview", "dthelpview", "-manPage", "dtlogin", (char *)NULL);
		execl("/usr/dt/bin/dthelpview", "dthelpview", "-manPage", "dtlogin", (char *)NULL);
		fprintf(stderr, "dvlogin: could not start dthelpview: %s\n", strerror(errno));
		_exit(127);
	}

	while(waitpid(pid, NULL, 0) < 0 && errno == EINTR)
	{
		/* retry */
	}
}


/*	run_motif_gui - create and run the Motif login GUI.
	
	Blocks in XtAppMainLoop until XtAppSetExitFlag is called
	(triggered by auth_success_exit in callbacks.h).
	
	Runs in the forked GUI child process, no threads needed.
*/
void run_motif_gui(int argc, char *argv[])
{
/* setup localization */
	XtSetLanguageProc(NULL, NULL, NULL);

/* open top level shell with fallback resources */
	topLevel = XtVaAppInitialize(&app, "dvlogin",
		NULL, 0, &argc, argv, fb_xres, NULL);

	XtVaSetValues(topLevel,
		XmNtitle, "dvlogin",

		XmNmaxWidth, 600,
		XmNminWidth, 600,
		XmNwidth, 600,

		XmNmaxHeight, 600,
		XmNminHeight, 600,
		XmNheight, 600,

		XmNmwmInputMode, MWM_INPUT_SYSTEM_MODAL,
	NULL);


	int decor;
	XtVaGetValues(topLevel, XmNmwmDecorations, &decor, NULL);
		decor &= ~MWM_DECOR_BORDER;
	XtVaSetValues(topLevel, XmNmwmDecorations, decor, NULL);


	Widget outerFrame = XtVaCreateManagedWidget("outerFrame",
		xmFormWidgetClass, topLevel,
		XmNtopAttachment, XmATTACH_FORM,
		XmNtopOffset, 0,
		XmNleftAttachment, XmATTACH_FORM,
		XmNleftOffset, 0,
		XmNrightAttachment, XmATTACH_FORM,
		XmNrightOffset, 0,
		XmNbottomAttachment, XmATTACH_FORM,
		XmNbottomOffset, 0,
		XmNshadowThickness, 0,
		XtVaTypedArg, XmNbackground, XmRString, "#000000", 8,
	NULL);

	XtAddEventHandler(outerFrame, EnterWindowMask, False,
		mouseOverLoginPanel, (XtPointer)outerFrame);

	mainForm = XtVaCreateManagedWidget("mainForm",
		xmFormWidgetClass, outerFrame,
		XmNtopAttachment, XmATTACH_FORM,
		XmNtopOffset, 1,
		XmNleftAttachment, XmATTACH_FORM,
		XmNleftOffset, 1,
		XmNrightAttachment, XmATTACH_FORM,
		XmNrightOffset, 1,
		XmNbottomAttachment, XmATTACH_FORM,
		XmNbottomOffset, 1,
		XmNshadowThickness, 2,
	NULL);



	Widget loginButton = XtVaCreateManagedWidget("loginButton",
		xmPushButtonWidgetClass, mainForm,
		XmNrightAttachment, XmATTACH_FORM,
		XmNrightOffset, 31,
		XmNbottomAttachment, XmATTACH_FORM,
		XmNbottomOffset, 30,
		XmNmarginWidth, 10,
		XmNmarginHeight, 6,
		XmNhighlightThickness, 1,
		XmNtraversalOn, 1,
		XtVaTypedArg, XmNbackground, XmRString, "#aaaaaa", 8,
		XtVaTypedArg, XmNforeground, XmRString, "#000000", 8,
		XtVaTypedArg, XmNhighlightColor, XmRString, "red", 4,
		XtVaTypedArg, XmNlabelString, XmRString, "Log In", 7,
	NULL);
	XtAddEventHandler(loginButton, EnterWindowMask, False,
		mouseOverPushButton, (XtPointer)loginButton);
	XtAddEventHandler(loginButton, LeaveWindowMask, False,
		mouseLeftPushButton, (XtPointer)loginButton);
	XtAddCallback(loginButton, XmNactivateCallback, loginButtonCB, NULL);



	helpButton = XtVaCreateManagedWidget("helpButton",
		xmPushButtonWidgetClass, mainForm,
		XmNrightAttachment, XmATTACH_WIDGET,
		XmNrightWidget, loginButton,
		XmNrightOffset, 16,
		XmNbottomAttachment, XmATTACH_FORM,
		XmNbottomOffset, 30,
		XmNmarginWidth, 10,
		XmNmarginHeight, 6,
		XmNhighlightThickness, 1,
		XmNtraversalOn, 1,
		XmNshadowThickness, 2,
		XtVaTypedArg, XmNbackground, XmRString, "#aaaaaa", 8,
		XtVaTypedArg, XmNforeground, XmRString, "#000000", 8,
		XtVaTypedArg, XmNhighlightColor, XmRString, "red", 4,
		XtVaTypedArg, XmNlabelString, XmRString, "Help", 5,
	NULL);
	XtAddEventHandler(helpButton, EnterWindowMask, False,
		mouseOverPushButton, (XtPointer)helpButton);
	XtAddEventHandler(helpButton, LeaveWindowMask, False,
		mouseLeftPushButton, (XtPointer)helpButton);
	XtAddCallback(helpButton, XmNactivateCallback, helpButtonCB, NULL);


/*
	powerButton = XtVaCreateManagedWidget("powerButton",
		xmToggleButtonWidgetClass, mainForm,
		XmNrightAttachment, XmATTACH_WIDGET,
		XmNrightWidget, optionsButton,
		XmNrightOffset, 0,
		XmNbottomAttachment, XmATTACH_FORM,
		XmNbottomOffset, 30,
		XmNmarginWidth, 10,
		XmNmarginHeight, 6,
		XmNhighlightThickness, 0,
		XmNindicatorOn, XmINDICATOR_NONE,
		XmNtraversalOn, 0,
		XmNshadowThickness, 2,
		XmNfillOnSelect, 1,
		XmNlabelType, XmSTRING,
		XmNset, 0,
		XtVaTypedArg, XmNbackground, XmRString, "#aaaaaa", 8,
		XtVaTypedArg, XmNforeground, XmRString, "#000000", 8,
		XtVaTypedArg, XmNhighlightColor, XmRString, "red", 4,
		XtVaTypedArg, XmNlabelString, XmRString, "Power", 6,
	NULL);
	XtAddEventHandler(powerButton, EnterWindowMask, False,
		mouseOverPushButton, (XtPointer)powerButton);
	XtAddEventHandler(powerButton, LeaveWindowMask, False,
		mouseLeftPushButton, (XtPointer)powerButton);
*/

/*
	helpButton = XtVaCreateManagedWidget("helpButton",
		xmToggleButtonWidgetClass, mainForm,
		XmNrightAttachment, XmATTACH_WIDGET,
		XmNrightWidget, powerButton,
		XmNrightOffset, 0,
		XmNbottomAttachment, XmATTACH_FORM,
		XmNbottomOffset, 30,
		XmNmarginWidth, 10,
		XmNmarginHeight, 6,
		XmNhighlightThickness, 0,
		XmNindicatorOn, XmINDICATOR_NONE,
		XmNtraversalOn, 1,
		XmNshadowThickness, 2,
		XmNfillOnSelect, 1,
		XmNlabelType, XmSTRING,
		XmNset, 0,
		XtVaTypedArg, XmNbackground, XmRString, "#aaaaaa", 8,
		XtVaTypedArg, XmNforeground, XmRString, "#000000", 8,
		XtVaTypedArg, XmNhighlightColor, XmRString, "red", 4,
		XtVaTypedArg, XmNlabelString, XmRString, "?", 7,
	NULL);
	XtAddEventHandler(helpButton, EnterWindowMask, False,
		mouseOverPushButton, (XtPointer)helpButton);
	XtAddEventHandler(helpButton, LeaveWindowMask, False,
		mouseLeftPushButton, (XtPointer)helpButton);
*/


	sep = XtVaCreateManagedWidget("sep",
		xmSeparatorWidgetClass, mainForm,
		XmNrightAttachment, XmATTACH_FORM,
		XmNrightOffset, 3,
		XmNleftAttachment, XmATTACH_FORM,
		XmNleftOffset, 3,
		XmNbottomAttachment, XmATTACH_WIDGET,
		XmNbottomWidget, loginButton,
		XmNbottomOffset, 16,
		XmNshadowThickness, 0,
		XmNmappedWhenManaged, 0,
	NULL);


	char hostname[1024];
	gethostname(hostname, sizeof(hostname));

	hostLabel = XtVaCreateManagedWidget("hostLabel",
		xmLabelWidgetClass, mainForm,
		XmNtopAttachment, XmATTACH_WIDGET,
		XmNtopWidget, sep,
		XmNtopOffset, 0,
		XmNleftAttachment, XmATTACH_FORM,
		XmNleftOffset, 38,
		XmNbottomAttachment, XmATTACH_FORM,
		XmNbottomOffset, 18,
		XtVaTypedArg, XmNforeground, XmRString, "#8228aa", 8,
		XtVaTypedArg, XmNlabelString, XmRString, hostname, sizeof(hostname) + 1,
	NULL);



	password = XtVaCreateManagedWidget("password",
		xmLabelGadgetClass, mainForm,
		XmNleftAttachment, XmATTACH_FORM,
		XmNleftOffset, 8,
		XmNbottomAttachment, XmATTACH_WIDGET,
		XmNbottomWidget, sep,
		XmNbottomOffset, 16,
		XmNalignment, XmALIGNMENT_END,
		XmNwidth, 110,
		XtVaTypedArg, XmNlabelString, XmRString, "Password:", 7,
	NULL);

	textBox2 = XtVaCreateManagedWidget("textBox2",
		xmTextWidgetClass, mainForm,
		XmNleftAttachment, XmATTACH_WIDGET,
		XmNleftWidget, password,
		XmNleftOffset, 6,
		XmNrightAttachment, XmATTACH_POSITION,
		XmNrightPosition, 50,
		XmNbottomAttachment, XmATTACH_WIDGET,
		XmNbottomWidget, sep,
		XmNbottomOffset, 16,
		XmNmarginHeight, 6,
		XmNmarginWidth, 8,
		XmNeditMode, XmSINGLE_LINE_EDIT,
		XmNhighlightThickness, 1,
		XmNshadowThickness, 2,
		XmNcursorPositionVisible, 0,
		XmNautoShowCursorPosition, 0,
		XmNblinkRate, 400,
		XmNtraversalOn, 1,
		XtVaTypedArg, XmNbackground, XmRString, "#b98e8e", 8,
		XtVaTypedArg, XmNforeground, XmRString, "#000000", 8,
		XtVaTypedArg, XmNhighlightColor, XmRString, "red", 4,
	NULL);

	XtAddCallback(textBox2, XmNfocusCallback, textWidgetGainedFocus, textBox2);
	XtAddCallback(textBox2, XmNlosingFocusCallback, textWidgetLostFocus, textBox2);
	XtAddEventHandler(textBox2, EnterWindowMask, False,
		mouseOverTextInput, (XtPointer)textBox2);
	XtAddEventHandler(textBox2, LeaveWindowMask, False,
		mouseLeftTextInput, (XtPointer)textBox2);
	XtAddCallback(textBox2, XmNmodifyVerifyCallback, passwordModifyVerifyCB, NULL);
	XtAddCallback(textBox2, XmNvalueChangedCallback, textInputMotionCB, NULL);
	XtAddCallback(textBox2, XmNactivateCallback, textInputActivatedCB, NULL);



	username = XtVaCreateManagedWidget("username",
		xmLabelGadgetClass, mainForm,
		XmNleftAttachment, XmATTACH_FORM,
		XmNleftOffset, 8,
		XmNbottomAttachment, XmATTACH_WIDGET,
		XmNbottomWidget, password,
		XmNbottomOffset, 10,
		XmNalignment, XmALIGNMENT_END,
		XmNwidth, 110,
		XtVaTypedArg, XmNlabelString, XmRString, "Username:", 7,
	NULL);

	textBox1 = XtVaCreateManagedWidget("textBox1",
		xmTextWidgetClass, mainForm,
		XmNleftAttachment, XmATTACH_WIDGET,
		XmNleftWidget, username,
		XmNleftOffset, 6,
		XmNrightAttachment, XmATTACH_POSITION,
		XmNrightPosition, 50,
		XmNbottomAttachment, XmATTACH_WIDGET,
		XmNbottomWidget, textBox2,
		XmNbottomOffset, 10,
		XmNmarginHeight, 6,
		XmNmarginWidth, 8,
		XmNeditMode, XmSINGLE_LINE_EDIT,
		XmNhighlightThickness, 1,
		XmNshadowThickness, 2,
		XmNcursorPositionVisible, 0,
		XmNautoShowCursorPosition, 0,
		XmNblinkRate, 400,
		XmNtraversalOn, 1,
		XtVaTypedArg, XmNbackground, XmRString, "#b98e8e", 8,
		XtVaTypedArg, XmNforeground, XmRString, "#000000", 8,
		XtVaTypedArg, XmNhighlightColor, XmRString, "red", 4,
	NULL);

	XtAddCallback(textBox1, XmNfocusCallback, textWidgetGainedFocus, textBox1);
	XtAddCallback(textBox1, XmNlosingFocusCallback, textWidgetLostFocus, textBox1);
	XtAddEventHandler(textBox1, EnterWindowMask, False,
		mouseOverTextInput, (XtPointer)textBox1);
	XtAddEventHandler(textBox1, LeaveWindowMask, False,
		mouseLeftTextInput, (XtPointer)textBox1);
	XtAddCallback(textBox1, XmNvalueChangedCallback, textInputMotionCB, NULL);
	XtAddCallback(textBox1, XmNactivateCallback, textInputActivatedCB, NULL);


	Dimension textBox_h;
	XtVaGetValues(textBox1, XmNheight, &textBox_h, NULL);
	XtVaSetValues(username, XmNheight, textBox_h, NULL);
	XtVaSetValues(password, XmNheight, textBox_h, NULL);



	Widget innerFrame0 = XtVaCreateManagedWidget("innerFrame0",
		xmFormWidgetClass, mainForm,
		XmNtopAttachment, XmATTACH_FORM,
		XmNtopOffset, 32,
		XmNleftAttachment, XmATTACH_FORM,
		XmNleftOffset, 32,
		XmNrightAttachment, XmATTACH_FORM,
		XmNrightOffset, 32,
		XmNbottomAttachment, XmATTACH_WIDGET,
		XmNbottomWidget, textBox1,
		XmNbottomOffset, 20,
		XmNshadowThickness, 1,
		XmNshadowType, XmSHADOW_IN,
		XtVaTypedArg, XmNbackground, XmRString, "#bebebe", 8,
	NULL);

	Widget innerFrame1 = XtVaCreateManagedWidget("innerFrame1",
		xmFormWidgetClass, innerFrame0,
		XmNtopAttachment, XmATTACH_FORM,
		XmNtopOffset, 1,
		XmNleftAttachment, XmATTACH_FORM,
		XmNleftOffset, 1,
		XmNrightAttachment, XmATTACH_FORM,
		XmNrightOffset, 1,
		XmNbottomAttachment, XmATTACH_FORM,
		XmNbottomOffset, 1,
		XmNshadowThickness, 0,
		XtVaTypedArg, XmNbackground, XmRString, "#000000", 8,
	NULL);

	Widget innerFrame2 = XtVaCreateManagedWidget("innerFrame2",
		xmFormWidgetClass, innerFrame1,
		XmNtopAttachment, XmATTACH_FORM,
		XmNtopOffset, 1,
		XmNleftAttachment, XmATTACH_FORM,
		XmNleftOffset, 1,
		XmNrightAttachment, XmATTACH_FORM,
		XmNrightOffset, 1,
		XmNbottomAttachment, XmATTACH_FORM,
		XmNbottomOffset, 1,
		XmNshadowThickness, 0,
		XtVaTypedArg, XmNbackground, XmRString, "#cdc8b4", 8,
	NULL);


	scrolledWin0 = XtVaCreateManagedWidget("scrolledWin0",
		xmScrolledWindowWidgetClass, innerFrame2,
		XmNscrollingPolicy, XmAUTOMATIC,
		XmNvisualPolicy, XmAUTOMATIC,
		XmNscrollBarDisplayPolicy, XmCONSTANT,
		XmNtopAttachment, XmATTACH_WIDGET,
		XmNtopWidget, innerFrame0,
		XmNtopOffset, 0,
		XmNleftAttachment, XmATTACH_FORM,
		XmNleftOffset, 0,
		XmNrightAttachment, XmATTACH_FORM,
		XmNrightOffset, 0,
		XmNbottomAttachment, XmATTACH_FORM,
		XmNbottomOffset, 0,
		XmNtraversalOn, 0,
		XmNshadowThickness, 0,
		XmNmappedWhenManaged, 1,
	NULL);

	Widget clipWindow0 = XtNameToWidget(scrolledWin0, "*ClipWindow");
	if(clipWindow0)
	{
		XtVaSetValues(clipWindow0,
			XtVaTypedArg, XmNbackground, XmRString, "#cdc8b4", 8,
		NULL);
	}

	Widget vertScrollBar0 = XtNameToWidget(scrolledWin0, "*VertScrollBar");
	if(vertScrollBar0)
	{
		XtVaSetValues(vertScrollBar0, 
			XmNwidth, 16, 
			XmNtraversalOn, 0,
		NULL);
	}
	
	addCustomShortcuts(scrolledWin0);
	

	scrolledWin = XtVaCreateManagedWidget("scrolledWin",
		xmScrolledWindowWidgetClass, mainForm,
		XmNscrollingPolicy, XmAUTOMATIC,
		XmNvisualPolicy, XmAUTOMATIC,
		XmNscrollBarDisplayPolicy, XmCONSTANT,
		XmNtopAttachment, XmATTACH_WIDGET,
		XmNtopWidget, innerFrame0,
		XmNtopOffset, 6,
		XmNleftAttachment, XmATTACH_WIDGET,
		XmNleftWidget, textBox1,
		XmNleftOffset, 6,
		XmNrightAttachment, XmATTACH_FORM,
		XmNrightOffset, 32,
		XmNbottomAttachment, XmATTACH_WIDGET,
		XmNbottomWidget, loginButton,
		XmNbottomOffset, 6,
		XmNtraversalOn, 0,
		XmNshadowThickness, 2,
		XmNmappedWhenManaged, 0,
	NULL);

	Widget clipWindow = XtNameToWidget(scrolledWin, "*ClipWindow");
	if(clipWindow)
	{
		XtVaSetValues(clipWindow,
			XtVaTypedArg, XmNbackground, XmRString, "#a7a7a7", 8,
		NULL);
	}

	Widget vertScrollBar = XtNameToWidget(scrolledWin, "*VertScrollBar");
	if(vertScrollBar)
	{
		XtVaSetValues(vertScrollBar, 
			XmNwidth, 18, 
			XmNtraversalOn, 0,
		NULL);
	}

	Widget horScrollBar = XtNameToWidget(scrolledWin, "*HorScrollBar");
	if(horScrollBar)
	{
		XtVaSetValues(horScrollBar, XmNheight, 18, NULL);
	}


	Widget optRC = XtVaCreateManagedWidget("optRC",
		xmRowColumnWidgetClass, scrolledWin,
		XmNorientation, XmVERTICAL,
		XmNpacking, XmPACK_TIGHT,
		XmNtraversalOn, 0,
		XmNmarginHeight, 4,
		XmNmarginWidth, 5,
		XmNspacing, 0,
		XmNtraversalOn, 0,
		XtVaTypedArg, XmNbackground, XmRString, "#a7a7a7", 8,
	NULL);
	XtVaSetValues(scrolledWin, XmNworkWindow, optRC, NULL);
	

	Widget optTog1 = XtVaCreateManagedWidget("optTog1",
		xmToggleButtonWidgetClass, optRC,
		XmNleftAttachment, XmATTACH_FORM,
		XmNleftOffset, 10,
		XmNbottomAttachment, XmATTACH_FORM,
		XmNbottomOffset, 7,
		XmNalignment, XmALIGNMENT_BEGINNING,
		XmNindicatorOn, XmINDICATOR_CHECK_BOX,
		XmNindicatorSize, 18,
		XmNtoggleMode, XmTOGGLE_BOOLEAN,
		XmNhighlightThickness, 0,
		XmNshadowThickness, 0,
		XmNset, 1,
		XmNspacing, 7,
		XmNmarginWidth, 2,
		XmNmarginHeight, 2,
		XmNtraversalOn, 0,
		XtVaTypedArg, XmNforeground, XmRString, "#000000", 8,
		XtVaTypedArg, XmNbackground, XmRString, "#a7a7a7", 8,
		XtVaTypedArg, XmNlabelString, XmRString, "Display system hostname.", 64,
	NULL);
	XtAddEventHandler(optTog1, EnterWindowMask, False,
		mouseOverPushButton, (XtPointer)optTog1);
	XtAddEventHandler(optTog1, LeaveWindowMask, False,
		mouseLeftPushButton, (XtPointer)optTog1);
	
	XtAddCallback(optTog1, XmNvalueChangedCallback, toggleHostnameCB, NULL);

/*
	Widget optTog2 = XtVaCreateManagedWidget("optTog2",
		xmToggleButtonWidgetClass, optRC,
		XmNleftAttachment, XmATTACH_FORM,
		XmNleftOffset, 10,
		XmNbottomAttachment, XmATTACH_FORM,
		XmNbottomOffset, 7,
		XmNalignment, XmALIGNMENT_BEGINNING,
		XmNindicatorOn, XmINDICATOR_CHECK_BOX,
		XmNindicatorSize, 18,
		XmNtoggleMode, XmTOGGLE_BOOLEAN,
		XmNhighlightThickness, 0,
		XmNshadowThickness, 0,
		XmNset, 0,
		XmNspacing, 7,
		XmNmarginWidth, 2,
		XmNmarginHeight, 2,
		XmNtraversalOn, 0,
		XtVaTypedArg, XmNforeground, XmRString, "#000000", 8,
		XtVaTypedArg, XmNbackground, XmRString, "#a7a7a7", 8,
		XtVaTypedArg, XmNlabelString, XmRString, "Enable clock widget.", 64,
	NULL);
	XtAddEventHandler(optTog2, EnterWindowMask, False,
		mouseOverPushButton, (XtPointer)optTog2);
	XtAddEventHandler(optTog2, LeaveWindowMask, False,
		mouseLeftPushButton, (XtPointer)optTog2);
*/



/* realize the widget tree */
	XtRealizeWidget(topLevel);
	
	update_shortcut_layout(scrolledWin0);

/* give textBox1 default focus */
	XmProcessTraversal(textBox1, XmTRAVERSE_CURRENT);



/* center the window using Xinerama */
	Display *display = XtDisplay(topLevel);
	Window window = XtWindow(topLevel);

	Display *dpy = XtDisplay(topLevel);
	Window win = XtWindow(topLevel);

	XcursorImage *image = XcursorLibraryLoadImage("left_ptr", NULL, 24);
	if(image)
	{
		Cursor cursor = XcursorImageLoadCursor(dpy, image);
		XDefineCursor(dpy, win, cursor);
		XFreeCursor(dpy, cursor);
		XcursorImageDestroy(image);
	}


	/* check for xinerama extension info */
	int event_base, error_base;
	if(!XineramaQueryExtension(display, &event_base, &error_base))
	{
		fprintf(stderr, "dvlogin: Xinerama not available, defaulting to screen 0.\n");
		XMoveWindow(display, window, 0, 0);
		XtAppMainLoop(app);
		XtDestroyApplicationContext(app);
		return;
	}

	/* get screen count */
	int screen_count;
	XineramaScreenInfo *screen_info = XineramaQueryScreens(display, &screen_count);
	if(!screen_info || screen_count == 0)
	{
		fprintf(stderr, "dvlogin: failed to retrieve Xinerama screen info.\n");
		XtAppMainLoop(app);
		XtDestroyApplicationContext(app);
		return;
	}

	/* get window attributes like height and width */
	XWindowAttributes windowAttr;
	XGetWindowAttributes(display, window, &windowAttr);
	int windowWidth = windowAttr.width;
	int windowHeight = windowAttr.height;

	if(screen_count > 0)
	{
		RootWindow(display, screen_info[0].screen_number);

		/* take height/width of window, add screen H/W, then divide by two */
		int newX = screen_info[0].x_org + (screen_info[0].width - windowWidth) / 2;
		int newY = screen_info[0].y_org + (screen_info[0].height - windowHeight) / 2;

		/* finally move window to center */
		XMoveWindow(display, window, newX, newY);
	}
	else
	{
		fprintf(stderr, "dvlogin: Xinerama not available, defaulting to screen 0.\n");
		XMoveWindow(display, window, 0, 0);
	}

	XFree(screen_info);

	XtAppMainLoop(app);

	XtDestroyApplicationContext(app);
}
