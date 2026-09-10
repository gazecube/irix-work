/*
 * clogin_reconstructed.h - C bridge from recovered IRIX clogin behavior
 * into the existing Motif dvlogin prototype.
 *
 * This file is integration glue.  The evidence-oriented C++ reconstruction
 * under irix-re/clogin remains the source of truth for recovered behavior.
 */
#ifndef DVLOGIN_CLOGIN_RECONSTRUCTED_H
#define DVLOGIN_CLOGIN_RECONSTRUCTED_H

#include <sys/types.h>
#include <sys/stat.h>
#ifdef __linux__
#include <sys/vfs.h>
#include <linux/magic.h>
#endif

#define CLOGIN_FACE_PATH_MAX 1024
#define CLOGIN_CONF_DEFAULT "/var/sysadm/config/clogin.conf"
#define CLOGIN_MAX_POLICY_ENTRIES 256
#define CLOGIN_MAX_IGNORE_ENTRIES 32

#define CLOGIN_FLAG_NOSHOW      0x01
#define CLOGIN_FLAG_NOICON      0x02
#define CLOGIN_FLAG_NODELETE    0x04
#define CLOGIN_FLAG_MACHINEICON 0x08
#define CLOGIN_FLAG_SHOW        0x10

typedef struct
{
	char name[MAX_USERNAME_LEN + 1];
	unsigned int flags;
} CloginPolicyEntry;

typedef struct
{
	int how_display;   /* 0 = faces, 1 = icons */
	int where_icons;   /* 0 = locally, 1 = anywhere */
	int where_home;    /* 0 = local only, 1 = anywhere */
	int window_size;   /* 0 = small, 1 = large */
	CloginPolicyEntry entries[CLOGIN_MAX_POLICY_ENTRIES];
	size_t entry_count;
	char ignore[CLOGIN_MAX_IGNORE_ENTRIES][MAXPATHLEN];
	size_t ignore_count;
} CloginPolicy;

typedef struct
{
	char username[MAX_USERNAME_LEN + 1];
	Time last_click_time;
} CloginShortcutData;

typedef enum
{
	CLOGIN_DIR_LOCAL = 0,
	CLOGIN_DIR_NFS = 1,
	CLOGIN_DIR_OTHER = 2,
	CLOGIN_DIR_MISSING = 3
} CloginDirectoryState;

static CloginPolicy g_clogin_policy;
static int g_clogin_policy_loaded = 0;

static void clogin_policy_defaults(CloginPolicy *policy)
{
	memset(policy, 0, sizeof(*policy));
	/* Defaults recovered from PwdNameList constructor. */
	policy->how_display = 0;
	policy->where_icons = 1;
	policy->where_home = 1;
	policy->window_size = 0;
}

static char *clogin_trim(char *s)
{
	char *end;
	while(*s != '\0' && isspace((unsigned char)*s)) { ++s; }
	end = s + strlen(s);
	while(end > s && isspace((unsigned char)end[-1])) { --end; }
	*end = '\0';
	return s;
}

static CloginPolicyEntry *clogin_policy_add_entry(CloginPolicy *policy,
	const char *name)
{
	CloginPolicyEntry *entry;
	if(policy->entry_count >= CLOGIN_MAX_POLICY_ENTRIES) { return NULL; }
	entry = &policy->entries[policy->entry_count++];
	memset(entry, 0, sizeof(*entry));
	strncpy(entry->name, name, MAX_USERNAME_LEN);
	entry->name[MAX_USERNAME_LEN] = '\0';
	return entry;
}

