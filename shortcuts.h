/*	dvshortcuts.h - User shortcut tiles for the DeskView login screen.
 *
	Provides clickable user tiles (icon + label) that auto-fill the
	username field when clicked. Supports per-user XPM/XBM images
	with a built-in default icon whose shirt color varies by role.
	
	API:
	add_root_shortcut(Widget container);
	add_user_shortcut(Widget container, char *name, char *image);
	
	The container should be an XmScrolledWindow. A horizontal
	XmRowColumn is lazily created inside it as the workWindow.
*/

#include <X11/xpm.h>

#define SHIRT_COLOR_ROOT  "#9b4949"
#define SHIRT_COLOR_USER  "#499b94"
#define SHIRT_COLOR_DEMO  "#9177a5"
#define SHORTCUT_BG       "#cdc8b4"
#define SHORTCUT_FG       "#000000"

#define XPM_TOTAL_LINES   73  /* 1 header + 8 colors + 64 pixel rows */
#define XPM_SHIRT_LINE     7  /* index of the '%' color definition */


/*	Default user icon (64x64, 8 colors)
	
	The '%' character is the shirt color, replaced at
	runtime with SHIRT_COLOR_ROOT or SHIRT_COLOR_USER.
*/

static char *default_user_xpm[] =
{
	"64 64 8 1",
	" \tc None",
	".\tc #000000",
	"+\tc #DEDEDE",
	"@\tc #C8C8C8",
	"#\tc #4B708B",
	"$\tc #FFFFFF",
	"%\tc #9B4949",
	"&\tc #555555",
	"                                                                ",
	"                                                                ",
	"                              ......                            ",
	"                     .........++++++..                          ",
	"                   ..@@@@@..++++++++++.                         ",
	"                 ..@@@@@..+++++++++++++..                       ",
	"               ..@@@@@..+++++++++++++++++.                      ",
	"             ..@@@@@..++++++++++++++++++++.                     ",
	"           ..@@@@@..++++++++++++++++++++++.                     ",
	"         ..@@@@@..+++++++++++++++++++++++++.                    ",
	"        .@@@@@..++++++++++++++++++++++...++.                    ",
	"       .@@@@@.++++++++++++++++++++++..##.++.                    ",
	"       .@@@@.+++++++++++++++++++++..####.++.      ...           ",
	"       .@@@@.+++++++++++++++++++..######.++.     .$$$..         ",
	"       .@@@.+++++++++++++++++++.########.++.    .$$$$$$.        ",
	"       .@@@.+++++++++++++++++..#########.++.   .$$$$$$$$.       ",
	"       .@@@.+++++++++++++++..###########.++.   .$$$$$$$$$.      ",
	"       .@@@.+++++++++++++..#############.++.  .$$$$$$$$$$.      ",
	"       .@@@.++++++++++++.###############.++.  .$$$$$$$$$$.      ",
	"       .@@@.+++++++++++.################.++. .$$$$$$$$$$$.      ",
	"       .@@@.+++++++++++.################.++. .$$$$$$$$$$$.      ",
	"       .@@@.+++++++++++.################.++. .$$$$$$$$$$$.      ",
	"       .@@@.+++++++++++.################.++. .$$$$$$$$$$$.      ",
	"       .@@@.+++++++++++.################.++. .$$$$$$$$$$$.      ",
	"       .@@@.+++++++++++.################.++.  .$$$$$$$$$$.      ",
	"       .@@@.+++++++++++.################.++.  .$$$$$$$$$$.      ",
	"        .@@.+++++++++++.################.++.   .$$$$$$$$$.      ",
	"        .@@.+++++++++++.################.++.   .$$$$$$$$.       ",
	"         .@.+++++++++++.###############.++.     .$$$$$$.        ",
	"          ..+++++++++++.#############..+++.      ......         ",
	"          ..+++++++++++.###########..++++.                      ",
	"           .+++++++++++.#########..+++++.                       ",
	"           .+++++++++++.#######..+++++..            .......     ",
	"           .+++++++++++.#####..+++++...     .... ...%%%%%%%.    ",
	"           .+++++++++++.###..+++++..@@.   ..+++..%%%%%%%%%%.    ",
	"           .+++++++++++.#..+++++..@@@@@...++...%%%%%%%%%%%%.    ",
	"           .+++++++++++..++++++.@@@@@@..+++.%%%%%%%%%%%%%%%%.   ",
	"            ..+++++++++++++++..@@@@@..++++.%%%%%%%%%%%%%%%%%.   ",
	"              ..+++++++++++..@@@@@..++++++.%%%%%%%%%%%%%%%%%%.  ",
	"                .++++++++..@@@@@..+++++++.%%%%%%%%%%%%%%%%%%%.  ",
	"               .@..++....@@@@@..++..+++++.%%%%%%%%%%%%%%%%%%%.  ",
	"               .@@@..@@@@@@@..+++.%%..++.%%%%%%%%%%%%%%%%%%%%.  ",
	"               .@@@@@@@@@@..+++++.%%%%.+.%%%%%%%%%%%%%%%%%%%%.  ",
	"                ..@@@@@@..+++++++.%%%%.+.%%%%%%%%%%%%%%%%%%%%.  ",
	"                 &..@@..+++++++++.%%%%%.%%%%%%%%%%%%%%%%%%%%%.  ",
	"               &&&&&..+++++++++++.%%%%%.%%%%%%%%%%%%%%%%%%%%%.  ",
	"             &&&&&&&.++++++++++++.%%%%%.%%%%%%%%%%%%%%%%%%%%.   ",
	"           &&&&&&&&&.++.+++++++++.%%%%%.%%%%%%%%%%%%%%%%%%..    ",
	"         &&&&&&&&&&.++++.+++++++++.%%%%%%%%%%%%%%%%%%%%%%.      ",
	"       &&&&&&&&&&&&&.++++.++++++++.%%%%%%%%%%%%%%%%%%%%..       ",
	"     &&&&&&&&&&&&&&&&..+++.+++++++.%%%%%%%%%%%%%%%%%%..         ",
	"   &&&&&&&&&&&&&&&&&&&&..++.++++++.%%%%%%%%%%%%%%%%..           ",
	"     &&&&&&&&&&&&&&&&&&&&..+.++++++.%%%%%%%%%%%%%%.             ",
	"       &&&&&&&&&&&&&&&&&&&&...+++...%%%%%%%%%%%%..              ",
	"         &&&&&&&&&&&&&&&&&&&&....&&.%%%%%%%%%%..                ",
	"           &&&&&&&&&&&&&&&&&&&.&&&&&.%%%%%%%..                  ",
	"             &&&&&&&&&&&&&&&&&&&&&&&.%%%%%..                    ",
	"               &&&&&&&&&&&&&&&&&&&&&&.....                      ",
	"                 &&&&&&&&&&&&&&&&&&                             ",
	"                   &&&&&&&&&&&&&&                               ",
	"                     &&&&&&&&&                                  ",
	"                       &&&&                                     ",
	"                                                                ",
	"                                                                ",
};



