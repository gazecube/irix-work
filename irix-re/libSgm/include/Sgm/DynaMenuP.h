#ifndef _SGDYNAMENUP_H_
#define _SGDYNAMENUP_H_

#include <Sgm/DynaMenu.h>
#include <Xm/ManagerP.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _SgDynaMenuClassPart
{
    int foo;
} SgDynaMenuClassPart;

typedef struct _SgDynaMenuClassRec {
    CoreClassPart core_class;
    CompositeClassPart composite_class;
    ConstraintClassPart constraint_class;
    XmManagerClassPart manager_class;
    SgDynaMenuClassPart dynaMenu_class;
} SgDynaMenuClassRec;

externalref SgDynaMenuClassRec sgDynaMenuClassRec;

typedef struct _SgDynaMenuPart
{
    Widget historyPopup;
    Widget historyButton;
    XmString *historyItems;
    int historyItemCount;
    int maxHistoryCount;
    XtCallbackList dynaPushCallback;
} SgDynaMenuPart;

typedef struct _SgDynaMenuRec {
    CorePart core;
    CompositePart composite;
    ConstraintPart constraint;
    XmManagerPart manager;
    SgDynaMenuPart dynaMenu;
} SgDynaMenuRec;

#ifdef __cplusplus
}
#endif

#endif