static void clogin_policy_parse(CloginPolicy *policy, const char *path)
{
	FILE *fp;
	char line[2048];

	clogin_policy_defaults(policy);
	fp = fopen(path, "r");
	if(fp == NULL) { return; }

	while(fgets(line, sizeof(line), fp) != NULL)
	{
		char *text;
		char *save;
		char *field;
		CloginPolicyEntry *entry;

		text = clogin_trim(line);
		if(text[0] == '\0' || text[0] == '#') { continue; }

		if(strcmp(text, "display faces") == 0) { policy->how_display = 0; continue; }
		if(strcmp(text, "display icons") == 0) { policy->how_display = 1; continue; }
		if(strcmp(text, "look for pictures locally") == 0) { policy->where_icons = 0; continue; }
		if(strcmp(text, "look for pictures anywhere") == 0) { policy->where_icons = 1; continue; }
		if(strcmp(text, "home directory must be local") == 0) { policy->where_home = 0; continue; }
		if(strcmp(text, "home directory can be anywhere") == 0) { policy->where_home = 1; continue; }
		if(strcmp(text, "use small window") == 0) { policy->window_size = 0; continue; }
		if(strcmp(text, "use large window") == 0) { policy->window_size = 1; continue; }

		save = NULL;
		field = strtok_r(text, ":", &save);
		if(field == NULL || field[0] == '\0') { continue; }

		if(field[0] == '/')
		{
			char *attr = strtok_r(NULL, ":", &save);
			if(attr != NULL && strcmp(attr, "ignore") == 0 &&
				policy->ignore_count < CLOGIN_MAX_IGNORE_ENTRIES)
			{
				strncpy(policy->ignore[policy->ignore_count], field, MAXPATHLEN - 1);
				policy->ignore[policy->ignore_count][MAXPATHLEN - 1] = '\0';
				policy->ignore_count++;
			}
			continue;
		}

		entry = clogin_policy_add_entry(policy, field);
		if(entry == NULL) { continue; }
		while((field = strtok_r(NULL, ":", &save)) != NULL)
		{
			if(strcmp(field, "noshow") == 0) entry->flags |= CLOGIN_FLAG_NOSHOW;
			else if(strcmp(field, "noicon") == 0) entry->flags |= CLOGIN_FLAG_NOICON;
			else if(strcmp(field, "nodelete") == 0) entry->flags |= CLOGIN_FLAG_NODELETE;
			else if(strcmp(field, "machineicon") == 0) entry->flags |= CLOGIN_FLAG_MACHINEICON;
			else if(strcmp(field, "show") == 0) entry->flags |= CLOGIN_FLAG_SHOW;
		}
	}
	fclose(fp);
}

static void clogin_load_policy(void)
{
	const char *path;
	if(g_clogin_policy_loaded) { return; }
	path = getenv("CLOGIN_CONF");
	if(path == NULL || path[0] == '\0') { path = CLOGIN_CONF_DEFAULT; }
	clogin_policy_parse(&g_clogin_policy, path);
	g_clogin_policy_loaded = 1;
}

static const CloginPolicyEntry *clogin_policy_find(const char *name)
{
	size_t i;
	clogin_load_policy();
	for(i = 0; i < g_clogin_policy.entry_count; ++i)
	{
		if(strcmp(g_clogin_policy.entries[i].name, name) == 0)
			return &g_clogin_policy.entries[i];
	}
	return NULL;
}

static int clogin_path_is_ignored(const char *path)
{
	size_t i;
	clogin_load_policy();
	if(path == NULL) { return 0; }
	for(i = 0; i < g_clogin_policy.ignore_count; ++i)
	{
		const char *prefix = g_clogin_policy.ignore[i];
		size_t n = strlen(prefix);
		if(strncmp(path, prefix, n) == 0 &&
			(path[n] == '\0' || path[n] == '/'))
			return 1;
	}
	return 0;
}

static CloginDirectoryState clogin_check_directory(const char *path)
{
	struct stat st;
	CloginDirectoryState state = CLOGIN_DIR_OTHER;

	/* Original checkDirectory() returns 2 for NULL/non-absolute paths and
	 * explicitly treats /dev/null as the same unclassified state. */
	if(path == NULL || path[0] != '/') return CLOGIN_DIR_OTHER;
	if(strcmp(path, "/dev/null") == 0) return CLOGIN_DIR_OTHER;

#ifdef __linux__
	{
		struct statfs fs;
		if(statfs(path, &fs) == 0)
		{
			if((unsigned long)fs.f_type == (unsigned long)NFS_SUPER_MAGIC)
				state = CLOGIN_DIR_NFS;
			else
				state = CLOGIN_DIR_OTHER;
		}
	}
#endif

	/* The original validates classified states other than OTHER with
	 * stat(); failure or non-directory becomes state 3. */
	if(state != CLOGIN_DIR_OTHER)
	{
		if(stat(path, &st) != 0 || !S_ISDIR(st.st_mode))
			return CLOGIN_DIR_MISSING;
	}
	return state;
}
static int clogin_user_eligible(const struct passwd *pw)
{
	const CloginPolicyEntry *entry;
	CloginDirectoryState state;

	if(pw == NULL || pw->pw_name == NULL || pw->pw_name[0] == '\0') return 0;
	if(pw->pw_dir == NULL || pw->pw_dir[0] == '\0') return 0;
	if(clogin_path_is_ignored(pw->pw_dir)) return 0;

	entry = clogin_policy_find(pw->pw_name);
	if(entry != NULL && (entry->flags & (CLOGIN_FLAG_NOSHOW | CLOGIN_FLAG_NOICON)))
		return 0;

	state = clogin_check_directory(pw->pw_dir);
	if(state == CLOGIN_DIR_MISSING) return 0;

	/* Binary evidence from pandora::createIconList(): state 2 is not a
	 * locality rejection.  The original performs an ordinary stat() and
	 * accepts the account when the target exists and is a directory.
	 * States 0 (local) and 1 (NFS) continue without this fallback. */
	if(state == CLOGIN_DIR_OTHER)
	{
		struct stat st;
		if(stat(pw->pw_dir, &st) != 0 || !S_ISDIR(st.st_mode)) return 0;
	}

	/* getWhereHome()==0 affects passwd/NIS enumeration in the original
	 * createIconList() via _getpwent_no_yp; it is not a post-enumeration
	 * checkDirectory()==LOCAL filter. */
	return 1;
}

