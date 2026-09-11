#ifndef _SG_DROP_POCKET_H
#define _SG_DROP_POCKET_H

#include <Xm/Xm.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Public resource names recovered from the original IRIX libSgm.so.1. */
#define SgNactivePixel        "activePixel"
#define SgCActivePixel        "ActivePixel"
#define SgNiconDataBasePath   "iconDataBasePath"
#define SgCIconDataBasePath   "IconDataBasePath"
#define SgNname               "name"
#define SgCName               "Name"
#define SgNiconUpdateCallback "iconUpdateCallback"
#define SgCIconUpdateCallback "IconUpdateCallback"

/*
 * Original callback payload recovered from SGI's SgDropPocket interface
 * documentation.  The numeric SgCR_ICON_CHANGE value is intentionally not
 * invented here; callback producers/consumers can still use the structure
 * while that constant is recovered from the original header/binary.
 */
typedef struct
{
    int reason;
    XEvent *event;
    Window window;
    XmString iconName;
    char *iconData;
} SgDropPocketCallbackStruct;

extern WidgetClass sgDropPocketWidgetClass;

Widget SgCreateDropPocket(Widget parent, char *name,
                          ArgList args, Cardinal count);
Boolean SgHasFileIconLibrary(void);

#ifdef __cplusplus
}
#endif

#endif /* _SG_DROP_POCKET_H */
