/*
 * SgGrid reconstruction.
 *
 * Private layout and routine names are taken from the original SGI GridP.h
 * and libSgm.a(Grid.o). The refresh path intentionally follows the original
 * model: recompute row/column geometry, position children, then invalidate the
 * realized window so Redisplay redraws separators and managed gadget children.
 */
#include <Sgm/GridP.h>

#include <Xm/DialogS.h>
#include <Xm/XmP.h>
#include <X11/IntrinsicP.h>
#include <X11/StringDefs.h>
#include <X11/Xlib.h>
#include <stdlib.h>
#include <string.h>

#ifndef XmInheritParentProcess
#define XmInheritParentProcess ((XmParentProcessProc)_XtInherit)
#endif

static void Initialize(Widget, Widget, ArgList, Cardinal *);
static void Destroy(Widget);
static void Resize(Widget);
static void Redisplay(Widget, XEvent *, Region);
static Boolean SetValues(Widget, Widget, Widget, ArgList, Cardinal *);
static XtGeometryResult GeometryManager(Widget, XtWidgetGeometry *, XtWidgetGeometry *);
static void ChangeManaged(Widget);
static void ConstraintInitialize(Widget, Widget, ArgList, Cardinal *);
static Boolean ConstraintSetValues(Widget, Widget, Widget, ArgList, Cardinal *);

static void ResetGridSize(SgGridWidget);
static void PositionChildren(SgGridWidget);
static void RefreshGrid(SgGridWidget);
static void RebuildMatrix(SgGridWidget);
static Position RowOrigin(SgGridWidget, int);
static Position ColumnOrigin(SgGridWidget, int);
static GC GetForegroundGC(SgGridWidget);

static XtResource resources[] =
{
    { XmNautoLayout, XmCAutoLayout, XtRBoolean, sizeof(Boolean),
      XtOffsetOf(SgGridRec, grid.automatic), XtRImmediate, (XtPointer)True },
    { XmNdefaultSpacing, XmCDefaultSpacing, XtRDimension, sizeof(Dimension),
      XtOffsetOf(SgGridRec, grid.default_spacing), XtRImmediate, (XtPointer)2 },
    { XmNshowGrid, XmCShowGrid, XtRBoolean, sizeof(Boolean),
      XtOffsetOf(SgGridRec, grid.show_grid), XtRImmediate, (XtPointer)False },
    { "buildMode", "BuildMode", XtRBoolean, sizeof(Boolean),
      XtOffsetOf(SgGridRec, grid.buildMode), XtRImmediate, (XtPointer)False },
    { XmNnumRows, XmCNumRows, XtRInt, sizeof(int),
      XtOffsetOf(SgGridRec, grid.num_rows), XtRImmediate, (XtPointer)1 },
    { XmNnumColumns, XmCNumColumns, XtRInt, sizeof(int),
      XtOffsetOf(SgGridRec, grid.num_columns), XtRImmediate, (XtPointer)1 },
    { XmNrowInfo, "RowInfo", XtRString, sizeof(String),
      XtOffsetOf(SgGridRec, grid.rowString), XtRImmediate, (XtPointer)NULL },
    { XmNcolInfo, "ColInfo", XtRString, sizeof(String),
      XtOffsetOf(SgGridRec, grid.colString), XtRImmediate, (XtPointer)NULL },
    { "rowResizeInfo", "RowresizeInfo", XtRString, sizeof(String),
      XtOffsetOf(SgGridRec, grid.rowResizeString), XtRImmediate, (XtPointer)NULL },
    { "colResizeInfo", "ColResizeInfo", XtRString, sizeof(String),
      XtOffsetOf(SgGridRec, grid.colResizeString), XtRImmediate, (XtPointer)NULL }
};

