#ifndef _SGGRID_H_
#define _SGGRID_H_

#include <Xm/Xm.h>

#ifdef __cplusplus
extern "C" {
#endif

externalref WidgetClass sgGridWidgetClass;

typedef struct _SgGridClassRec *SgGridWidgetClass;
typedef struct _SgGridRec      *SgGridWidget;

#ifndef SgIsGrid
#define SgIsGrid(w) XtIsSubclass((w), sgGridWidgetClass)
#endif

#define XmNnumRows            "numRows"
#define XmCNumRows            "NumRows"
#define XmNnumColumns         "numColumns"
#define XmCNumColumns         "NumColumns"
#define XmNrow                "row"
#define XmCRow                "Row"
#define XmNcolumn             "column"
#define XmCColumn             "Column"
#define XmNresizeVertical     "resizeVertical"
#define XmNresizeHorizontal   "resizeHorizontal"
#define XmCResizeVertical     "ResizeVertical"
#define XmCResizeHorizontal   "ResizeHorizontal"
#define XmNshowGrid           "showGrid"
#define XmCShowGrid           "ShowGrid"
#define XmCGravity            "Gravity"
#define XmNgravity            "gravity"
#define XmRGravity            "Gravity"
#define XmNautoLayout         "autoLayout"
#define XmCAutoLayout         "AutoLayout"
#define XmNdefaultSpacing     "defaultSpacing"
#define XmCDefaultSpacing     "DefaultSpacing"
#define XmNrowResizable       "rowResizable"
#define XmCRowResizable       "RowResizable"
#define XmNcolumnResizable    "columnResizable"
#define XmCColumnResizable    "ColumnResizable"
#define XmNrowInfo            "rowInfo"
#define XmNcolInfo            "colInfo"

extern Widget SgCreateGrid(Widget parent, char *name,
                           ArgList arglist, Cardinal argcount);
extern Widget SgCreateGridDialog(Widget parent, char *name,
                                 ArgList arglist, Cardinal argcount);
extern void SgGridSetColumnResizability(Widget widget, int col,
                                        int resizability);
extern void SgGridSetRowResizability(Widget widget, int row,
                                     int resizability);
extern void SgGridSetColumnMargin(Widget widget, int col, Dimension margin);
extern void SgGridSetRowMargin(Widget widget, int row, Dimension margin);

/* Exported by the original library although omitted from the public header. */
extern void SgGridHighlightCell(Widget widget, int row, int col);
extern int SgGridPositionToRowSeparator(Widget widget, Position y);
extern int SgGridPositionToColumnSeparator(Widget widget, Position x);
extern int SgGridPositionToRow(Widget widget, Position y);
extern int SgGridPositionToColumn(Widget widget, Position x);
extern Position SgGridRowToYPosition(Widget widget, int row);

#ifdef __cplusplus
}
#endif

#endif