typedef struct
{
	char username[MAX_USERNAME_LEN + 1];
} ShortcutCBData;



/*	shortcutClickEH - fills the username field and focuses
	the password field when a shortcut tile is clicked.
*/
static void shortcutClickEH(Widget w, XtPointer client_data,
	XEvent *event, Boolean *cont)
{
	ShortcutCBData *data = (ShortcutCBData *)client_data;

	XmTextSetString(textBox1, data->username);
	clear_password_field();
	XmProcessTraversal(textBox2, XmTRAVERSE_CURRENT);
}



/*	create_default_user_pixmap - builds a pixmap from the
	embedded default icon with a custom shirt color.
	
	Returns the icon pixmap. Writes the shape mask to
	mask_out (may be None if the XPM has no transparency).
*/
static Pixmap create_default_user_pixmap(Display *dpy, Drawable d,
	const char *shirt_color, Pixmap *mask_out)
{
	char *xpm_copy[XPM_TOTAL_LINES];
	char shirt_line[64];
	Pixmap pixmap = None;
	int i;

	/* build the replacement color line for '%' */
	snprintf(shirt_line, sizeof(shirt_line), "%%\tc %s", shirt_color);

	/* shallow-copy every line, swapping the shirt color row */
	for(i = 0; i < XPM_TOTAL_LINES; i++)
	{
		if(i == XPM_SHIRT_LINE)
		{
			xpm_copy[i] = shirt_line;
		}
		else
		{
			xpm_copy[i] = default_user_xpm[i];
		}
	}

	XpmAttributes attrs;
	attrs.valuemask = 0;

	if(XpmCreatePixmapFromData(dpy, d, xpm_copy,
		&pixmap, mask_out, &attrs) != XpmSuccess)
	{
		pixmap = None;
		if(mask_out) { *mask_out = None; }
	}

	return pixmap;
}


