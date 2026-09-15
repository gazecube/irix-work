#ifndef _SGGRIDP_H_
#define _SGGRIDP_H_

#include <Sgm/Grid.h>
#include <Xm/BulletinBP.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _SgGridConstraintPart
{
    int       gravity;
    Boolean   resizable_horiz;
    Boolean   resizable_vert;
    int       row;
    int       column;
    Dimension prefH;
    Dimension prefW;
    int       cursorX;
    int       cursorY;
    Boolean   moveOnly;
    Boolean   builderAction;
} SgGridConstraintPart, *SgGridConstraint;

typedef struct _SgGridConstraintRec
{
    XmManagerConstraintPart manager;
    SgGridConstraintPart    grid;
} SgGridConstraintRec, *SgGridConstraintPtr;

typedef struct _SgGridClassPart
{
    int foo;
} SgGridClassPart;

typedef struct _SgGridClassRec
{
    CoreClassPart            core_class;
    CompositeClassPart       composite_class;
    ConstraintClassPart      constraint_class;
    XmManagerClassPart       manager_class;
    XmBulletinBoardClassPart bulletin_board_class;
    SgGridClassPart          grid_class;
} SgGridClassRec;

externalref SgGridClassRec sgGridClassRec;

typedef struct _RowColumnInfo
{
    int       spring_constant;
    Dimension margin;
    Dimension size;
} RowColumnInfo;

typedef struct _SgGridPart
{
    Boolean        buildMode;
    Boolean        automatic;
    Dimension      default_spacing;
    int            num_rows;
    int            num_columns;
    RowColumnInfo *row_info;
    RowColumnInfo *col_info;
    WidgetList     matrix;
    Boolean        show_grid;
    String         rowString;
    String         colString;
    String         rowResizeString;
    String         colResizeString;
} SgGridPart;

typedef struct _SgGridRec
{
    CorePart            core;
    CompositePart       composite;
    ConstraintPart      constraint;
    XmManagerPart       manager;
    XmBulletinBoardPart bulletin_board;
    SgGridPart          grid;
} SgGridRec;

#ifdef __cplusplus
}
#endif

#endif
