/*
 * clogin_features.h - lightweight IRIX clogin-style greeter behavior
 *
 * This layer deliberately leaves the existing SGI-ish Motif layout alone.
 * It adds the small behavioral pieces described by IRIX clogin(1):
 *
 *   - icon list built from login-capable NSS users
 *   - users without an existing home directory are omitted
 *   - per-user face lookup in the historical clogin search order
 *   - single click fills the login name; double click acts like Enter
 *   - password prompt stays hidden until a login name is selected/typed
 *
 * Face files use XPM/XBM rather than SGI image-library RGB files.  The
 * historical path names are still checked first, so an XPM stored as
 * login.icon or /usr/local/lib/faces/$USER works without renaming it.
 */

#ifndef DVLOGIN_CLOGIN_FEATURES_H
#define DVLOGIN_CLOGIN_FEATURES_H

#define CLOGIN_FACE_PATH_MAX 1024


typedef struct
{
	char username[MAX_USERNAME_LEN + 1];
	Time last_click_time;
} CloginShortcutData;


static void clogin_set_password_visible(Boolean visible, Boolean focus)
{
	if(password != NULL)
	{
		XtVaSetValues(password, XmNmappedWhenManaged, visible, NULL);
	}
	if(textBox2 != NULL)
	{
		XtVaSetValues(textBox2, XmNmappedWhenManaged, visible, NULL);
	}

	if(visible && focus && textBox2 != NULL)
	{
		XmProcessTraversal(textBox2, XmTRAVERSE_CURRENT);
	}
}


static void clogin_username_changed_cb(Widget w, XtPointer client_data,
	XtPointer call_data)
{
	char *name;

	name = XmTextGetString(w);
	if(name != NULL && name[0] != '\0')
	{
		clogin_set_password_visible(True, False);
	}
	else
	{
		clear_password_field();
		clogin_set_password_visible(False, False);
	}

	if(name != NULL)
	{
		XtFree(name);
	}
}


static void clogin_text_input_activated_cb(Widget w, XtPointer client_data,
	XtPointer call_data)
{
	char *name;

	if(strcmp(XtName(w), "textBox1") != 0)
	{
		textInputActivatedCB(w, client_data, call_data);
		return;
	}

	name = XmTextGetString(textBox1);
	if(name != NULL && name[0] != '\0')
	{
		clogin_set_password_visible(True, True);
	}
	else
	{
		textInputActivatedCB(w, client_data, call_data);
	}

	if(name != NULL)
	{
		XtFree(name);
	}
}


static void clogin_login_button_cb(Widget w, XtPointer client_data,
	XtPointer call_data)
{
	char *name;
	Boolean mapped;

	mapped = False;
	XtVaGetValues(textBox2, XmNmappedWhenManaged, &mapped, NULL);

	if(!mapped)
	{
		name = XmTextGetString(textBox1);
		if(name != NULL && name[0] != '\0')
		{
			clogin_set_password_visible(True, True);
			XtFree(name);
			return;
		}
		if(name != NULL)
		{
			XtFree(name);
		}
	}

	loginButtonCB(w, client_data, call_data);
}


static int clogin_shell_is_login_capable(const char *shell)
{
	if(shell == NULL || shell[0] == '\0')
	{
		return 1;
	}

	if(strcmp(shell, "/bin/false") == 0 ||
		strcmp(shell, "/usr/bin/false") == 0 ||
		strcmp(shell, "/sbin/nologin") == 0 ||
		strcmp(shell, "/usr/sbin/nologin") == 0)
	{
		return 0;
	}

	return 1;
}


static int clogin_home_exists(const char *home)
{
	struct stat st;

	if(home == NULL || home[0] == '\0')
	{
		return 0;
	}

	if(stat(home, &st) != 0)
	{
		return 0;
	}

	return S_ISDIR(st.st_mode) ? 1 : 0;
}


static uid_t clogin_min_uid(void)
{
	const char *value;
	char *end;
	unsigned long parsed;

	value = getenv("DVLOGIN_MIN_UID");
	if(value == NULL || value[0] == '\0')
	{
		return (uid_t)1000;
	}

	errno = 0;
	end = NULL;
	parsed = strtoul(value, &end, 10);
	if(errno != 0 || end == value || *end != '\0')
	{
		return (uid_t)1000;
	}

	return (uid_t)parsed;
}


static int clogin_try_face_path(char *out, size_t out_size, const char *path)
{
	if(path == NULL || path[0] == '\0')
	{
		return 0;
	}

	if(access(path, R_OK) != 0)
	{
		return 0;
	}

	strncpy(out, path, out_size - 1);
	out[out_size - 1] = '\0';
	return 1;
}


/*
 * IRIX clogin search order:
 *   $HOME/.icons/login.icon
 *   /usr/local/lib/faces/$USER
 *   /usr/lib/faces/$USER
 *
 * Extension variants are accepted as a convenience on modern systems.
 */
