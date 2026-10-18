#include "IrixHostAbiContext.h"

#include <X11/Intrinsic.h>
#include <X11/IntrinsicP.h>
#include <X11/ShellP.h>
#include <Xm/Label.h>
#include <Xm/DropSMgr.h>
#include <Xm/Xm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Force the split Xt and Motif DSOs into the process before context creation. */

/* The prototype does not export the host Motif resource table yet. Model a
 * native client's table at its header-defined offsets for this ABI test. */
char _XmStrings[32768];

#undef _XmGetDefaultFontList
extern XmFontList _XmGetDefaultFontList(Widget, unsigned char);

extern int irix_host_abi_test_drop_callback(Widget, int,
    const unsigned char[24], const unsigned char[96], unsigned char[24]);

typedef struct DropProbeState {
    Widget widget;
    int calls;
    int failed;
} DropProbeState;

static void drop_probe_callback(Widget widget, XtPointer closure,
                                 XtPointer data)
{
    DropProbeState *state = NULL;
    XmDragProcCallbackStruct *cb = data;
    XmString nested;
    XtVaGetValues(widget, XmNuserData, &state, NULL);
    if (!state) return;
    ++state->calls;
    if (widget != state->widget || closure || cb->reason != 17 ||
        cb->timeStamp != 0x12345678u || cb->dragContext ||
        cb->x != -7 || cb->y != 250 || !cb->event ||
        cb->event->type != MotionNotify || cb->event->xmotion.x != 101 ||
        cb->event->xmotion.y != -25 ||
        cb->event->xmotion.display != XtDisplayOfObject(widget))
        state->failed = 1;
    /* A real callback may enter Xt/Motif again before the outer call returns. */
    nested = XmStringCreateLtoR("nested callback", "ISO8859-1");
    if (!nested) state->failed = 1;
    XmStringFree(nested);
    cb->x = -99; cb->y = 321;
    cb->dropSiteStatus = XmVALID_DROP_SITE;
    cb->operation = XmDROP_COPY;
    cb->operations = XmDROP_COPY | XmDROP_MOVE;
    cb->animate = False;
}

static void drop_probe_drop_callback(Widget widget, XtPointer closure,
                                      XtPointer data)
{
    XmDropProcCallbackStruct *cb = data;
    XmDragProcCallbackStruct drag;
    memset(&drag, 0, sizeof(drag));
    drag.reason = cb->reason; drag.event = cb->event;
    drag.timeStamp = cb->timeStamp; drag.dragContext = cb->dragContext;
    drag.x = cb->x; drag.y = cb->y;
    drag.dropSiteStatus = cb->dropSiteStatus;
    drag.operation = cb->operation; drag.operations = cb->operations;
    drag.animate = cb->dropAction;
    drop_probe_callback(widget, closure, &drag);
    cb->x = drag.x; cb->y = drag.y;
    cb->dropSiteStatus = drag.dropSiteStatus;
    cb->operation = drag.operation; cb->operations = drag.operations;
    cb->dropAction = drag.animate;
}

static int native_drop_probe(Widget widget)
{
    DropProbeState state = { widget, 0, 0 };
    Arg args[5];
    Atom targets[2] = { 31u, 77u };
    unsigned char input[24] = {
        0,0,0,17, 0,0,0,0, 0x12,0x34,0x56,0x78, 0,0,0,0,
        0xff,0xf9,0,0xfa, 0,0,0,1
    };
    unsigned char event[96] = { 0 }, output[24];
    int kind;
    event[3] = MotionNotify; event[35] = 101;
    event[36] = 0xff; event[37] = 0xff;
    event[38] = 0xff; event[39] = 0xe7;
    XtVaSetValues(widget, XmNuserData, &state, NULL);
    XtSetArg(args[0], XmNdragProc, drop_probe_callback);
    XtSetArg(args[1], XmNdropProc, drop_probe_drop_callback);
    XtSetArg(args[2], XmNdropSiteActivity, XmDROP_SITE_INACTIVE);
    XmDropSiteRegister(widget, args, 3u);
    XtSetArg(args[0], XmNimportTargets, targets);
    XtSetArg(args[1], XmNnumImportTargets, 2u);
    XtSetArg(args[2], XmNdropSiteOperations, XmDROP_COPY | XmDROP_MOVE);
    XmDropSiteUpdate(widget, args, 3u);
    for (kind = 0; kind < 2; ++kind) {
        if (irix_host_abi_test_drop_callback(widget, kind, input, event,
                                             output) != 0 ||
            output[16] != 0xff || output[17] != 0x9d ||
            output[18] != 1 || output[19] != 0x41 ||
            output[20] != XmVALID_DROP_SITE || output[21] != XmDROP_COPY ||
            output[22] != (XmDROP_COPY | XmDROP_MOVE) || output[23] != 0)
            state.failed = 1;
    }
    XmDropSiteUnregister(widget);
    if (irix_host_abi_test_drop_callback(widget, 0, input, event, output) == 0)
        state.failed = 1;
    XtVaSetValues(widget, XmNuserData, NULL, NULL);
    if (state.failed || state.calls != 2) {
        fprintf(stderr, "native drop callback conversion/reentry failed (%d calls)\n",
                state.calls);
        return 1;
    }
    return 0;
}