static XtResource constraint_resources[] =
{
    { XmNgravity, XmCGravity, XtRInt, sizeof(int),
      XtOffsetOf(SgGridConstraintRec, grid.gravity), XtRImmediate, (XtPointer)CenterGravity },
    { XmNresizeHorizontal, XmCResizeHorizontal, XtRBoolean, sizeof(Boolean),
      XtOffsetOf(SgGridConstraintRec, grid.resizable_horiz), XtRImmediate, (XtPointer)True },
    { XmNresizeVertical, XmCResizeVertical, XtRBoolean, sizeof(Boolean),
      XtOffsetOf(SgGridConstraintRec, grid.resizable_vert), XtRImmediate, (XtPointer)True },
    { XmNrow, XmCRow, XtRInt, sizeof(int),
      XtOffsetOf(SgGridConstraintRec, grid.row), XtRImmediate, (XtPointer)0 },
    { XmNcolumn, XmCColumn, XtRInt, sizeof(int),
      XtOffsetOf(SgGridConstraintRec, grid.column), XtRImmediate, (XtPointer)0 },
    { "moveOnly", "MoveOnly", XtRBoolean, sizeof(Boolean),
      XtOffsetOf(SgGridConstraintRec, grid.moveOnly), XtRImmediate, (XtPointer)False },
    { "builderAction", "BuilderAction", XtRBoolean, sizeof(Boolean),
      XtOffsetOf(SgGridConstraintRec, grid.builderAction), XtRImmediate, (XtPointer)False }
};

SgGridClassRec sgGridClassRec =
{
    {
        (WidgetClass)&xmBulletinBoardClassRec,
        "SgGrid",
        sizeof(SgGridRec),
        NULL,
        NULL,
        False,
        Initialize,
        NULL,
        XtInheritRealize,
        NULL,
        0,
        resources,
        XtNumber(resources),
        NULLQUARK,
        True,
        XtExposeCompressMultiple,
        True,
        False,
        Destroy,
        Resize,
        Redisplay,
        SetValues,
        NULL,
        XtInheritSetValuesAlmost,
        NULL,
        NULL,
        XtVersion,
        NULL,
        XtInheritTranslations,
        XtInheritQueryGeometry,
        NULL,
        NULL
    },
    {
        GeometryManager,
        ChangeManaged,
        XtInheritInsertChild,
        XtInheritDeleteChild,
        NULL
    },
    {
        constraint_resources,
        XtNumber(constraint_resources),
        sizeof(SgGridConstraintRec),
        ConstraintInitialize,
        NULL,
        ConstraintSetValues,
        NULL
    },
    {
        XtInheritTranslations,
        NULL,
        0,
        NULL,
        0,
        XmInheritParentProcess,
        NULL
    },
    { NULL, NULL, NULL, NULL },
    { 0 }
};

WidgetClass sgGridWidgetClass = (WidgetClass)&sgGridClassRec;

static GC GetForegroundGC(SgGridWidget g)
{
    XGCValues values;
    XtGCMask mask = GCForeground | GCBackground;

    values.foreground = g->manager.foreground;
    values.background = g->core.background_pixel;
    return XtGetGC((Widget)g, mask, &values);
}

static void Initialize(Widget request, Widget new_widget,
                       ArgList args, Cardinal *num_args)
{
    SgGridWidget g = (SgGridWidget)new_widget;
    (void)request; (void)args; (void)num_args;

    if (g->core.width == 0) g->core.width = 10;
    if (g->core.height == 0) g->core.height = 10;
    if (g->grid.num_rows < 1) {
        XtWarning("Grid must have at least one row");
        g->grid.num_rows = 1;
    }
    if (g->grid.num_columns < 1) {
        XtWarning("Grid must have at least one column");
        g->grid.num_columns = 1;
    }

    g->grid.row_info = NULL;
    g->grid.col_info = NULL;
    g->grid.matrix = NULL;
    ResetGridSize(g);
}

static void Destroy(Widget widget)
{
    SgGridWidget g = (SgGridWidget)widget;
    if (g->grid.row_info) XtFree((char *)g->grid.row_info);
    if (g->grid.col_info) XtFree((char *)g->grid.col_info);
    if (g->grid.matrix) XtFree((char *)g->grid.matrix);
    g->grid.row_info = NULL;
    g->grid.col_info = NULL;
    g->grid.matrix = NULL;
}

