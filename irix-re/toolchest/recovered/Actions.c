/*
 * Toolchest parser action sentinels recovered from IRIX 6.5.
 *
 * Each original body is exactly `return True`:
 *   F_Separator @ 0x10010f18, size 0x08
 *   F_Label     @ 0x10010f20, size 0x08
 *   F_Nop       @ 0x10010f64, size 0x08
 *   F_Inactive  @ 0x10010f6c, size 0x08
 *   F_Menu      @ 0x10010f74, size 0x08
 *   F_Title     @ 0x1001117c, size 0x08
 *
 * ActivateCallback explicitly supplies four argument registers to the action
 * function pointer: argument string, a null context, Xm callback event, and
 * the MenuItem label. Keep that recovered call surface here until the exact
 * original typedef spelling is recovered.
 */

#include <X11/Intrinsic.h>
#include <X11/Xlib.h>

typedef Boolean (*ToolchestFunction)(String, XtPointer, XEvent *, String);

#define SIMPLE_ACTION(name) \
Boolean name(String args, XtPointer context, XEvent *event, String label) \
{ \
    (void)args; \
    (void)context; \
    (void)event; \
    (void)label; \
    return True; \
}

SIMPLE_ACTION(F_Separator)
SIMPLE_ACTION(F_Label)
SIMPLE_ACTION(F_Nop)
SIMPLE_ACTION(F_Inactive)
SIMPLE_ACTION(F_Menu)
SIMPLE_ACTION(F_Title)

#undef SIMPLE_ACTION