static int guest_widget_probe(IrixHostAbiContext *context)
{
    const char *class_name = getenv("IRIX_HOST_ABI_PROBE_CLASS");
    const char *app_name = getenv("IRIX_HOST_ABI_PROBE_APP");
    IrixGuestAddr app_out, guest_class, guest_app, argc_addr, argv_addr;
    IrixGuestAddr label_name, manager_name;
    uint32_t args[9], shell = 0, label = 0, app_context = 0;
    uint32_t managers[3] = { 0, 0, 0 };
    unsigned i;
    unsigned char empty_argv[8] = { 0 };

    if (!class_name) class_name = "MotifProbe";
    if (!app_name) app_name = "motifprobe";
    if (irix_host_abi_context_copy_bytes(context, empty_argv,
                                         sizeof(empty_argv), &argv_addr) != 0 ||
        irix_host_abi_context_copy_string(context, class_name, &guest_class) != 0 ||
        irix_host_abi_context_copy_string(context, app_name, &guest_app) != 0 ||
        irix_host_abi_context_copy_string(context, "probeLabel", &label_name) != 0 ||
        irix_host_abi_context_copy_string(context, "probeManager", &manager_name) != 0 ||
        irix_host_abi_context_copy_bytes(context, "\0\0\0\0", 4u,
                                         &argc_addr) != 0 ||
        irix_host_abi_context_copy_bytes(context, "\0\0\0\0", 4u,
                                         &app_out) != 0 ||
        irix_host_abi_context_write32be(context, argc_addr, 1u) != 0 ||
        irix_host_abi_context_write32be(context, argv_addr, guest_app) != 0 ||
        irix_host_abi_context_write32be(context, argv_addr + 4u, 0u) != 0)
        return 1;

    args[0] = app_out;
    args[1] = guest_class;
    args[2] = 0; args[3] = 0;
    args[4] = argc_addr;
    args[5] = argv_addr;
    args[6] = 0; args[7] = 0; args[8] = 0;
    if (irix_host_abi_context_call(context, "XtAppInitialize", args, 9u,
                                   &shell) != 0 || !shell ||
        irix_host_abi_context_read32be(context, app_out, &app_context) != 0 ||
        !app_context) {
        fprintf(stderr, "translated XtAppInitialize probe failed\n");
        return 1;
    }
    /* Repeated Manager initialization exercises the SGI per-instance
     * extension lifecycle, which a shell/label-only probe does not cover. */
    for (i = 0; i < 3u; ++i) {
        args[0] = i ? managers[i - 1u] : shell;
        args[1] = manager_name; args[2] = 0; args[3] = 0;
        if (irix_host_abi_context_call(context, "XmCreateForm", args, 4u,
                                       &managers[i]) != 0 || !managers[i]) {
            fprintf(stderr, "translated nested XmCreateForm probe failed\n");
            return 1;
        }
    }
    args[0] = managers[2]; args[1] = label_name; args[2] = 0; args[3] = 0;
    if (irix_host_abi_context_call(context, "XmCreateLabel", args, 4u,
                                   &label) != 0 || !label) {
        fprintf(stderr, "translated XmCreateLabel probe failed\n");
        return 1;
    }
    args[0] = label;
    if (irix_host_abi_context_call(context, "XtManageChild", args, 1u,
                                   NULL) != 0) {
        fprintf(stderr, "translated XtManageChild probe failed\n");
        return 1;
    }
    for (i = 3u; i > 0u; --i) {
        args[0] = managers[i - 1u];
        if (irix_host_abi_context_call(context, "XtManageChild", args, 1u,
                                       NULL) != 0)
            return 1;
    }
    args[0] = shell;
    if (irix_host_abi_context_call(context, "XtRealizeWidget", args, 1u,
                                   NULL) != 0 ||
        irix_host_abi_context_call(context, "XtDestroyWidget", args, 1u,
                                   NULL) != 0) {
        fprintf(stderr, "translated widget realize/destroy probe failed\n");
        return 1;
    }
    args[0] = app_context;
    if (irix_host_abi_context_call(context, "XtDestroyApplicationContext", args,
                                   1u, NULL) != 0) {
        fprintf(stderr, "translated app-context cleanup probe failed\n");
        return 1;
    }
    return 0;
}