static void ResetGridSize(SgGridWidget g)
{
    int i;
    size_t cells;

    if (g->grid.row_info) XtFree((char *)g->grid.row_info);
    if (g->grid.col_info) XtFree((char *)g->grid.col_info);
    if (g->grid.matrix) XtFree((char *)g->grid.matrix);

    g->grid.row_info = (RowColumnInfo *)XtCalloc((Cardinal)g->grid.num_rows,
                                                 sizeof(RowColumnInfo));
    g->grid.col_info = (RowColumnInfo *)XtCalloc((Cardinal)g->grid.num_columns,
                                                 sizeof(RowColumnInfo));
    cells = (size_t)g->grid.num_rows * (size_t)g->grid.num_columns;
    g->grid.matrix = (WidgetList)XtCalloc((Cardinal)cells, sizeof(Widget));

    for (i = 0; i < g->grid.num_rows; ++i) {
        g->grid.row_info[i].spring_constant = 100;
        g->grid.row_info[i].margin = g->grid.default_spacing;
    }
    for (i = 0; i < g->grid.num_columns; ++i) {
        g->grid.col_info[i].spring_constant = 100;
        g->grid.col_info[i].margin = g->grid.default_spacing;
    }
}

static void ConstraintInitialize(Widget request, Widget new_widget,
                                 ArgList args, Cardinal *num_args)
{
    SgGridConstraint c = (SgGridConstraint)new_widget->core.constraints;
    (void)request; (void)args; (void)num_args;
    if (!c) return;
    c->prefW = new_widget->core.width;
    c->prefH = new_widget->core.height;
    c->cursorX = c->cursorY = 0;
}

static Boolean ConstraintSetValues(Widget old_widget, Widget request_widget,
                                   Widget new_widget, ArgList args,
                                   Cardinal *num_args)
{
    SgGridConstraint oldc = (SgGridConstraint)old_widget->core.constraints;
    SgGridConstraint newc = (SgGridConstraint)new_widget->core.constraints;
    (void)request_widget; (void)args; (void)num_args;
    if (!oldc || !newc) return False;
    if (oldc->row != newc->row || oldc->column != newc->column ||
        oldc->gravity != newc->gravity ||
        oldc->resizable_horiz != newc->resizable_horiz ||
        oldc->resizable_vert != newc->resizable_vert)
    {
        SgGridWidget g = (SgGridWidget)XtParent(new_widget);
        if (g) {
            RebuildMatrix(g);
            PositionChildren(g);
            RefreshGrid(g);
        }
    }
    return False;
}

static void RebuildMatrix(SgGridWidget g)
{
    Cardinal i;
    size_t cells = (size_t)g->grid.num_rows * (size_t)g->grid.num_columns;
    if (!g->grid.matrix) return;
    memset(g->grid.matrix, 0, cells * sizeof(Widget));

    for (i = 0; i < g->composite.num_children; ++i) {
        Widget child = g->composite.children[i];
        SgGridConstraint c;
        size_t index;
        if (!child || !child->core.constraints) continue;
        c = (SgGridConstraint)child->core.constraints;
        if (c->row < 0 || c->row >= g->grid.num_rows ||
            c->column < 0 || c->column >= g->grid.num_columns)
            continue;
        index = (size_t)c->row * (size_t)g->grid.num_columns + (size_t)c->column;
        g->grid.matrix[index] = child;
    }
}

