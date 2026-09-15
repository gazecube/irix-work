#ifndef _SGFINDERP_H_
#define _SGFINDERP_H_

#include <Sgm/Finder.h>
#include <Xm/ManagerP.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAXDIVS 255
#define MIN_TEXT_WIDTH 40
#define DESIRED_SEPARATOR 2

typedef struct _SgFinderClassPart
{
    int foo;
} SgFinderClassPart;

typedef struct _SgFinderClassRec {
    CoreClassPart core_class;
    CompositeClassPart composite_class;
    ConstraintClassPart constraint_class;
    XmManagerClassPart manager_class;
    SgFinderClassPart finder_class;
} SgFinderClassRec;

externalref SgFinderClassRec sgFinderClassRec;

typedef struct _SgFinderPart
{
    Widget dropPocket;
    Widget zbText;
    Widget zoomBar;
    Widget text;
    Widget historyBar;

    Dimension divisions[MAXDIVS];
    XFontStruct *fontStruct;

    int divisionCount;
    XmTextPosition oldFirstPos;
    XmTextPosition oldLastPos;
    unsigned char separator;

    Boolean addHistoryOnActivate;
    Boolean useDropPocket;
    Boolean useHistoryMenu;
    Pixmap historyPixmap;
    Pixmap insensitiveHistoryPixmap;

    XtCallbackList activate_callback;
    XtCallbackList value_changed_callback;

    SgSetTextFunc setTextSectionFunc;
} SgFinderPart;

typedef struct _SgFinderRec {
    CorePart core;
    CompositePart composite;
    ConstraintPart constraint;
    XmManagerPart manager;
    SgFinderPart finder;
} SgFinderRec;

#ifdef __cplusplus
}
#endif

#endif