/*	load_user_pixmap - loads an icon from an XPM or XBM file.
	
	Tries XPM first. If the path ends in .xbm (or XPM fails),
	falls back to XReadBitmapFile.
	
	Returns the pixmap or None on failure.
*/
static Pixmap load_user_pixmap(Display *dpy, Drawable d,
	const char *path, Pixmap *mask_out)
{
	Pixmap pixmap = None;
	XpmAttributes attrs;
	int len;

	if(path == NULL) { return None; }

	attrs.valuemask = 0;

	/* try XPM first */
	if(XpmReadFileToPixmap(dpy, d, (char *)path,
		&pixmap, mask_out, &attrs) == XpmSuccess)
	{
		return pixmap;
	}

	/* fall back to XBM if the extension matches */
	len = strlen(path);
	if(len >= 4 && strcmp(path + len - 4, ".xbm") == 0)
	{
		unsigned int w, h;
		int xhot, yhot;

		if(XReadBitmapFile(dpy, d, path,
			&w, &h, &pixmap, &xhot, &yhot) == BitmapSuccess)
		{
			if(mask_out) { *mask_out = None; }
			return pixmap;
		}
	}

	return None;
}


/*	composite_with_background - flattens a pixmap + mask pair
	onto a solid-color background so that transparent regions
	match the container instead of rendering as black.
	
	Returns a new pixmap. The caller should XFreePixmap the
	originals if they are no longer needed.
*/
static Pixmap composite_with_background(Display *dpy, Drawable d,
	Pixmap icon, Pixmap mask, unsigned long bg_pixel)
{
	Window root_ret;
	int x_ret, y_ret;
	unsigned int w, h, border_ret, depth_ret;

	XGetGeometry(dpy, icon, &root_ret, &x_ret, &y_ret,
		&w, &h, &border_ret, &depth_ret);

	/* create a pixmap the same size and depth as the icon */
	Pixmap result = XCreatePixmap(dpy, d, w, h, depth_ret);
	GC gc = XCreateGC(dpy, result, 0, NULL);

	/* fill entirely with the background color */
	XSetForeground(dpy, gc, bg_pixel);
	XFillRectangle(dpy, result, gc, 0, 0, w, h);

	/* stamp the icon through the transparency mask */
	if(mask != None)
	{
		XSetClipMask(dpy, gc, mask);
		XSetClipOrigin(dpy, gc, 0, 0);
	}
	XCopyArea(dpy, icon, result, gc, 0, 0, w, h, 0, 0);

	XFreeGC(dpy, gc);
	return result;
}



/*	compare_shortcut_tiles - qsort comparator.
	Root is always first; everything else is alphabetical.
*/
static int compare_shortcut_tiles(const void *a, const void *b)
{
	Widget wa = *(const Widget *)a;
	Widget wb = *(const Widget *)b;

	ShortcutCBData *da = NULL;
	ShortcutCBData *db = NULL;
	XtVaGetValues(wa, XmNuserData, &da, NULL);
	XtVaGetValues(wb, XmNuserData, &db, NULL);

	if(da == NULL || db == NULL) { return 0; }

	int a_root = (strcmp(da->username, "root") == 0);
	int b_root = (strcmp(db->username, "root") == 0);

	if(a_root && !b_root) { return -1; }
	if(!a_root && b_root) { return  1; }

	return strcmp(da->username, db->username);
}