static void PositionChildren(SgGridWidget g)
{
    int row, col;
    Cardinal i;
    Dimension total_w = 0, total_h = 0;
    Dimension extra_w = 0, extra_h = 0;

    if (!g->grid.row_info || !g->grid.col_info) return;

    for (row = 0; row < g->grid.num_rows; ++row) g->grid.row_info[row].size = 0;
    for (col = 0; col < g->grid.num_columns; ++col) g->grid.col_info[col].size = 0;

    RebuildMatrix(g);

    for (i = 0; i < g->composite.num_children; ++i) {
        Widget child = g->composite.children[i];
        SgGridConstraint c;
        Dimension needw, needh;
        if (!child || !XtIsManaged(child) || !child->core.constraints) continue;
        c = (SgGridConstraint)child->core.constraints;
        if (c->row < 0 || c->row >= g->grid.num_rows ||
            c->column < 0 || c->column >= g->grid.num_columns)
            continue;
        c->prefW = child->core.width;
        c->prefH = child->core.height;
        needw = (Dimension)(child->core.width + 2 * child->core.border_width);
        needh = (Dimension)(child->core.height + 2 * child->core.border_width);
        if (needw > g->grid.col_info[c->column].size)
            g->grid.col_info[c->column].size = needw;
        if (needh > g->grid.row_info[c->row].size)
            g->grid.row_info[c->row].size = needh;
    }

    for (row = 0; row < g->grid.num_rows; ++row)
        total_h = (Dimension)(total_h + g->grid.row_info[row].size +
                              2 * g->grid.row_info[row].margin);
    for (col = 0; col < g->grid.num_columns; ++col)
        total_w = (Dimension)(total_w + g->grid.col_info[col].size +
                              2 * g->grid.col_info[col].margin);

    if (g->core.width > total_w) extra_w = (Dimension)(g->core.width - total_w);
    if (g->core.height > total_h) extra_h = (Dimension)(g->core.height - total_h);

    if (g->grid.num_columns > 0 && extra_w)
        for (col = 0; col < g->grid.num_columns; ++col)
            g->grid.col_info[col].size = (Dimension)(g->grid.col_info[col].size +
                extra_w / g->grid.num_columns);
    if (g->grid.num_rows > 0 && extra_h)
        for (row = 0; row < g->grid.num_rows; ++row)
            g->grid.row_info[row].size = (Dimension)(g->grid.row_info[row].size +
                extra_h / g->grid.num_rows);

    for (i = 0; i < g->composite.num_children; ++i) {
        Widget child = g->composite.children[i];
        SgGridConstraint c;
        Position x, y;
        Dimension cellw, cellh, w, h;
        if (!child || !XtIsManaged(child) || !child->core.constraints) continue;
        c = (SgGridConstraint)child->core.constraints;
        if (c->row < 0 || c->row >= g->grid.num_rows ||
            c->column < 0 || c->column >= g->grid.num_columns)
            continue;

        x = (Position)(ColumnOrigin(g, c->column) + g->grid.col_info[c->column].margin);
        y = (Position)(RowOrigin(g, c->row) + g->grid.row_info[c->row].margin);
        cellw = g->grid.col_info[c->column].size;
        cellh = g->grid.row_info[c->row].size;
        w = c->resizable_horiz ? cellw : c->prefW;
        h = c->resizable_vert ? cellh : c->prefH;
        if (w < 1) w = 1;
        if (h < 1) h = 1;
        XtConfigureWidget(child, x, y, w, h, child->core.border_width);
    }
}

static Position RowOrigin(SgGridWidget g, int row)
{
    Position y = 0;
    int i;
    for (i = 0; i < row && i < g->grid.num_rows; ++i)
        y = (Position)(y + g->grid.row_info[i].size + 2 * g->grid.row_info[i].margin);
    return y;
}

static Position ColumnOrigin(SgGridWidget g, int col)
{
    Position x = 0;
    int i;
    for (i = 0; i < col && i < g->grid.num_columns; ++i)
        x = (Position)(x + g->grid.col_info[i].size + 2 * g->grid.col_info[i].margin);
    return x;
}

static void RefreshGrid(SgGridWidget g)
{
    if (!g || !XtIsRealized((Widget)g)) return;
    XClearArea(XtDisplay((Widget)g), XtWindow((Widget)g), 0, 0, 0, 0, True);
}

static void Resize(Widget widget)
{
    SgGridWidget g = (SgGridWidget)widget;
    PositionChildren(g);
    RefreshGrid(g);
}

