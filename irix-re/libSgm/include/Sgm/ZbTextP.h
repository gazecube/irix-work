#ifndef _SGZBTEXTP_H_
#define _SGZBTEXTP_H_

#include <Sgm/ZbText.h>
#include <Xm/ManagerP.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _SgZbTextClassPart
{
    int foo;
} SgZbTextClassPart;

typedef struct _SgZbTextClassRec {
    CoreClassPart core_class;
    CompositeClassPart composite_class;
    ConstraintClassPart constraint_class;
    XmManagerClassPart manager_class;
    SgZbTextClassPart zbtext_class;
} SgZbTextClassRec;

externalref SgZbTextClassRec sgZbTextClassRec;

typedef struct _SgZbTextPart
{
    Widget zoomBar;
    Widget text;
} SgZbTextPart;

typedef struct _SgZbTextRec {
    CorePart core;
    CompositePart composite;
    ConstraintPart constraint;
    XmManagerPart manager;
    SgZbTextPart zbtext;
} SgZbTextRec;

#ifdef __cplusplus
}
#endif

#endif