static int native_widget_probe(void)
{
    int argc = 1;
    char program_name[] = "host-abi-widget-probe";
    char *argv[] = { program_name, NULL };
    XtAppContext app_context = NULL;
    Widget shell, label, popup;
    Widget children[1];
    XmFontContext font_context = (XmFontContext)1;

    shell = XtAppInitialize(&app_context, "HostAbiWidgetProbe", NULL, 0,
                            &argc, argv, NULL, NULL, 0);
    if (!shell || !app_context) {
        fprintf(stderr, "native XtAppInitialize wrapper failed\n");
        return 1;
    }
    if (!XtScreen(shell) || ((ShellWidget)shell)->shell.visual !=
            DefaultVisualOfScreen(XtScreen(shell))) {
        fprintf(stderr, "native shell record has no paired host visual\n");
        return 1;
    }
    popup = XtCreatePopupShell("nativeProbePopup", XtClass(shell), shell,
                               NULL, 0u);
    if (!popup || XtParent(popup) != shell ||
        ((ShellWidget)popup)->shell.visual !=
            DefaultVisualOfScreen(XtScreen(popup))) {
        fprintf(stderr,
            "native popup record mismatch popup=%p parent=%p expected-parent=%p visual=%p expected-visual=%p screen=%p\n",
            (void *)popup, popup ? (void *)XtParent(popup) : NULL,
            (void *)shell,
            popup ? (void *)((ShellWidget)popup)->shell.visual : NULL,
            popup && XtScreen(popup)
                ? (void *)DefaultVisualOfScreen(XtScreen(popup)) : NULL,
            popup ? (void *)XtScreen(popup) : NULL);
        return 1;
    }
    XtDestroyWidget(popup);
    if (XmFontListInitFontContext(&font_context, NULL) != False ||
        font_context != (XmFontContext)0) {
        fprintf(stderr, "null XmFontList did not fail cleanly\n");
        XtDestroyWidget(shell);
        XtDestroyApplicationContext(app_context);
        return 1;
    }
    label = XmCreateLabel(shell, "nativeProbeLabel", NULL, 0);
    if (!label) {
        fprintf(stderr, "native XmCreateLabel wrapper failed\n");
        XtDestroyWidget(shell);
        XtDestroyApplicationContext(app_context);
        return 1;
    }
    if (!XtDisplayOfObject(label) || !XtScreenOfObject(label) ||
        XtScreen(label) != XtScreenOfObject(label)) {
        fprintf(stderr, "native child widget did not inherit display/screen\n");
        XtDestroyWidget(shell);
        XtDestroyApplicationContext(app_context);
        return 1;
    }
    children[0] = label;
    {
        Arg text_arg;
        XmString text = XmStringCreate("native status label", "ISO8859-1");
        XmString copy, different;
        if (!text) return 1;
        copy = XmStringCopy(text);
        different = XmStringCreate("different text", "ISO8859-1");
        if (!copy || copy == text || !XmStringByteCompare(text, copy) ||
            !XmStringCompare(text, copy) || XmStringByteCompare(text, different) ||
            XmStringByteCompare(text, NULL)) return 1;
        {
            XmFontList borrowed = _XmGetDefaultFontList(label, 1u);
            XmFontList owned = XmFontListCopy(borrowed);
            Dimension width = 0, height = 0;
            XmStringExtent(owned, text, &width, &height);
            if (!borrowed || !owned || !width || !height ||
                XmStringWidth(owned, text) != width ||
                XmStringHeight(owned, text) != height) {
                fprintf(stderr, "native font-list/extent conversion failed (%u x %u)\n",
                        width, height);
                return 1;
            }
            XmFontListFree(owned);
        }
        XmStringFree(copy); XmStringFree(different);
        XtSetArg(text_arg, XmNlabelString, text);
        XtSetValues(label, &text_arg, 1u);
        XmStringFree(text);
    }
    {
        Arg geometry[2];
        XtSetArg(geometry[0], XtNwidth, 123u);
        XtSetArg(geometry[1], XtNx, (XtArgVal)-7);
        XtSetValues(label, geometry, 2u);
        XtVaSetValues(label, XtNheight, (XtArgVal)45u, NULL);
        if (label->core.width != 123u || label->core.height != 45u ||
            label->core.x != -7) {
            fprintf(stderr, "native XtSetValues scalar conversion failed\n");
            return 1;
        }
    }
    if (native_drop_probe(label) != 0)
        return 1;
    XtManageChildren(children, 1u);
    XtManageChild(label);
    XtRealizeWidget(shell);
    {
        const char *hold = getenv("IRIX_HOST_ABI_HOLD_WIDGET_PROBE");
        unsigned long seconds = hold ? strtoul(hold, NULL, 10) : 0;
        if (seconds) {
            XSync(XtDisplay(shell), False);
            sleep(seconds > 30u ? 30u : (unsigned)seconds);
        }
    }
    XtDestroyWidget(shell);
    XtDestroyApplicationContext(app_context);
    return 0;
}