static void Redisplay(Widget widget, XEvent *event, Region region)
{
    SgGridWidget g = (SgGridWidget)widget;
    Display *dpy;
    Window win;
    GC gc = NULL;
    int row, col;
    (void)event; (void)region;

    if (!XtIsRealized(widget)) return;
    dpy = XtDisplay(widget);
    win = XtWindow(widget);

    if (g->grid.show_grid) {
        Position y = 0;
        Position x = 0;
        gc = GetForegroundGC(g);
        for (row = 0; row < g->grid.num_rows - 1; ++row) {
            y = (Position)(y + g->grid.row_info[row].size +
                           2 * g->grid.row_info[row].margin);
            XDrawLine(dpy, win, gc,
                      0, y, (int)g->core.width, y);
        }
        for (col = 0; col < g->grid.num_columns - 1; ++col) {
            x = (Position)(x + g->grid.col_info[col].size +
                           2 * g->grid.col_info[col].margin);
            XDrawLine(dpy, win, gc,
                      x, 0, x, (int)g->core.height);
        }
        XtReleaseGC(widget, gc);
    }
}

static XtGeometryResult GeometryManager(Widget child,
                                        XtWidgetGeometry *request,
                                        XtWidgetGeometry *reply)
{
    SgGridWidget g = (SgGridWidget)XtParent(child);
    SgGridConstraint c = (SgGridConstraint)child->core.constraints;
    (void)reply;
    if (!g || !c) return XtGeometryNo;
    if (request->request_mode & (CWX | CWY)) return XtGeometryNo;
    if (request->request_mode & CWWidth) c->prefW = request->width;
    if (request->request_mode & CWHeight) c->prefH = request->height;
    PositionChildren(g);
    RefreshGrid(g);
    return XtGeometryYes;
}

static void ChangeManaged(Widget widget)
{
    SgGridWidget g = (SgGridWidget)widget;
    PositionChildren(g);
    RefreshGrid(g);
}

static Boolean SetValues(Widget old_widget, Widget request_widget,
                         Widget new_widget, ArgList args, Cardinal *num_args)
{
    SgGridWidget oldg = (SgGridWidget)old_widget;
    SgGridWidget newg = (SgGridWidget)new_widget;
    Boolean geometry_changed = False;
    (void)request_widget; (void)args; (void)num_args;

    if (newg->grid.num_rows < 1) newg->grid.num_rows = 1;
    if (newg->grid.num_columns < 1) newg->grid.num_columns = 1;

    if (oldg->grid.num_rows != newg->grid.num_rows ||
        oldg->grid.num_columns != newg->grid.num_columns)
    {
        ResetGridSize(newg);
        geometry_changed = True;
    }
    if (oldg->grid.default_spacing != newg->grid.default_spacing ||
        oldg->grid.automatic != newg->grid.automatic)
        geometry_changed = True;

    if (geometry_changed) PositionChildren(newg);
    if (geometry_changed || oldg->grid.show_grid != newg->grid.show_grid)
        RefreshGrid(newg);

    return geometry_changed || oldg->grid.show_grid != newg->grid.show_grid;
}

Widget SgCreateGrid(Widget parent, char *name, ArgList arglist, Cardinal argcount)
{
    return XtCreateWidget(name, sgGridWidgetClass, parent, arglist, argcount);
}

Widget SgCreateGridDialog(Widget parent, char *name,
                          ArgList arglist, Cardinal argcount)
{
    Widget shell = XmCreateDialogShell(parent, name, NULL, 0);
    Widget grid = XtCreateWidget(name, sgGridWidgetClass, shell, arglist, argcount);
    XtAddCallback(grid, XmNdestroyCallback, (XtCallbackProc)XtDestroyWidget, shell);
    return grid;
}

void SgGridSetRowResizability(Widget widget, int row, int resizability)
{
    SgGridWidget g = (SgGridWidget)widget;
    if (!g || row < 0 || row >= g->grid.num_rows) {
        XtError("SgGridSetRowResize: Specified row is out of range");
        return;
    }
    g->grid.row_info[row].spring_constant = resizability;
    PositionChildren(g);
    RefreshGrid(g);
}