/*	update_shortcut_layout - sorts tiles (root first, then
	alphanumeric) and recalculates the row count so tiles
	wrap to fit the visible width of the container.
	
	Call once after XtRealizeWidget and on every resize.
	
	With HORIZONTAL + PACK_COLUMN, XmNnumColumns is the
	number of ROWS. Children fill left-to-right per row,
	then down by normal reading order.
*/
static void update_shortcut_layout(Widget container)
{
	Widget rc = XtNameToWidget(container, "*dvShortcutRC");
	if(rc == NULL) { return; }

	Widget *children;
	Cardinal num_children;
	XtVaGetValues(rc,
		XmNchildren, &children,
		XmNnumChildren, &num_children,
	NULL);
	if(num_children == 0) { return; }

	/* sort by root first, then alphabetical */
	Widget *sorted = (Widget *)malloc(num_children * sizeof(Widget));
	if(sorted != NULL)
	{
		Cardinal i;
		memcpy(sorted, children, num_children * sizeof(Widget));
		qsort(sorted, num_children, sizeof(Widget), compare_shortcut_tiles);

		for(i = 0; i < num_children; i++)
		{
			XtVaSetValues(sorted[i], XmNpositionIndex, (int)i, NULL);
		}
		free(sorted);
	}

	/* calculate how many items fit per row */
	Dimension avail_w = 0;
	Widget clip = XtNameToWidget(container, "*ClipWindow");
	if(clip != NULL)
	{
		XtVaGetValues(clip, XmNwidth, &avail_w, NULL);
	}
	else
	{
		XtVaGetValues(container, XmNwidth, &avail_w, NULL);
	}
	if(avail_w == 0) { return; }

	/* re-fetch children[0] after sort may have changed */
	XtVaGetValues(rc, XmNchildren, &children, NULL);

	Dimension child_w = 0;
	XtVaGetValues(children[0], XmNwidth, &child_w, NULL);
	if(child_w == 0) { return; }

	Dimension spacing, margin_w;
	XtVaGetValues(rc,
		XmNspacing, &spacing,
		XmNmarginWidth, &margin_w,
	NULL);

	int usable = (int)avail_w - (2 * (int)margin_w);
	int items_per_row = usable / ((int)child_w + (int)spacing);
	if(items_per_row < 1) { items_per_row = 1; }

	/*
		HORIZONTAL + PACK_COLUMN: numColumns = number of rows.
		Ceiling division gives the minimum rows to hold all tiles.
	*/
	int num_rows = ((int)num_children + items_per_row - 1) / items_per_row;
	if(num_rows < 1) { num_rows = 1; }

	XtVaSetValues(rc, XmNnumColumns, (short)num_rows, NULL);
}


/*	shortcutContainerResizeEH - fires on ConfigureNotify
	so the tile grid reflows when the window is resized.
*/
static void shortcutContainerResizeEH(Widget w, XtPointer client_data,
	XEvent *event, Boolean *cont)
{
	if(event->type == ConfigureNotify)
	{
		update_shortcut_layout(w);
	}
}


/*	find_or_create_shortcut_rc - returns the grid-layout
	XmRowColumn inside a scrolledWin, creating it on the
	first call.
	
	HORIZONTAL + PACK_COLUMN: children fill left-to-right
	per row, then wrap down. numColumns = number of rows
	(recalculated by update_shortcut_layout).
*/
static Widget find_or_create_shortcut_rc(Widget container)
{
	Widget rc = XtNameToWidget(container, "*dvShortcutRC");
	if(rc != NULL) { return rc; }

	rc = XtVaCreateManagedWidget("dvShortcutRC",
		xmRowColumnWidgetClass, container,
		XmNorientation, XmHORIZONTAL,
		XmNpacking, XmPACK_COLUMN,
		XmNnumColumns, 1,
		XmNadjustLast, False,
		XmNspacing, 12,
		XmNmarginHeight, 18,
		XmNmarginWidth, 18,
		XmNtraversalOn, 0,
		XtVaTypedArg, XmNbackground, XmRString, SHORTCUT_BG, 8,
	NULL);

	XtVaSetValues(container, XmNworkWindow, rc, NULL);

	/* reflow tiles when the container is resized */
	XtAddEventHandler(container, StructureNotifyMask, False,
		shortcutContainerResizeEH, NULL);

	return rc;
}


/*	make_shortcut_interactive - attaches click, enter and
	leave event handlers to a widget so it behaves like a
	clickable tile with a hand cursor on hover.
*/
static void make_shortcut_interactive(Widget w, ShortcutCBData *data)
{
	XtAddEventHandler(w, ButtonPressMask, False,
		shortcutClickEH, (XtPointer)data);
	XtAddEventHandler(w, EnterWindowMask, False,
		mouseOverPushButton, (XtPointer)w);
	XtAddEventHandler(w, LeaveWindowMask, False,
		mouseLeftPushButton, (XtPointer)w);
}