static int clogin_find_face(const struct passwd *pw, char *out,
	size_t out_size)
{
	char path[CLOGIN_FACE_PATH_MAX];
	const char *suffixes[] = { "", ".xpm", ".xbm", NULL };
	int i;

	if(pw == NULL || pw->pw_name == NULL || pw->pw_dir == NULL)
	{
		return 0;
	}

	snprintf(path, sizeof(path), "%s/.icons/login.icon", pw->pw_dir);
	if(clogin_try_face_path(out, out_size, path))
	{
		return 1;
	}

	snprintf(path, sizeof(path), "%s/.icons/login.xpm", pw->pw_dir);
	if(clogin_try_face_path(out, out_size, path))
	{
		return 1;
	}

	snprintf(path, sizeof(path), "%s/.icons/login.xbm", pw->pw_dir);
	if(clogin_try_face_path(out, out_size, path))
	{
		return 1;
	}

	for(i = 0; suffixes[i] != NULL; i++)
	{
		snprintf(path, sizeof(path), "/usr/local/lib/faces/%s%s",
			pw->pw_name, suffixes[i]);
		if(clogin_try_face_path(out, out_size, path))
		{
			return 1;
		}
	}

	for(i = 0; suffixes[i] != NULL; i++)
	{
		snprintf(path, sizeof(path), "/usr/lib/faces/%s%s",
			pw->pw_name, suffixes[i]);
		if(clogin_try_face_path(out, out_size, path))
		{
			return 1;
		}
	}

	return 0;
}


static Pixmap clogin_load_face_pixmap(Display *dpy, Drawable drawable,
	const char *path, Pixmap *mask_out)
{
	Pixmap pixmap;
	XpmAttributes attrs;
	unsigned int width;
	unsigned int height;
	int xhot;
	int yhot;

	pixmap = None;
	attrs.valuemask = 0;

	if(path == NULL)
	{
		return None;
	}

	if(XpmReadFileToPixmap(dpy, drawable, (char *)path,
		&pixmap, mask_out, &attrs) == XpmSuccess)
	{
		return pixmap;
	}

	if(XReadBitmapFile(dpy, drawable, path, &width, &height,
		&pixmap, &xhot, &yhot) == BitmapSuccess)
	{
		if(mask_out != NULL)
		{
			*mask_out = None;
		}
		return pixmap;
	}

	return None;
}


static void clogin_shortcut_click_eh(Widget w, XtPointer client_data,
	XEvent *event, Boolean *cont)
{
	CloginShortcutData *data;
	Time now;
	Time delta;
	int multi_click;

	if(event == NULL || event->type != ButtonPress ||
		event->xbutton.button != Button1)
	{
		return;
	}

	data = (CloginShortcutData *)client_data;
	if(data == NULL)
	{
		return;
	}

	now = event->xbutton.time;
	multi_click = XtGetMultiClickTime(XtDisplay(w));
	delta = (data->last_click_time == 0) ? (Time)(multi_click + 1) :
		(now - data->last_click_time);
	data->last_click_time = now;

	XmTextSetString(textBox1, data->username);
	clear_password_field();
	clogin_set_password_visible(True, False);

	if(delta <= (Time)multi_click)
	{
		/* clogin(1): double-click is equivalent to name + Enter. */
		XmProcessTraversal(textBox2, XmTRAVERSE_CURRENT);
	}
	else
	{
		XmProcessTraversal(textBox1, XmTRAVERSE_CURRENT);
		XmTextSetInsertionPosition(textBox1, XmTextGetLastPosition(textBox1));
	}
}


static void clogin_make_shortcut_interactive(Widget w,
	CloginShortcutData *data)
{
	XtAddEventHandler(w, ButtonPressMask, False,
		clogin_shortcut_click_eh, (XtPointer)data);
	XtAddEventHandler(w, EnterWindowMask, False,
		mouseOverPushButton, (XtPointer)w);
	XtAddEventHandler(w, LeaveWindowMask, False,
		mouseLeftPushButton, (XtPointer)w);
}