int main(void)
{
    IrixHostAbiContext *context;
    IrixGuestAddr text, charset;
    uint32_t args[2], xm_string = 0;
    XmString host_xm_string;
    strcpy(XmNuserData, "userData");
    strcpy(XmNdragProc, "dragProc");
    strcpy(XmNdropProc, "dropProc");
    strcpy(XmNdropSiteActivity, "dropSiteActivity");
    strcpy(XmNimportTargets, "importTargets");
    strcpy(XmNnumImportTargets, "numImportTargets");
    strcpy(XmNdropSiteOperations, "dropSiteOperations");
    strcpy(XmNlabelString, "labelString");
    (void)XmStringCreateLtoR;
    context = irix_host_abi_context_create();
    if (!context) {
        fprintf(stderr, "host ABI context initialization failed\n");
        return 1;
    }
    if (!irix_host_abi_context_has_function(context, "XtAppInitialize") ||
        !irix_host_abi_context_has_function(context, "XmStringCreateLtoR") ||
        !irix_host_abi_context_has_function(context, "XmCreateLabel")) {
        fprintf(stderr, "translated Xt/Motif registries are incomplete\n");
        irix_host_abi_context_destroy(context);
        return 1;
    }
    if (irix_host_abi_context_copy_string(context, "native context probe", &text) != 0 ||
        irix_host_abi_context_copy_string(context, "ISO8859-1", &charset) != 0) {
        irix_host_abi_context_destroy(context);
        return 1;
    }
    args[0] = text;
    args[1] = charset;
    {
        IrixN32AbiArgument typed_args[2] = {
            { IRIX_N32_ABI_PTR32, 0, text },
            { IRIX_N32_ABI_PTR32, 0, charset }
        };
        uint64_t typed_result = 0;
        if (irix_host_abi_context_call_typed(context, "XmStringCreateLtoR",
                typed_args, 2, IRIX_HOST_ABI_RESULT_PTR32,
                &typed_result) != 0 || !typed_result) {
            irix_host_abi_context_destroy(context);
            return 2;
        }
        xm_string = (uint32_t)typed_result;
    }
    if (!xm_string) {
        irix_host_abi_context_destroy(context);
        return 2;
    }
    args[0] = xm_string;
    if (irix_host_abi_context_call(context, "XmStringFree", args, 1, NULL) != 0) {
        irix_host_abi_context_destroy(context);
        return 3;
    }
    if (getenv("IRIX_HOST_ABI_RUN_WIDGET_PROBE") &&
        guest_widget_probe(context) != 0) {
        irix_host_abi_context_destroy(context);
        return 5;
    }
    irix_host_abi_context_destroy(context);

    /* Exercise the public host-ABI wrapper resolved ahead of guest aliases. */
    host_xm_string = XmStringCreateLtoR("native host ABI probe", "ISO8859-1");
    if (!host_xm_string) {
        fprintf(stderr, "native XmStringCreateLtoR wrapper failed\n");
        return 4;
    }
    XmStringFree(host_xm_string);
    if (getenv("IRIX_HOST_ABI_RUN_WIDGET_PROBE") &&
        native_widget_probe() != 0)
        return 6;
    return 0;
}