static void clogin_set_password_visible(Boolean visible, Boolean focus)
{
	if(password != NULL) XtVaSetValues(password, XmNmappedWhenManaged, visible, NULL);
	if(textBox2 != NULL) XtVaSetValues(textBox2, XmNmappedWhenManaged, visible, NULL);
	if(visible && focus && textBox2 != NULL)
		XmProcessTraversal(textBox2, XmTRAVERSE_CURRENT);
}

static void clogin_username_changed_cb(Widget w, XtPointer client_data,
	XtPointer call_data)
{
	char *name = XmTextGetString(w);
	(void)client_data; (void)call_data;
	if(name != NULL && name[0] != '\0') clogin_set_password_visible(True, False);
	else { clear_password_field(); clogin_set_password_visible(False, False); }
	if(name != NULL) XtFree(name);
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
	if(name != NULL && name[0] != '\0') clogin_set_password_visible(True, True);
	else textInputActivatedCB(w, client_data, call_data);
	if(name != NULL) XtFree(name);
}

static void clogin_login_button_cb(Widget w, XtPointer client_data,
	XtPointer call_data)
{
	char *name;
	Boolean mapped = False;
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
		if(name != NULL) XtFree(name);
	}
	loginButtonCB(w, client_data, call_data);
}

static int clogin_try_face_path(char *out, size_t out_size, const char *path)
{
	if(path == NULL || path[0] == '\0' || access(path, R_OK) != 0) return 0;
	strncpy(out, path, out_size - 1);
	out[out_size - 1] = '\0';
	return 1;
}

static int clogin_find_face(const struct passwd *pw, char *out, size_t out_size)
{
	char path[CLOGIN_FACE_PATH_MAX];
	const CloginPolicyEntry *entry;
	int built_in;
	CloginDirectoryState home_state;

	if(pw == NULL || pw->pw_name == NULL || pw->pw_dir == NULL) return 0;
	clogin_load_policy();

	/* Exact recovered gate: when pictures are not allowed anywhere,
	 * checkDirectory(home) must succeed before trying the home icon. */
	if(g_clogin_policy.where_icons != 1)
	{
		home_state = clogin_check_directory(pw->pw_dir);
		/* findUserPicture() requires checkDirectory(home)==0 exactly in
		 * local-picture mode; unlike createIconList(), state 2 has no stat
		 * fallback here. */
		if(home_state != CLOGIN_DIR_LOCAL) return 0;
	}

	snprintf(path, sizeof(path), "%s/.icons/login.icon", pw->pw_dir);
	if(clogin_try_face_path(out, out_size, path)) return 1;

	if(g_clogin_policy.where_icons == 1 || g_clogin_policy.how_display != 0)
	{
		snprintf(path, sizeof(path), "/usr/local/lib/faces/%s", pw->pw_name);
		if(clogin_try_face_path(out, out_size, path)) return 1;
	}

	if(g_clogin_policy.where_icons == 1)
	{
		snprintf(path, sizeof(path), "/usr/lib/faces/%s", pw->pw_name);
		if(clogin_try_face_path(out, out_size, path)) return 1;
	}

	entry = clogin_policy_find(pw->pw_name);
	built_in = (entry != NULL); /* exact isBuiltInName() behavior */
	snprintf(path, sizeof(path), "/usr/local/lib/faces/%s",
		built_in ? "machineUserIcon" : "userIcon");
	return clogin_try_face_path(out, out_size, path);
}

static Pixmap clogin_load_face_pixmap(Display *dpy, Drawable drawable,
	const char *path, Pixmap *mask_out)
{
	Pixmap pixmap = None;
	XpmAttributes attrs;
	unsigned int width, height;
	int xhot, yhot;
	attrs.valuemask = 0;
	if(path == NULL) return None;
	if(XpmReadFileToPixmap(dpy, drawable, (char *)path, &pixmap, mask_out, &attrs) == XpmSuccess)
		return pixmap;
	if(XReadBitmapFile(dpy, drawable, path, &width, &height, &pixmap, &xhot, &yhot) == BitmapSuccess)
	{
		if(mask_out != NULL) *mask_out = None;
		return pixmap;
	}
	return None;
}