static void clogin_add_shortcut(Widget container, const struct passwd *pw,
	const char *shirt_color)
{
	Display *dpy;
	Drawable root;
	Pixmap icon;
	Pixmap mask;
	Pixmap flat;
	Widget rc;
	Widget tile;
	Widget icon_widget;
	Widget label_widget;
	Pixel bg_pixel;
	CloginShortcutData *data;
	char face[CLOGIN_FACE_PATH_MAX];
	char widget_name[64];
	char icon_name[72];
	char label_name[72];
	static int next_id = 0;

	if(pw == NULL || pw->pw_name == NULL)
	{
		return;
	}

	dpy = XtDisplay(container);
	root = RootWindowOfScreen(XtScreen(container));
	icon = None;
	mask = None;
	face[0] = '\0';

	if(clogin_find_face(pw, face, sizeof(face)))
	{
		icon = clogin_load_face_pixmap(dpy, root, face, &mask);
	}

	if(icon == None)
	{
		icon = create_default_user_pixmap(dpy, root, shirt_color, &mask);
	}

	rc = find_or_create_shortcut_rc(container);
	if(icon != None && mask != None)
	{
		XtVaGetValues(rc, XmNbackground, &bg_pixel, NULL);
		flat = composite_with_background(dpy, root, icon, mask, bg_pixel);
		XFreePixmap(dpy, icon);
		XFreePixmap(dpy, mask);
		icon = flat;
		mask = None;
	}

	snprintf(widget_name, sizeof(widget_name), "cloginShortcut%d", next_id++);
	tile = XtVaCreateManagedWidget(widget_name,
		xmRowColumnWidgetClass, rc,
		XmNorientation, XmVERTICAL,
		XmNpacking, XmPACK_TIGHT,
		XmNspacing, 2,
		XmNmarginHeight, 6,
		XmNmarginWidth, 8,
		XmNtraversalOn, 0,
		XmNentryAlignment, XmALIGNMENT_CENTER,
		XtVaTypedArg, XmNbackground, XmRString, SHORTCUT_BG, 8,
	NULL);

	snprintf(icon_name, sizeof(icon_name), "%sIcon", widget_name);
	icon_widget = XtVaCreateManagedWidget(icon_name,
		xmLabelWidgetClass, tile,
		XmNlabelType, XmPIXMAP,
		XmNlabelPixmap, icon,
		XmNmarginWidth, 0,
		XmNmarginHeight, 0,
		XmNhighlightThickness, 0,
		XmNshadowThickness, 0,
		XtVaTypedArg, XmNbackground, XmRString, SHORTCUT_BG, 8,
	NULL);

	snprintf(label_name, sizeof(label_name), "%sLabel", widget_name);
	label_widget = XtVaCreateManagedWidget(label_name,
		xmLabelWidgetClass, tile,
		XmNmarginWidth, 1,
		XmNmarginHeight, 4,
		XmNhighlightThickness, 0,
		XmNshadowThickness, 0,
		XmNalignment, XmALIGNMENT_CENTER,
		XtVaTypedArg, XmNbackground, XmRString, SHORTCUT_BG, 8,
		XtVaTypedArg, XmNforeground, XmRString, SHORTCUT_FG, 8,
		XtVaTypedArg, XmNlabelString, XmRString,
			pw->pw_name, strlen(pw->pw_name) + 1,
	NULL);

	data = (CloginShortcutData *)calloc(1, sizeof(CloginShortcutData));
	if(data == NULL)
	{
		return;
	}
	strncpy(data->username, pw->pw_name, MAX_USERNAME_LEN);
	data->username[MAX_USERNAME_LEN] = '\0';

	/* Existing sorter expects ShortcutCBData layout beginning with username. */
	XtVaSetValues(tile, XmNuserData, (XtPointer)data, NULL);

	clogin_make_shortcut_interactive(tile, data);
	clogin_make_shortcut_interactive(icon_widget, data);
	clogin_make_shortcut_interactive(label_widget, data);
}


static void clogin_add_shortcuts(Widget container)
{
	struct passwd *pw;
	uid_t min_uid;

	min_uid = clogin_min_uid();
	setpwent();
	while((pw = getpwent()) != NULL)
	{
		if(pw->pw_name == NULL || pw->pw_name[0] == '\0')
		{
			continue;
		}

		if(strcmp(pw->pw_name, "root") == 0)
		{
			if(enableRootShortcut && clogin_home_exists(pw->pw_dir) &&
				clogin_shell_is_login_capable(pw->pw_shell))
			{
				clogin_add_shortcut(container, pw, SHIRT_COLOR_ROOT);
			}
			continue;
		}

		if(pw->pw_uid < min_uid)
		{
			continue;
		}
		if(!clogin_shell_is_login_capable(pw->pw_shell))
		{
			continue;
		}
		if(!clogin_home_exists(pw->pw_dir))
		{
			continue;
		}

		clogin_add_shortcut(container, pw, SHIRT_COLOR_USER);
	}
	endpwent();
}


/*
 * gui.h creates all widgets before calling XtRealizeWidget.  Hook that one
 * point so the password row starts hidden and username typing can reveal it.
 */
static void clogin_realize_widget(Widget w)
{
	clogin_set_password_visible(False, False);
	XtAddCallback(textBox1, XmNvalueChangedCallback,
		clogin_username_changed_cb, NULL);
	XtRealizeWidget(w);
}


/* Rewrite only gui.h's references; main.c undefines these immediately after. */
#define addCustomShortcuts clogin_add_shortcuts
#define loginButtonCB clogin_login_button_cb
#define textInputActivatedCB clogin_text_input_activated_cb
#define XtRealizeWidget(w) clogin_realize_widget((w))

#endif /* DVLOGIN_CLOGIN_FEATURES_H */
