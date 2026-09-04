/*
 * clogin_ui.h - portable presentation layer for recovered IRIX clogin states.
 *
 * The original PandoraComponent has separate displayIcon() and showPicture()
 * presentation paths.  This layer preserves that boundary using Motif/Xpm
 * while the full SGI Imd/Sg rendering stack is still being reconstructed.
 */
#ifndef DVLOGIN_CLOGIN_UI_H
#define DVLOGIN_CLOGIN_UI_H

static Widget g_clogin_icon_view = NULL;
static Widget g_clogin_picture_view = NULL;
static Pixmap g_clogin_picture_pixmap = None;

static void clogin_ui_release_picture(void)
{
	if(g_clogin_picture_view != NULL)
	{
		XtDestroyWidget(g_clogin_picture_view);
		g_clogin_picture_view = NULL;
	}

	if(g_clogin_picture_pixmap != None && scrolledWin0 != NULL)
	{
		XFreePixmap(XtDisplay(scrolledWin0), g_clogin_picture_pixmap);
		g_clogin_picture_pixmap = None;
	}
}

static void clogin_ui_show_icon_view(void)
{
	if(scrolledWin0 == NULL || g_clogin_icon_view == NULL)
		return;

	clogin_ui_release_picture();
	XtVaSetValues(scrolledWin0, XmNworkWindow, g_clogin_icon_view, NULL);
	XtManageChild(g_clogin_icon_view);
}

static int clogin_ui_show_picture(const char *username_value)
{
	struct passwd *pw;
	Display *dpy;
	Drawable root;
	Pixmap mask = None;
	Pixmap pixmap = None;
	Pixel background;
	char face[CLOGIN_FACE_PATH_MAX];
	Widget picture;
	Widget name_label;

	if(scrolledWin0 == NULL || username_value == NULL || username_value[0] == '\0')
		return 0;

	pw = getpwnam(username_value);
	if(pw == NULL || !clogin_user_eligible(pw))
		return 0;

	dpy = XtDisplay(scrolledWin0);
	root = RootWindowOfScreen(XtScreen(scrolledWin0));
	face[0] = '\0';

	if(clogin_find_face(pw, face, sizeof(face)))
		pixmap = clogin_load_face_pixmap(dpy, root, face, &mask);

	if(pixmap == None)
	{
		pixmap = create_default_user_pixmap(dpy, root,
			strcmp(pw->pw_name, "root") == 0 ? SHIRT_COLOR_ROOT : SHIRT_COLOR_USER,
			&mask);
	}

	if(pixmap == None)
		return 0;

	if(mask != None)
	{
		Pixmap flat;
		XtVaGetValues(scrolledWin0, XmNbackground, &background, NULL);
		flat = composite_with_background(dpy, root, pixmap, mask, background);
		XFreePixmap(dpy, pixmap);
		XFreePixmap(dpy, mask);
		pixmap = flat;
	}

	if(pixmap == None)
		return 0;

	clogin_ui_release_picture();
	if(g_clogin_icon_view != NULL)
		XtUnmanageChild(g_clogin_icon_view);

	g_clogin_picture_pixmap = pixmap;
	g_clogin_picture_view = XtVaCreateWidget("cloginPictureView",
		xmRowColumnWidgetClass, scrolledWin0,
		XmNorientation, XmVERTICAL,
		XmNpacking, XmPACK_TIGHT,
		XmNentryAlignment, XmALIGNMENT_CENTER,
		XmNalignment, XmALIGNMENT_CENTER,
		XmNspacing, 10,
		XmNmarginWidth, 18,
		XmNmarginHeight, 18,
		XmNtraversalOn, 0,
		XtVaTypedArg, XmNbackground, XmRString, SHORTCUT_BG, 8,
		NULL);

	picture = XtVaCreateManagedWidget("selectedUserPicture",
		xmLabelWidgetClass, g_clogin_picture_view,
		XmNlabelType, XmPIXMAP,
		XmNlabelPixmap, g_clogin_picture_pixmap,
		XmNmarginWidth, 0,
		XmNmarginHeight, 0,
		XmNhighlightThickness, 0,
		XmNshadowThickness, 0,
		XtVaTypedArg, XmNbackground, XmRString, SHORTCUT_BG, 8,
		NULL);
	(void)picture;

	name_label = XtVaCreateManagedWidget("selectedUserName",
		xmLabelWidgetClass, g_clogin_picture_view,
		XmNalignment, XmALIGNMENT_CENTER,
		XmNmarginWidth, 4,
		XmNmarginHeight, 4,
		XtVaTypedArg, XmNbackground, XmRString, SHORTCUT_BG, 8,
		XtVaTypedArg, XmNforeground, XmRString, SHORTCUT_FG, 8,
		XtVaTypedArg, XmNlabelString, XmRString,
			pw->pw_name, strlen(pw->pw_name) + 1,
		NULL);
	(void)name_label;

	XtVaSetValues(scrolledWin0, XmNworkWindow, g_clogin_picture_view, NULL);
	XtManageChild(g_clogin_picture_view);
	return 1;
}

static void clogin_ui_username_changed_cb(Widget w, XtPointer client_data,
	XtPointer call_data)
{
	char *name;
	(void)client_data;
	(void)call_data;

	name = XmTextGetString(w);
	if(name == NULL || name[0] == '\0')
	{
		clogin_ui_show_icon_view();
	}
	else
	{
		(void)clogin_ui_show_picture(name);
	}

	if(name != NULL)
		XtFree(name);
}

static void clogin_ui_realize_widget(Widget w)
{
	/* Retain the existing reconstructed behavior first. */
	clogin_realize_widget(w);

	/* Capture the model/list presentation created by addCustomShortcuts(). */
	if(scrolledWin0 != NULL)
	{
		XtVaGetValues(scrolledWin0, XmNworkWindow, &g_clogin_icon_view, NULL);
	}

	if(textBox1 != NULL)
	{
		XtAddCallback(textBox1, XmNvalueChangedCallback,
			clogin_ui_username_changed_cb, NULL);
	}
}

/* Override only the realize hook supplied by clogin_reconstructed.h. */
#undef XtRealizeWidget
#define XtRealizeWidget(w) clogin_ui_realize_widget((w))

#endif /* DVLOGIN_CLOGIN_UI_H */