static void clogin_shortcut_click_eh(Widget w, XtPointer client_data,
	XEvent *event, Boolean *cont)
{
	CloginShortcutData *data;
	Time now, delta;
	int multi_click;
	(void)cont;
	if(event == NULL || event->type != ButtonPress || event->xbutton.button != Button1) return;
	data = (CloginShortcutData *)client_data;
	if(data == NULL) return;
	now = event->xbutton.time;
	multi_click = XtGetMultiClickTime(XtDisplay(w));
	delta = data->last_click_time == 0 ? (Time)(multi_click + 1) : now - data->last_click_time;
	data->last_click_time = now;
	XmTextSetString(textBox1, data->username);
	clear_password_field();
	clogin_set_password_visible(True, False);
	if(delta <= (Time)multi_click) XmProcessTraversal(textBox2, XmTRAVERSE_CURRENT);
	else
	{
		XmProcessTraversal(textBox1, XmTRAVERSE_CURRENT);
		XmTextSetInsertionPosition(textBox1, XmTextGetLastPosition(textBox1));
	}
}

static void clogin_make_shortcut_interactive(Widget w, CloginShortcutData *data)
{
	XtAddEventHandler(w, ButtonPressMask, False, clogin_shortcut_click_eh, (XtPointer)data);
	XtAddEventHandler(w, EnterWindowMask, False, mouseOverPushButton, (XtPointer)w);
	XtAddEventHandler(w, LeaveWindowMask, False, mouseLeftPushButton, (XtPointer)w);
}

static void clogin_add_shortcut(Widget container, const struct passwd *pw,
	const char *shirt_color)
{
	Display *dpy;
	Drawable root;
	Pixmap icon = None, mask = None, flat;
	Widget rc, tile, icon_widget, label_widget;
	Pixel bg_pixel;
	CloginShortcutData *data;
	char face[CLOGIN_FACE_PATH_MAX];
	char widget_name[64], icon_name[72], label_name[72];
	static int next_id = 0;

	if(pw == NULL || pw->pw_name == NULL) return;
	dpy = XtDisplay(container);
	root = RootWindowOfScreen(XtScreen(container));
	face[0] = '\0';
	if(clogin_find_face(pw, face, sizeof(face))) icon = clogin_load_face_pixmap(dpy, root, face, &mask);
	if(icon == None) icon = create_default_user_pixmap(dpy, root, shirt_color, &mask);

	rc = find_or_create_shortcut_rc(container);
	if(icon != None && mask != None)
	{
		XtVaGetValues(rc, XmNbackground, &bg_pixel, NULL);
		flat = composite_with_background(dpy, root, icon, mask, bg_pixel);
		XFreePixmap(dpy, icon); XFreePixmap(dpy, mask);
		icon = flat; mask = None;
	}

	snprintf(widget_name, sizeof(widget_name), "cloginShortcut%d", next_id++);
	tile = XtVaCreateManagedWidget(widget_name, xmRowColumnWidgetClass, rc,
		XmNorientation, XmVERTICAL, XmNpacking, XmPACK_TIGHT, XmNspacing, 2,
		XmNmarginHeight, 6, XmNmarginWidth, 8, XmNtraversalOn, 0,
		XmNentryAlignment, XmALIGNMENT_CENTER,
		XtVaTypedArg, XmNbackground, XmRString, SHORTCUT_BG, 8, NULL);

	snprintf(icon_name, sizeof(icon_name), "%sIcon", widget_name);
	icon_widget = XtVaCreateManagedWidget(icon_name, xmLabelWidgetClass, tile,
		XmNlabelType, XmPIXMAP, XmNlabelPixmap, icon, XmNmarginWidth, 0,
		XmNmarginHeight, 0, XmNhighlightThickness, 0, XmNshadowThickness, 0,
		XtVaTypedArg, XmNbackground, XmRString, SHORTCUT_BG, 8, NULL);

	snprintf(label_name, sizeof(label_name), "%sLabel", widget_name);
	label_widget = XtVaCreateManagedWidget(label_name, xmLabelWidgetClass, tile,
		XmNmarginWidth, 1, XmNmarginHeight, 4, XmNhighlightThickness, 0,
		XmNshadowThickness, 0, XmNalignment, XmALIGNMENT_CENTER,
		XtVaTypedArg, XmNbackground, XmRString, SHORTCUT_BG, 8,
		XtVaTypedArg, XmNforeground, XmRString, SHORTCUT_FG, 8,
		XtVaTypedArg, XmNlabelString, XmRString, pw->pw_name, strlen(pw->pw_name) + 1,
		NULL);

	data = (CloginShortcutData *)calloc(1, sizeof(*data));
	if(data == NULL) return;
	strncpy(data->username, pw->pw_name, MAX_USERNAME_LEN);
	data->username[MAX_USERNAME_LEN] = '\0';
	XtVaSetValues(tile, XmNuserData, (XtPointer)data, NULL);
	clogin_make_shortcut_interactive(tile, data);
	clogin_make_shortcut_interactive(icon_widget, data);
	clogin_make_shortcut_interactive(label_widget, data);
}