void SgGridSetColumnResizability(Widget widget, int col, int resizability)
{
    SgGridWidget g = (SgGridWidget)widget;
    if (!g || col < 0 || col >= g->grid.num_columns) {
        XtError("SgGridSetColumnResize: Specified column is out of range");
        return;
    }
    g->grid.col_info[col].spring_constant = resizability;
    PositionChildren(g);
    RefreshGrid(g);
}

void SgGridSetRowMargin(Widget widget, int row, Dimension margin)
{
    SgGridWidget g = (SgGridWidget)widget;
    if (!g || row < 0 || row >= g->grid.num_rows) {
        XtError("SgGridSetRowMargin: Specified row is out of range");
        return;
    }
    g->grid.row_info[row].margin = margin;
    PositionChildren(g);
    RefreshGrid(g);
}

void SgGridSetColumnMargin(Widget widget, int col, Dimension margin)
{
    SgGridWidget g = (SgGridWidget)widget;
    if (!g || col < 0 || col >= g->grid.num_columns) {
        XtError("SgGridSetColumnMargin: Specified column is out of range");
        return;
    }
    g->grid.col_info[col].margin = margin;
    PositionChildren(g);
    RefreshGrid(g);
}

Position SgGridRowToYPosition(Widget widget, int row)
{
    SgGridWidget g = (SgGridWidget)widget;
    if (!g || row <= 0) return 0;
    if (row > g->grid.num_rows) row = g->grid.num_rows;
    return RowOrigin(g, row);
}

int SgGridPositionToRow(Widget widget, Position y)
{
    SgGridWidget g = (SgGridWidget)widget;
    int row;
    if (!g) return -1;
    for (row = 0; row < g->grid.num_rows; ++row) {
        Position start = RowOrigin(g, row);
        Position stop = (Position)(start + g->grid.row_info[row].size +
                                   2 * g->grid.row_info[row].margin);
        if (y > start && y < stop) return row;
    }
    return -1;
}

int SgGridPositionToColumn(Widget widget, Position x)
{
    SgGridWidget g = (SgGridWidget)widget;
    int col;
    if (!g) return -1;
    for (col = 0; col < g->grid.num_columns; ++col) {
        Position start = ColumnOrigin(g, col);
        Position stop = (Position)(start + g->grid.col_info[col].size +
                                   2 * g->grid.col_info[col].margin);
        if (x > start && x < stop) return col;
    }
    return -1;
}

int SgGridPositionToRowSeparator(Widget widget, Position y)
{
    SgGridWidget g = (SgGridWidget)widget;
    int row;
    if (!g) return -1;
    for (row = 0; row < g->grid.num_rows; ++row) {
        Position sep = (Position)(RowOrigin(g, row) + g->grid.row_info[row].size);
        Dimension m = g->grid.row_info[row].margin;
        if (y > sep && y < (Position)(sep + 4 * m)) return row;
    }
    return -1;
}

int SgGridPositionToColumnSeparator(Widget widget, Position x)
{
    SgGridWidget g = (SgGridWidget)widget;
    int col;
    if (!g) return -1;
    for (col = 0; col < g->grid.num_columns; ++col) {
        Position sep = (Position)(ColumnOrigin(g, col) + g->grid.col_info[col].size);
        Dimension m = g->grid.col_info[col].margin;
        if (x > sep && x < (Position)(sep + 4 * m)) return col;
    }
    return -1;
}

void SgGridHighlightCell(Widget widget, int row, int col)
{
    SgGridWidget g = (SgGridWidget)widget;
    Position x, y;
    Dimension w, h;
    GC gc;
    if (!g || !XtIsRealized(widget) || row < 0 || col < 0 ||
        row >= g->grid.num_rows || col >= g->grid.num_columns)
        return;
    x = ColumnOrigin(g, col);
    y = RowOrigin(g, row);
    w = g->grid.col_info[col].size;
    h = g->grid.row_info[row].size;
    gc = GetForegroundGC(g);
    XDrawRectangle(XtDisplay(widget), XtWindow(widget), gc, x, y, w, h);
    XtReleaseGC(widget, gc);
}