/*	add_shortcut_internal - creates a single shortcut tile
	inside the given scrolledWin container.
*/
static void add_shortcut_internal(Widget container,
	const char *username, const char *image_path,
	const char *shirt_color)
{
	Display *dpy = XtDisplay(container);
	Drawable root = RootWindowOfScreen(XtScreen(container));
	Pixmap icon = None;
	Pixmap mask = None;

	/* try loading a custom image first */
	if(image_path != NULL)
	{
		icon = load_user_pixmap(dpy, root, image_path, &mask);
	}

	/* fall back to the default icon with appropriate shirt color */
	if(icon == None)
	{
		icon = create_default_user_pixmap(dpy, root, shirt_color, &mask);
	}

	/* get (or create) the horizontal layout row */
	Widget rc = find_or_create_shortcut_rc(container);

	/*	Flatten transparency against the container background.
		XmNlabelPixmap ignores the mask, so transparent regions
		would render as black without this step.
	*/
	if(icon != None && mask != None)
	{
		Pixel bg_pixel;
		XtVaGetValues(rc, XmNbackground, &bg_pixel, NULL);

		Pixmap flat = composite_with_background(dpy, root,
			icon, mask, bg_pixel);

		XFreePixmap(dpy, icon);
		XFreePixmap(dpy, mask);
		icon = flat;
		mask = None;
	}

	/* generate a unique widget name */
	static int shortcut_id = 0;
	char name[32];
	snprintf(name, sizeof(name), "dvShortcut%d", shortcut_id++);

	/* vertical RC for this tile: icon on top, label below */
	Widget tileRC = XtVaCreateManagedWidget(name,
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

	/* icon label (pixmap) */
	char iconName[40];
	snprintf(iconName, sizeof(iconName), "%sIcon", name);

	Widget iconW = XtVaCreateManagedWidget(iconName,
		xmLabelWidgetClass, tileRC,
		XmNlabelType, XmPIXMAP,
		XmNlabelPixmap, icon,
		XmNmarginWidth, 0,
		XmNmarginHeight, 0,
		XmNhighlightThickness, 0,
		XmNshadowThickness, 0,
		XtVaTypedArg, XmNbackground, XmRString, SHORTCUT_BG, 8,
	NULL);

	/* username label (text) */
	char textName[40];
	snprintf(textName, sizeof(textName), "%sLabel", name);

	Widget labelW = XtVaCreateManagedWidget(textName,
		xmLabelWidgetClass, tileRC,
		XmNmarginWidth, 1,
		XmNmarginHeight, 4,
		XmNhighlightThickness, 0,
		XmNshadowThickness, 0,
		XmNalignment, XmALIGNMENT_CENTER,
		XtVaTypedArg, XmNbackground, XmRString, SHORTCUT_BG, 8,
		XtVaTypedArg, XmNforeground, XmRString, SHORTCUT_FG, 8,
		XtVaTypedArg, XmNlabelString, XmRString,
			username, strlen(username) + 1,
	NULL);

	/* allocate persistent callback data */
	ShortcutCBData *cbdata =
		(ShortcutCBData *)malloc(sizeof(ShortcutCBData));
	strncpy(cbdata->username, username, MAX_USERNAME_LEN);
	cbdata->username[MAX_USERNAME_LEN] = '\0';

	/* attach to the tile so update_shortcut_layout can sort by name */
	XtVaSetValues(tileRC, XmNuserData, (XtPointer)cbdata, NULL);

	/* make the tile, icon, and label all clickable */
	make_shortcut_interactive(tileRC, cbdata);
	make_shortcut_interactive(iconW, cbdata);
	make_shortcut_interactive(labelW, cbdata);
}


void add_root_shortcut(Widget container)
{
	add_shortcut_internal(container, "root", NULL, SHIRT_COLOR_ROOT);
}

void add_user_shortcut(Widget container, char *name, char *image)
{
	add_shortcut_internal(container, name, image, SHIRT_COLOR_USER);
}