typedef struct
{
	struct passwd pw;
	char name[MAX_USERNAME_LEN + 1];
	char dir[MAXPATHLEN];
	char gecos[512];
} CloginPasswdSnapshot;

static void clogin_snapshot_passwd(CloginPasswdSnapshot *dst,
	const struct passwd *src, const char *name_override)
{
	memset(dst, 0, sizeof(*dst));
	dst->pw = *src;
	strncpy(dst->name,
		name_override != NULL ? name_override : (src->pw_name != NULL ? src->pw_name : ""),
		MAX_USERNAME_LEN);
	dst->name[MAX_USERNAME_LEN] = '\0';
	strncpy(dst->dir, src->pw_dir != NULL ? src->pw_dir : "", MAXPATHLEN - 1);
	dst->dir[MAXPATHLEN - 1] = '\0';
	strncpy(dst->gecos, src->pw_gecos != NULL ? src->pw_gecos : "", sizeof(dst->gecos) - 1);
	dst->gecos[sizeof(dst->gecos) - 1] = '\0';
	dst->pw.pw_name = dst->name;
	dst->pw.pw_dir = dst->dir;
	dst->pw.pw_gecos = dst->gecos;
}

static void clogin_restore_passwd_position(size_t seen)
{
	size_t i;
	setpwent();
	for(i = 0; i < seen; ++i)
	{
		if(getpwent() == NULL) break;
	}
}

static void clogin_add_shortcuts(Widget container)
{
	struct passwd *pw;
	size_t seen = 0;

	clogin_load_policy();
	setpwent();
	while((pw = getpwent()) != NULL)
	{
		const struct passwd *candidate = pw;
		CloginPasswdSnapshot snap;
		const char *name;

		++seen;
		if(pw->pw_name == NULL || pw->pw_name[0] == '\0') continue;

		/* Binary-backed createIconList() behavior: a leading '-' record
		 * terminates this enumeration routine. */
		if(pw->pw_name[0] == '-') break;

		name = pw->pw_name[0] == '+' ? pw->pw_name + 1 : pw->pw_name;

		/* Empty-home records are resolved by name.  The original copies the
		 * result before rewinding passwd enumeration to its previous count. */
		if(pw->pw_dir == NULL || pw->pw_dir[0] == '\0')
		{
			struct passwd *resolved = getpwnam(name);
			if(resolved == NULL)
			{
				clogin_restore_passwd_position(seen);
				break;
			}
			if(resolved->pw_name != NULL && resolved->pw_name[0] == '+')
				clogin_snapshot_passwd(&snap, resolved, resolved->pw_name + 1);
			else
				clogin_snapshot_passwd(&snap, resolved, NULL);
			candidate = &snap.pw;
			clogin_restore_passwd_position(seen);
		}
		else if(pw->pw_name[0] == '+')
		{
			clogin_snapshot_passwd(&snap, pw, name);
			candidate = &snap.pw;
		}

		if(!clogin_user_eligible(candidate)) continue;
		clogin_add_shortcut(container, candidate,
			strcmp(candidate->pw_name, "root") == 0 ? SHIRT_COLOR_ROOT : SHIRT_COLOR_USER);
	}
	endpwent();
}

static void clogin_realize_widget(Widget w)
{
	clogin_set_password_visible(False, False);
	XtAddCallback(textBox1, XmNvalueChangedCallback, clogin_username_changed_cb, NULL);
	XtRealizeWidget(w);
}

#define addCustomShortcuts clogin_add_shortcuts
#define loginButtonCB clogin_login_button_cb
#define textInputActivatedCB clogin_text_input_activated_cb
#define XtRealizeWidget(w) clogin_realize_widget((w))

#endif /* DVLOGIN_CLOGIN_RECONSTRUCTED_H */
