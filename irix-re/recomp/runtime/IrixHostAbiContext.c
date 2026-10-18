#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "IrixHostAbiContext.h"
#include "IrixMipsJit.h"

#include "IrixCLibExtraBridge.h"
#include "IrixCxxBridge.h"
#include "IrixFsBridge.h"
#include "IrixLibcBridge.h"
#include "IrixPosixBridge.h"
#include "IrixRuntimeLibcBridge.h"
#include "IrixRuntimeXtBridge.h"
#include "IrixRuntimeX11Bridge.h"
#include "IrixStdioBridge.h"
#include "IrixX11Bridge.h"
#include "IrixX11BridgeExtraAccess.h"
#include "IrixX11ConnectionBridge.h"
#include "IrixX11ContextBridge.h"
#include "IrixX11KeyboardBridge.h"
#include "IrixX11WindowBridge.h"
#include "IrixX11WmBridge.h"
#include "IrixXrmCompat.h"
#include "IrixXrmQuarkBridge.h"
#include "IrixGuest.h"

#include <dlfcn.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <glob.h>
#include <sys/stat.h>
#include <X11/Intrinsic.h>
#include <X11/IntrinsicP.h>
#include <X11/ShellP.h>
#include <Xm/Label.h>
#include <Xm/List.h>
#include <Xm/Text.h>
#include <Xm/TextF.h>
#include <Xm/ToggleB.h>
#include <Xm/ScrollBar.h>
#include <Xm/SelectioB.h>
#include <Xm/MessageB.h>
#include <Xm/FileSB.h>
#include <Xm/DragIcon.h>
#include <Xm/CutPaste.h>
#include <Xm/RowColumn.h>
#include <Xm/DropSMgr.h>
#include <Xm/PrimitiveP.h>
#include <Xm/ManagerP.h>
#include <Xm/ScrolledW.h>
#include <Xm/RepType.h>
#include <Xm/Xm.h>
#include <Xm/VendorS.h>

/* Keep this bridge independent of the host Motif string table. These names
 * are used only as Xt resource identifiers and their literal spellings are
 * the ABI value passed to host/guest resource conversion. */
#undef XmNanimationStyle
#define XmNanimationStyle "animationStyle"
#undef XmNbottomShadowPixmap
#define XmNbottomShadowPixmap "bottomShadowPixmap"
#undef XmNdropSiteActivity
#define XmNdropSiteActivity "dropSiteActivity"
#undef XmNdropSiteOperations
#define XmNdropSiteOperations "dropSiteOperations"
#undef XmNdropSiteType
#define XmNdropSiteType "dropSiteType"
#undef XmNforeground
#define XmNforeground "foreground"
#undef XmNlabelInsensitivePixmap
#define XmNlabelInsensitivePixmap "labelInsensitivePixmap"
#undef XmNlabelPixmap
#define XmNlabelPixmap "labelPixmap"
#undef XmNnumImportTargets
#define XmNnumImportTargets "numImportTargets"
#undef XmNsubMenuId
#define XmNsubMenuId "subMenuId"
#undef XmNtopShadowPixmap
#define XmNtopShadowPixmap "topShadowPixmap"
#undef XmNvisual
#define XmNvisual "visual"

/* Xm's native entry points are exported by libXm.  The implementation and
 * process-wide host/guest state live in libXt; libXm contains ABI-preserving
 * tail thunks to these private names. */
#ifdef IRIX_NATIVE32_DSO_BUILD
#define XmAddProtocolCallback irix_hostabi_impl_XmAddProtocolCallback
#define XmAddProtocols irix_hostabi_impl_XmAddProtocols
#define XmAddWMProtocolCallback irix_hostabi_impl_XmAddWMProtocolCallback
#define XmCreateDialogShell irix_hostabi_impl_XmCreateDialogShell
#define XmCreateDrawingArea irix_hostabi_impl_XmCreateDrawingArea
#define XmCreateForm irix_hostabi_impl_XmCreateForm
#define XmCreateLabel irix_hostabi_impl_XmCreateLabel
#define XmCreateMenuBar irix_hostabi_impl_XmCreateMenuBar
#define XmCreateMessageBox irix_hostabi_impl_XmCreateMessageBox
#define XmCreatePanedWindow irix_hostabi_impl_XmCreatePanedWindow
#define XmCreatePopupMenu irix_hostabi_impl_XmCreatePopupMenu
#define XmCreatePulldownMenu irix_hostabi_impl_XmCreatePulldownMenu
#define XmCreateScrollBar irix_hostabi_impl_XmCreateScrollBar
#define XmCreateScrolledList irix_hostabi_impl_XmCreateScrolledList
#define XmCreateSeparator irix_hostabi_impl_XmCreateSeparator
#define XmCreateText irix_hostabi_impl_XmCreateText
#define XmCreateTextField irix_hostabi_impl_XmCreateTextField
#define XmCreateArrowButton irix_hostabi_impl_XmCreateArrowButton
#define XmCreateArrowButtonGadget irix_hostabi_impl_XmCreateArrowButtonGadget
#define XmCreateCascadeButton irix_hostabi_impl_XmCreateCascadeButton
#define XmCreateCascadeButtonGadget irix_hostabi_impl_XmCreateCascadeButtonGadget
#define XmCreateDrawnButton irix_hostabi_impl_XmCreateDrawnButton
#define XmCreateFileSelectionBox irix_hostabi_impl_XmCreateFileSelectionBox
#define XmCreateFormDialog irix_hostabi_impl_XmCreateFormDialog
#define XmCreateFrame irix_hostabi_impl_XmCreateFrame
#define XmCreateInformationDialog irix_hostabi_impl_XmCreateInformationDialog
#define XmCreateLabelGadget irix_hostabi_impl_XmCreateLabelGadget
#define XmCreateMessageDialog irix_hostabi_impl_XmCreateMessageDialog
#define XmCreateOptionMenu irix_hostabi_impl_XmCreateOptionMenu
#define XmCreatePromptDialog irix_hostabi_impl_XmCreatePromptDialog
#define XmCreatePushButton irix_hostabi_impl_XmCreatePushButton
#define XmCreatePushButtonGadget irix_hostabi_impl_XmCreatePushButtonGadget
#define XmCreateQuestionDialog irix_hostabi_impl_XmCreateQuestionDialog
#define XmCreateRadioBox irix_hostabi_impl_XmCreateRadioBox
#define XmCreateRowColumn irix_hostabi_impl_XmCreateRowColumn
#define XmCreateScale irix_hostabi_impl_XmCreateScale
#define XmCreateScrolledText irix_hostabi_impl_XmCreateScrolledText
#define XmCreateScrolledWindow irix_hostabi_impl_XmCreateScrolledWindow
#define XmCreateSelectionDialog irix_hostabi_impl_XmCreateSelectionDialog
#define XmCreateSeparatorGadget irix_hostabi_impl_XmCreateSeparatorGadget
#define XmCreateTemplateDialog irix_hostabi_impl_XmCreateTemplateDialog
#define XmCreateToggleButton irix_hostabi_impl_XmCreateToggleButton
#define XmCreateToggleButtonGadget irix_hostabi_impl_XmCreateToggleButtonGadget
#define XmDropSiteRegister irix_hostabi_impl_XmDropSiteRegister
#define XmDropSiteUnregister irix_hostabi_impl_XmDropSiteUnregister
#define XmDropSiteUpdate irix_hostabi_impl_XmDropSiteUpdate
#define XmFontListCopy irix_hostabi_impl_XmFontListCopy
#define XmFontListCreate irix_hostabi_impl_XmFontListCreate
#define XmFontListFree irix_hostabi_impl_XmFontListFree
#define XmFontListFreeFontContext irix_hostabi_impl_XmFontListFreeFontContext
#define XmFontListGetNextFont irix_hostabi_impl_XmFontListGetNextFont
#define XmFontListInitFontContext irix_hostabi_impl_XmFontListInitFontContext
#define XmGetColors irix_hostabi_impl_XmGetColors
#define XmGetFocusWidget irix_hostabi_impl_XmGetFocusWidget
#define XmGetMenuCursor irix_hostabi_impl_XmGetMenuCursor
#define XmGetPixmap irix_hostabi_impl_XmGetPixmap
#define XmGetPixmapByDepth irix_hostabi_impl_XmGetPixmapByDepth
#define XmGetXmDisplay irix_hostabi_impl_XmGetXmDisplay
#define XmInternAtom irix_hostabi_impl_XmInternAtom
#define XmListSelectPos irix_hostabi_impl_XmListSelectPos
#define XmClipboardCopy irix_hostabi_impl_XmClipboardCopy
#define XmClipboardEndCopy irix_hostabi_impl_XmClipboardEndCopy
#define XmClipboardInquireLength irix_hostabi_impl_XmClipboardInquireLength
#define XmClipboardLock irix_hostabi_impl_XmClipboardLock
#define XmClipboardRetrieve irix_hostabi_impl_XmClipboardRetrieve
#define XmClipboardStartCopy irix_hostabi_impl_XmClipboardStartCopy
#define XmClipboardUnlock irix_hostabi_impl_XmClipboardUnlock
#define XmCreateDragIcon irix_hostabi_impl_XmCreateDragIcon
#define XmDragStart irix_hostabi_impl_XmDragStart
#define XmDropTransferStart irix_hostabi_impl_XmDropTransferStart
#define XmFileSelectionBoxGetChild irix_hostabi_impl_XmFileSelectionBoxGetChild
#define XmFileSelectionDoSearch irix_hostabi_impl_XmFileSelectionDoSearch
#define XmFontListAppendEntry irix_hostabi_impl_XmFontListAppendEntry
#define XmFontListEntryCreate irix_hostabi_impl_XmFontListEntryCreate
#define XmFontListEntryFree irix_hostabi_impl_XmFontListEntryFree
#define XmGetDestination irix_hostabi_impl_XmGetDestination
#define XmImMbLookupString irix_hostabi_impl_XmImMbLookupString
#define XmImRegister irix_hostabi_impl_XmImRegister
#define XmImSetFocusValues irix_hostabi_impl_XmImSetFocusValues
#define XmImSetValues irix_hostabi_impl_XmImSetValues
#define XmImUnregister irix_hostabi_impl_XmImUnregister
#define XmImUnsetFocus irix_hostabi_impl_XmImUnsetFocus
#define XmListAddItem irix_hostabi_impl_XmListAddItem
#define XmListAddItemsUnselected irix_hostabi_impl_XmListAddItemsUnselected
#define XmListDeleteAllItems irix_hostabi_impl_XmListDeleteAllItems
#define XmListDeleteItemsPos irix_hostabi_impl_XmListDeleteItemsPos
#define XmListDeletePos irix_hostabi_impl_XmListDeletePos
#define XmListDeletePositions irix_hostabi_impl_XmListDeletePositions
#define XmListDeselectAllItems irix_hostabi_impl_XmListDeselectAllItems
#define XmListDeselectPos irix_hostabi_impl_XmListDeselectPos
#define XmListGetKbdItemPos irix_hostabi_impl_XmListGetKbdItemPos
#define XmListGetMatchPos irix_hostabi_impl_XmListGetMatchPos
#define XmListGetSelectedPos irix_hostabi_impl_XmListGetSelectedPos
#define XmListPosSelected irix_hostabi_impl_XmListPosSelected
#define XmListReplaceItemsPos irix_hostabi_impl_XmListReplaceItemsPos
#define XmListSelectItem irix_hostabi_impl_XmListSelectItem
#define XmListSetItem irix_hostabi_impl_XmListSetItem
#define XmListSetKbdItemPos irix_hostabi_impl_XmListSetKbdItemPos
#define XmListSetPos irix_hostabi_impl_XmListSetPos
#define XmMenuPosition irix_hostabi_impl_XmMenuPosition
#define XmMessageBoxGetChild irix_hostabi_impl_XmMessageBoxGetChild
#define XmOptionButtonGadget irix_hostabi_impl_XmOptionButtonGadget
#define XmOptionLabelGadget irix_hostabi_impl_XmOptionLabelGadget
#define XmProcessTraversal irix_hostabi_impl_XmProcessTraversal
#define XmScrollBarSetValues irix_hostabi_impl_XmScrollBarSetValues
#define XmScrollVisible irix_hostabi_impl_XmScrollVisible
#define XmSelectionBoxGetChild irix_hostabi_impl_XmSelectionBoxGetChild
#define XmStringDraw irix_hostabi_impl_XmStringDraw
#define XmStringDrawUnderline irix_hostabi_impl_XmStringDrawUnderline
#define XmStringFreeContext irix_hostabi_impl_XmStringFreeContext
#define XmStringGetNextComponent irix_hostabi_impl_XmStringGetNextComponent
#define XmStringGetNextSegment irix_hostabi_impl_XmStringGetNextSegment
#define XmStringInitContext irix_hostabi_impl_XmStringInitContext
#define XmStringLtoRCreate irix_hostabi_impl_XmStringLtoRCreate
#define XmStringNConcat irix_hostabi_impl_XmStringNConcat
#define XmTextClearSelection irix_hostabi_impl_XmTextClearSelection
#define XmTextFieldClearSelection irix_hostabi_impl_XmTextFieldClearSelection
#define XmTextFieldGetCursorPosition irix_hostabi_impl_XmTextFieldGetCursorPosition
#define XmTextFieldGetEditable irix_hostabi_impl_XmTextFieldGetEditable
#define XmTextFieldGetInsertionPosition irix_hostabi_impl_XmTextFieldGetInsertionPosition
#define XmTextFieldGetLastPosition irix_hostabi_impl_XmTextFieldGetLastPosition
#define XmTextFieldGetSelection irix_hostabi_impl_XmTextFieldGetSelection
#define XmTextFieldGetSelectionPosition irix_hostabi_impl_XmTextFieldGetSelectionPosition
#define XmTextFieldGetString irix_hostabi_impl_XmTextFieldGetString
#define XmTextFieldInsert irix_hostabi_impl_XmTextFieldInsert
#define XmTextFieldRemove irix_hostabi_impl_XmTextFieldRemove
#define XmTextFieldSetCursorPosition irix_hostabi_impl_XmTextFieldSetCursorPosition
#define XmTextFieldSetHighlight irix_hostabi_impl_XmTextFieldSetHighlight
#define XmTextFieldSetInsertionPosition irix_hostabi_impl_XmTextFieldSetInsertionPosition
#define XmTextFieldSetSelection irix_hostabi_impl_XmTextFieldSetSelection
#define XmTextFieldSetString irix_hostabi_impl_XmTextFieldSetString
#define XmTextFieldShowPosition irix_hostabi_impl_XmTextFieldShowPosition
#define XmTextFieldXYToPos irix_hostabi_impl_XmTextFieldXYToPos
#define XmTextGetInsertionPosition irix_hostabi_impl_XmTextGetInsertionPosition
#define XmTextGetLastPosition irix_hostabi_impl_XmTextGetLastPosition
#define XmTextGetMaxLength irix_hostabi_impl_XmTextGetMaxLength
#define XmTextGetSelection irix_hostabi_impl_XmTextGetSelection
#define XmTextGetSelectionPosition irix_hostabi_impl_XmTextGetSelectionPosition
#define XmTextGetString irix_hostabi_impl_XmTextGetString
#define XmTextInsert irix_hostabi_impl_XmTextInsert
#define XmTextRemove irix_hostabi_impl_XmTextRemove
#define XmTextSetCursorPosition irix_hostabi_impl_XmTextSetCursorPosition
#define XmTextSetHighlight irix_hostabi_impl_XmTextSetHighlight
#define XmTextSetInsertionPosition irix_hostabi_impl_XmTextSetInsertionPosition
#define XmTextSetSelection irix_hostabi_impl_XmTextSetSelection
#define XmTextSetString irix_hostabi_impl_XmTextSetString
#define XmToggleButtonGadgetGetState irix_hostabi_impl_XmToggleButtonGadgetGetState
#define XmToggleButtonGadgetSetState irix_hostabi_impl_XmToggleButtonGadgetSetState
#define XmToggleButtonGetState irix_hostabi_impl_XmToggleButtonGetState
#define XmToggleButtonSetState irix_hostabi_impl_XmToggleButtonSetState
#define XmUpdateDisplay irix_hostabi_impl_XmUpdateDisplay
#define XmVaCreateSimpleRadioBox irix_hostabi_impl_XmVaCreateSimpleRadioBox
#define _XmBottomShadowColorDefault irix_hostabi_impl__XmBottomShadowColorDefault
#define _XmDestroyParentCallback irix_hostabi_impl__XmDestroyParentCallback
#define _XmGrabKeyboard irix_hostabi_impl__XmGrabKeyboard
#define _XmGrabPointer irix_hostabi_impl__XmGrabPointer
#define _XmMenuPopDown irix_hostabi_impl__XmMenuPopDown
#define _XmMoveObject irix_hostabi_impl__XmMoveObject
#define _XmOSBuildFileList irix_hostabi_impl__XmOSBuildFileList
#define _XmOSFileCompare irix_hostabi_impl__XmOSFileCompare
#define _XmOSFindPatternPart irix_hostabi_impl__XmOSFindPatternPart
#define _XmOSQualifyFileSpec irix_hostabi_impl__XmOSQualifyFileSpec
#define _XmPostPopupMenu irix_hostabi_impl__XmPostPopupMenu
#define _XmPrimitiveEnter irix_hostabi_impl__XmPrimitiveEnter
#define _XmPrimitiveLeave irix_hostabi_impl__XmPrimitiveLeave
#define _XmSetDestination irix_hostabi_impl__XmSetDestination
#define _XmShellIsExclusive irix_hostabi_impl__XmShellIsExclusive
#define _XmStringDraw irix_hostabi_impl__XmStringDraw
#define _XmStringGetTextConcat irix_hostabi_impl__XmStringGetTextConcat
#define _XmTopShadowColorDefault irix_hostabi_impl__XmTopShadowColorDefault
#define XmRemoveProtocolCallback irix_hostabi_impl_XmRemoveProtocolCallback
#define XmRepTypeGetId irix_hostabi_impl_XmRepTypeGetId
#define XmRepTypeRegister irix_hostabi_impl_XmRepTypeRegister
#define XmRepTypeValidValue irix_hostabi_impl_XmRepTypeValidValue
#define XmScrolledWindowSetAreas irix_hostabi_impl_XmScrolledWindowSetAreas
#define XmSetMenuCursor irix_hostabi_impl_XmSetMenuCursor
#define XmStringByteCompare irix_hostabi_impl_XmStringByteCompare
#define XmStringCompare irix_hostabi_impl_XmStringCompare
#define XmStringConcat irix_hostabi_impl_XmStringConcat
#define XmStringCopy irix_hostabi_impl_XmStringCopy
#define XmStringCreate irix_hostabi_impl_XmStringCreate
#define XmStringCreateLocalized irix_hostabi_impl_XmStringCreateLocalized
#define XmStringCreateLtoR irix_hostabi_impl_XmStringCreateLtoR
#define XmStringCreateSimple irix_hostabi_impl_XmStringCreateSimple
#define XmStringExtent irix_hostabi_impl_XmStringExtent
#define XmStringFree irix_hostabi_impl_XmStringFree
#define XmStringGetLtoR irix_hostabi_impl_XmStringGetLtoR
#define XmStringHeight irix_hostabi_impl_XmStringHeight
#define XmStringLength irix_hostabi_impl_XmStringLength
#define XmStringSegmentCreate irix_hostabi_impl_XmStringSegmentCreate
#define XmStringWidth irix_hostabi_impl_XmStringWidth
#define XmeGetDefaultRenderTable irix_hostabi_impl_XmeGetDefaultRenderTable
#define _XmMakeGeometryRequest irix_hostabi_impl__XmMakeGeometryRequest
#endif

extern int irix_x11_image_write_n32_event(IrixGuestRuntime *, IrixGuestAddr,
                                          IrixGuestAddr, const XEvent *);

#ifdef IRIX_HAVE_FAST_QUARKS
#include "irix_fast_quarks.inc"
#endif

#ifndef IRIX_HOST_ABI_CORE_IMAGE_DIR
#define IRIX_HOST_ABI_CORE_IMAGE_DIR ""
#endif
#ifndef IRIX_HOST_ABI_RUNTIME_TREE
#define IRIX_HOST_ABI_RUNTIME_TREE ""
#endif

typedef IrixGuestFunction (*IrixHostImportResolver)(const char *);
typedef void (*IrixCoreLibraryRegister)(IrixGuestRuntime *);
extern void irix_guest_register_gp_range(IrixGuestAddr base, uint32_t size,
                                         IrixGuestAddr gp);
extern void irix_x11_extra_init(IrixGuestRuntime *rt);
extern IrixGuestFunction irix_x11_extra_lookup(const char *name);
extern void irix_x11_bulk_init(IrixGuestRuntime *rt);
extern IrixGuestFunction irix_x11_bulk_lookup(const char *name);
extern void irix_x11_image_bridge_init(IrixGuestRuntime *rt);
extern IrixGuestFunction irix_x11_image_bridge_lookup(const char *name);
extern void irix_host_abi_register_core_imports(IrixGuestRuntime *,
                                                 IrixHostImportResolver);

typedef struct IrixHostDropCallback IrixHostDropCallback;
struct IrixHostAbiContext {
    IrixGuestRuntime runtime;
    pthread_mutex_t mutex;
    int ready;
    unsigned call_depth;
    unsigned drop_callback_count;
    IrixHostDropCallback *drop_callbacks;
};
static void host_drop_callbacks_free(IrixHostAbiContext *context);
static WidgetClass host_system_composite_class(void);

typedef struct IrixHostXmStringHandle {
    uint32_t magic;
    uint32_t guest_address;
    struct IrixHostXmStringHandle *next;
} IrixHostXmStringHandle;

typedef struct IrixHostOpaqueHandle {
    uint32_t magic;
    uint32_t guest_address;
    uint32_t parent_guest_address;
    uint32_t app_guest_address;
    uint32_t guest_display_address;
    uint32_t guest_screen_address;
    Display *host_display;
    Screen *host_screen;
    XtAppContext host_app_context;
    unsigned host_display_initialized;
    XrmDatabase host_resource_database;
    WidgetRec widget_rec;
    Widget widget_object;
    void *widget_storage;
    size_t widget_storage_size;
    void *host_constraint_storage;
    Widget *host_children;
    Cardinal host_child_count;
    char *host_app_name;
    char *host_app_class;
    unsigned kind;
    unsigned is_shell;
    unsigned managed;
    unsigned realized;
    int alive;
    XtPointer host_user_data;
    IrixGuestAddr drop_targets;
    uint32_t drop_target_count;
    struct IrixHostOpaqueHandle *next;
} IrixHostOpaqueHandle;

static void host_native_composite_insert_child(IrixHostOpaqueHandle *parent,
                                                IrixHostOpaqueHandle *child);
static void host_native_composite_change_managed(IrixHostOpaqueHandle *parent);
static void host_native_prepare_constraint(IrixHostOpaqueHandle *parent,
                                            IrixHostOpaqueHandle *child);
static void host_native_widget_initialize(IrixHostOpaqueHandle *handle,
    WidgetClass widget_class, ArgList args, Cardinal arg_count);
static void host_apply_initial_geometry(IrixHostOpaqueHandle *handle,
    ArgList args, Cardinal arg_count);
static int host_xt_get_scalar(IrixHostAbiContext *context,
                              IrixHostOpaqueHandle *handle,
                              const char *name, uint32_t *value);
static void host_sync_guest_geometry(IrixHostAbiContext *context,
                                     IrixHostOpaqueHandle *handle);

typedef struct IrixN32XrmOptionDescRec {
    uint32_t option;
    uint32_t specifier;
    uint32_t arg_kind;
    uint32_t value;
} IrixN32XrmOptionDescRec;

typedef struct IrixN32ArgRec {
    uint32_t name;
    uint32_t value;
} IrixN32ArgRec;

#define IRIX_HOST_XMSTRING_MAGIC 0x584d5331u
#define IRIX_HOST_OPAQUE_MAGIC 0x58485431u
#define IRIX_HOST_HANDLE_WIDGET 1u
#define IRIX_HOST_HANDLE_APP_CONTEXT 2u
#define IRIX_HOST_HANDLE_TRANSLATIONS 3u
#define IRIX_HOST_ABI_MAX_ARGS 32u

static pthread_once_t g_default_context_once = PTHREAD_ONCE_INIT;
static IrixHostAbiContext *g_default_context;
static pthread_mutex_t g_host_handle_mutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;
static IrixHostXmStringHandle *g_host_xmstrings;
static IrixHostOpaqueHandle *g_host_opaque_handles;
static unsigned g_host_abi_trap_log_count;
static CoreClassRec g_host_core_class;
static void *g_host_xt_handle;
typedef struct IrixHostMotifClassFacade {
    const char *symbol;
    const char *class_name;
    const char *superclass_symbol;
    unsigned type_flags;
    CoreClassRec record;
} IrixHostMotifClassFacade;

/* Netscape/XFE reads XmListRec.list.itemCount directly. This prefix follows
 * holyshit/usr/Motif-1.2/include/Xm/ListP.h; the translated Widget remains
 * opaque and is tracked separately. */
typedef struct IrixHostXmListPartPrefix {
    Dimension spacing;
    short item_spacing;
    Dimension margin_width;
    Dimension margin_height;
    XmFontList font;
    XmString *items;
    int item_count;
} IrixHostXmListPartPrefix;

typedef struct IrixHostXmListWidgetPrefix {
    CorePart core;
    XmPrimitivePart primitive;
    IrixHostXmListPartPrefix list;
} IrixHostXmListWidgetPrefix;

/* Netscape 4's XFE sources inspect XmDisplayRec.display.userGrabbed directly
 * in their event loop. Preserve the SGI Motif 1.2 layout in
 * holyshit/usr/Motif-1.2/include/Xm/DisplayP.h through that byte while
 * keeping the translated XmDisplay widget opaque. */
typedef struct IrixHostXmDisplayWidgetPrefix {
    CorePart core;
    CompositePart composite;
    ShellPart shell;
    WMShellPart wm;
    VendorShellPart vendor;
    TopLevelShellPart top_level;
    ApplicationShellPart application;
    unsigned char display_prefix_through_user_grabbed[3];
} IrixHostXmDisplayWidgetPrefix;

_Static_assert(offsetof(IrixHostXmListWidgetPrefix, list.item_count) ==
               sizeof(CorePart) + sizeof(XmPrimitivePart) +
               3u * sizeof(Dimension) + sizeof(short) +
               sizeof(XmFontList) +
               sizeof(XmString *),
               "host XmList prefix does not match its declared layout");
_Static_assert(offsetof(IrixHostXmDisplayWidgetPrefix,
                        display_prefix_through_user_grabbed) + 2u == 330u,
               "host XmDisplay prefix does not match Netscape XFE layout");
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
static IrixHostMotifClassFacade g_host_motif_classes[] = {
    { "xmPrimitiveWidgetClass", "XmPrimitive", "coreWidgetClass", 0x04u },
    { "xmManagerWidgetClass", "XmManager", "compositeWidgetClass", 0x0cu },
    { "xmLabelWidgetClass", "XmLabel", "xmPrimitiveWidgetClass", 0x04u },
    { "xmCascadeButtonWidgetClass", "XmCascadeButton", "xmLabelWidgetClass", 0x04u },
    { "xmArrowButtonWidgetClass", "XmArrowButton", "xmPrimitiveWidgetClass", 0x04u },
    { "xmDrawnButtonWidgetClass", "XmDrawnButton", "xmLabelWidgetClass", 0x04u },
    { "xmToggleButtonWidgetClass", "XmToggleButton", "xmLabelWidgetClass", 0x04u },
    { "xmPushButtonWidgetClass", "XmPushButton", "xmLabelWidgetClass", 0x04u },
    { "xmSeparatorWidgetClass", "XmSeparator", "xmPrimitiveWidgetClass", 0x04u },
    { "xmListWidgetClass", "XmList", "xmPrimitiveWidgetClass", 0x04u },
    { "xmScrollBarWidgetClass", "XmScrollBar", "xmPrimitiveWidgetClass", 0x04u },
    { "xmTextWidgetClass", "XmText", "xmPrimitiveWidgetClass", 0x04u },
    { "xmTextFieldWidgetClass", "XmTextField", "xmPrimitiveWidgetClass", 0x04u },
    { "xmSashWidgetClass", "XmSash", "xmPrimitiveWidgetClass", 0x04u },
    { "xmDrawingAreaWidgetClass", "XmDrawingArea", "xmManagerWidgetClass", 0x0cu },
    { "xmFrameWidgetClass", "XmFrame", "xmManagerWidgetClass", 0x0cu },
    { "xmPanedWindowWidgetClass", "XmPanedWindow", "xmManagerWidgetClass", 0x1cu },
    { "xmFormWidgetClass", "XmForm", "xmManagerWidgetClass", 0x1cu },
    { "xmRowColumnWidgetClass", "XmRowColumn", "xmManagerWidgetClass", 0x0cu },
    { "xmMessageBoxWidgetClass", "XmMessageBox", "xmManagerWidgetClass", 0x0cu },
    { "xmFileSelectionBoxWidgetClass", "XmFileSelectionBox", "xmManagerWidgetClass", 0x0cu },
    { "xmMenuShellWidgetClass", "XmMenuShell", "overrideShellWidgetClass", 0x24u },
    { "xmDialogShellWidgetClass", "XmDialogShell", "transientShellWidgetClass", 0x6cu }
};
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
static pthread_once_t g_host_xt_classes_once = PTHREAD_ONCE_INIT;
static WidgetClass g_host_application_shell_class;
static WidgetClass g_host_top_level_shell_class;
static WidgetClass g_host_shell_widget_class;
static int g_reported_callbacks_skipped;
static _Thread_local unsigned host_native_insert_depth;
static void host_report_callbacks_skipped(void);

static WidgetClass host_motif_class_from_facade(const char *symbol)
{
    size_t i;
    if (!symbol)
        return NULL;
    for (i = 0; i < sizeof(g_host_motif_classes) /
                    sizeof(g_host_motif_classes[0]); ++i)
        if (strcmp(g_host_motif_classes[i].symbol, symbol) == 0)
            {
            char record_name[96];
            const char *suffix = strstr(symbol, "WidgetClass");
            void *record = NULL;
            if (suffix && (size_t)(suffix - symbol) + 9 < sizeof(record_name)) {
                snprintf(record_name, sizeof(record_name), "%.*sClassRec",
                         (int)(suffix - symbol), symbol);
                record = dlsym(RTLD_DEFAULT, record_name);
            }
            return record ? (WidgetClass)record :
                            (WidgetClass)&g_host_motif_classes[i].record;
        }
    return NULL;
}

static void host_prepare_xt_classes_once(void)
{
    static const char *const class_names[] = {
        "shellWidgetClass",
        "applicationShellWidgetClass",
        "topLevelShellWidgetClass",
        "wmShellWidgetClass",
        "transientShellWidgetClass",
        "overrideShellWidgetClass"
    };
    size_t i;

    if (!g_host_xt_handle)
        g_host_xt_handle = dlopen("libXt.so.6", RTLD_NOW | RTLD_NOLOAD);
    if (!g_host_xt_handle)
        return;

    memset(&g_host_core_class, 0, sizeof(g_host_core_class));
    g_host_core_class.core_class.class_name = (String)"HostAbiCore";
    g_host_core_class.core_class.widget_size = sizeof(WidgetRec);
    g_host_core_class.core_class.class_inited = 0x04u;
    g_host_core_class.core_class.version = XtVersion;

    for (i = 0; i < sizeof(class_names) / sizeof(class_names[0]); ++i) {
        WidgetClass *host_slot = (WidgetClass *)dlsym(g_host_xt_handle,
                                                       class_names[i]);
        WidgetClass *native_slot = (WidgetClass *)dlsym(RTLD_DEFAULT,
                                                         class_names[i]);
        WidgetClass widget_class = host_slot ? *host_slot : NULL;
        if (!widget_class)
            continue;
        if (strstr(class_names[i], "shellWidgetClass") &&
            !strstr(class_names[i], "applicationShell") &&
            !strstr(class_names[i], "topLevelShell") &&
            !strstr(class_names[i], "wmShell") &&
            !strstr(class_names[i], "transientShell"))
            widget_class->core_class.class_inited |= 0x24u;
        else if (strstr(class_names[i], "applicationShell") ||
            strstr(class_names[i], "topLevelShell"))
            widget_class->core_class.class_inited |= 0xecu;
        else if (strstr(class_names[i], "wmShell") ||
                 strstr(class_names[i], "transientShell"))
            widget_class->core_class.class_inited |= 0x6cu;
        else
            widget_class->core_class.class_inited |= 0x2cu;
        if (native_slot)
            *native_slot = widget_class;
        if (strcmp(class_names[i], "applicationShellWidgetClass") == 0)
            g_host_application_shell_class = widget_class;
        else if (strcmp(class_names[i], "topLevelShellWidgetClass") == 0)
            g_host_top_level_shell_class = widget_class;
        else if (strcmp(class_names[i], "shellWidgetClass") == 0)
            g_host_shell_widget_class = widget_class;
    }

    /*
     * The translated Motif DSOs expose guest-entry functions, not host-format
     * WidgetClass data.  Netscape's link-only placeholders leave these slots
     * null.  Supply small host Xt class records for native type checks while
     * keeping widget creation and rendering in the translated guest path.
     */
    for (i = 0; i < sizeof(g_host_motif_classes) /
                    sizeof(g_host_motif_classes[0]); ++i) {
        IrixHostMotifClassFacade *facade = &g_host_motif_classes[i];
        WidgetClass *native_slot = (WidgetClass *)dlsym(RTLD_DEFAULT,
                                                        facade->symbol);
        WidgetClass superclass = NULL;
        if (facade->superclass_symbol[0] == 'x')
            superclass = host_motif_class_from_facade(
                facade->superclass_symbol);
        else {
            WidgetClass *superclass_slot = (WidgetClass *)dlsym(
                g_host_xt_handle, facade->superclass_symbol);
            if (superclass_slot)
                superclass = *superclass_slot;
        }
        memset(&facade->record, 0, sizeof(facade->record));
        facade->record.core_class.superclass = superclass;
        facade->record.core_class.class_name = (String)facade->class_name;
        facade->record.core_class.widget_size =
            strcmp(facade->symbol, "xmListWidgetClass") == 0
                ? sizeof(IrixHostXmListWidgetPrefix) : sizeof(WidgetRec);
        facade->record.core_class.class_inited = facade->type_flags;
        facade->record.core_class.version = XtVersion;
        {
            WidgetClass native_record = host_motif_class_from_facade(facade->symbol);
            if (native_record != (WidgetClass)&facade->record)
                memcpy(native_record, &facade->record, sizeof(CoreClassRec));
            if (strcmp(facade->symbol, "xmManagerWidgetClass") == 0 &&
                native_record != (WidgetClass)&facade->record) {
                WidgetClass system_composite = host_system_composite_class();
                if (system_composite)
                    ((CompositeWidgetClass)native_record)->composite_class =
                        ((CompositeWidgetClass)system_composite)->composite_class;
            }
            if (native_slot && !*native_slot)
                *native_slot = native_record;
        }
    }
}

static void host_prepare_xt_classes(void)
{
    pthread_once(&g_host_xt_classes_once, host_prepare_xt_classes_once);
}

static void host_xt_adopt_display(IrixHostOpaqueHandle *app_handle)
{
    /*
     * Do not initialize the host's Xt against the bridge Display.  Xt's
     * internal calls are ELF-interposed by the translated IRIX libXt in this
     * process, so mixing a native XtAppContext with that library is unsafe.
     * Event processing stays on the translated Xt side instead.
     */
    (void)app_handle;
}


static void create_default_context(void)
{
    g_default_context = irix_host_abi_context_create();
}

static IrixGuestFunction host_resolve(const char *name)
{
    IrixGuestFunction fn;
#define TRY_LOOKUP(expr) do { fn = (expr); if (fn) return fn; } while (0)
    TRY_LOOKUP(irix_libc_bridge_lookup(name));
    TRY_LOOKUP(irix_posix_bridge_lookup(name));
    TRY_LOOKUP(irix_fs_bridge_lookup(name));
    TRY_LOOKUP(irix_stdio_bridge_lookup(name));
    TRY_LOOKUP(irix_clib_extra_bridge_lookup(name));
    TRY_LOOKUP(irix_cxx_bridge_lookup(name));
    TRY_LOOKUP(irix_runtime_libc_bridge_lookup(name));
    TRY_LOOKUP(irix_xrm_quark_bridge_lookup(name));
    TRY_LOOKUP(irix_xrm_compat_lookup(name));
    TRY_LOOKUP(irix_x11_bridge_lookup(name));
    TRY_LOOKUP(irix_x11_extra_lookup(name));
    TRY_LOOKUP(irix_x11_bulk_lookup(name));
    TRY_LOOKUP(irix_x11_image_bridge_lookup(name));
    TRY_LOOKUP(irix_x11_keyboard_bridge_lookup(name));
    TRY_LOOKUP(irix_x11_connection_bridge_lookup(name));
    TRY_LOOKUP(irix_x11_context_bridge_lookup(name));
    TRY_LOOKUP(irix_x11_window_bridge_lookup(name));
    TRY_LOOKUP(irix_x11_wm_bridge_lookup(name));
    TRY_LOOKUP(irix_runtime_x11_bridge_lookup(name));
    TRY_LOOKUP(irix_runtime_xt_bridge_lookup(name));
#undef TRY_LOOKUP
    return NULL;
}

static int load_blob(IrixGuestRuntime *rt, const char *path,
                     IrixGuestAddr base)
{
    FILE *file = fopen(path, "rb");
    long length;
    void *destination;
    if (!file)
        return -1;
    if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) <= 0 ||
        fseek(file, 0, SEEK_SET) != 0 ||
        (unsigned long)length > UINT32_MAX ||
        irix_guest_map(rt, base, (uint32_t)length, 7u) != 0) {
        fclose(file);
        return -1;
    }
    destination = irix_guest_ptr(rt, base, (size_t)length);
    if (!destination || fread(destination, 1, (size_t)length, file) !=
                        (size_t)length) {
        fclose(file);
        return -1;
    }
    fclose(file);
    return 0;
}

static int load_core_image(IrixGuestRuntime *rt)
{
    char path[4096];
    if (snprintf(path, sizeof(path), "%s/main.bin",
                 IRIX_HOST_ABI_CORE_IMAGE_DIR) >= (int)sizeof(path) ||
        load_blob(rt, path, 0x50000000u) != 0)
        return -1;
    if (snprintf(path, sizeof(path), "%s/small.bin",
                 IRIX_HOST_ABI_CORE_IMAGE_DIR) >= (int)sizeof(path) ||
        load_blob(rt, path, 0x60000000u) != 0)
        return -1;
    irix_guest_register_gp_range(0x50000000u, 0x10000000u, 0x60008000u);
    irix_guest_register_gp_range(0x60000000u, 0x00010000u, 0x60008000u);
    return 0;
}

static int load_library_image(IrixGuestRuntime *rt, const char *relative)
{
    char path[4096];
    IrixGuestElfInfo info;
    size_t first_segment = rt->memory.count;
    size_t i;
    if (snprintf(path, sizeof(path), "%s/%s", IRIX_HOST_ABI_RUNTIME_TREE,
                 relative) >= (int)sizeof(path) ||
        irix_guest_load_elf32be_info(rt, path, &info) != 0) {
        fprintf(stderr, "IRIX host ABI: cannot map guest library %s\n", relative);
        return -1;
    }
    if (info.gp_value) {
        for (i = first_segment; i < rt->memory.count; ++i) {
            IrixGuestSegment *segment = &rt->memory.segments[i];
            irix_guest_register_gp_range(segment->guest_base, segment->size,
                                         info.gp_value);
        }
    }
    return 0;
}

static uint16_t host_file_u16be(const unsigned char *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static uint32_t host_file_u32be(const unsigned char *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static int host_elf_vaddr_to_offset(const unsigned char *file,
                                    size_t file_size,
                                    uint32_t phoff, uint16_t phentsize,
                                    uint16_t phnum, uint32_t address,
                                    size_t *offset_out)
{
    uint16_t i;
    if (phentsize < 32u)
        return -1;
    for (i = 0; i < phnum; ++i) {
        size_t ph = (size_t)phoff + (size_t)i * phentsize;
        uint32_t type, file_offset, virtual_address, file_bytes;
        if (ph > file_size || file_size - ph < phentsize)
            return -1;
        type = host_file_u32be(file + ph);
        if (type != 1u) /* PT_LOAD */
            continue;
        file_offset = host_file_u32be(file + ph + 4u);
        virtual_address = host_file_u32be(file + ph + 8u);
        file_bytes = host_file_u32be(file + ph + 16u);
        if (address >= virtual_address &&
            address - virtual_address < file_bytes) {
            size_t offset = (size_t)file_offset +
                            (size_t)(address - virtual_address);
            if (offset >= file_size)
                return -1;
            *offset_out = offset;
            return 0;
        }
    }
    return -1;
}

static IrixGuestAddr host_bridge_import_address(IrixGuestRuntime *rt,
                                                const char *name)
{
    IrixGuestFunction bridge = host_resolve(name);
    size_t i;
    if (!bridge)
        return 0;
    for (i = 0; i < rt->function_count; ++i) {
        IrixGuestFunctionEntry *entry = &rt->functions[i];
        if (entry->function == bridge && entry->name &&
            strcmp(entry->name, name) == 0)
            return entry->address;
    }
    return 0;
}

static IrixGuestFunctionEntry *host_translated_function(
    IrixGuestRuntime *rt, const char *name)
{
    size_t i;
    if (!rt || !name)
        return NULL;
    for (i = 0; i < rt->function_count; ++i) {
        IrixGuestFunctionEntry *entry = &rt->functions[i];
        if (entry->address >= 0x50000000u && entry->name &&
            strcmp(entry->name, name) == 0 && entry->function &&
            !(entry->flags & (IRIX_GUEST_FUNCTION_NATIVE |
                              IRIX_GUEST_FUNCTION_INTERIOR)))
            return entry;
    }
    return NULL;
}

/*
 * The host ABI runtime maps SGI DSOs for guest data and globals but executes
 * their translated exports.  Register exact-address aliases for public
 * original Xt/Motif functions so an indirect MIPS call through a quickstart
 * GOT is routed to the matching translated function instead of interpreting
 * the original DSO body in a second state universe.
 */
static size_t host_register_guest_dso_aliases(IrixGuestRuntime *rt,
                                               const char *path)
{
    enum {
        DT_SYMTAB_VALUE = 6,
        DT_STRTAB_VALUE = 5,
        DT_MIPS_SYMTABNO_VALUE = 0x70000011
    };
    FILE *stream = fopen(path, "rb");
    unsigned char *file = NULL;
    long length;
    size_t file_size, dynamic_offset = 0, dynamic_size = 0;
    size_t registered = 0, cursor;
    uint32_t phoff, symtab = 0, strtab = 0, symtabno = 0;
    uint16_t phentsize, phnum, i;
    if (!stream)
        return 0;
    if (fseek(stream, 0, SEEK_END) != 0 || (length = ftell(stream)) < 52 ||
        fseek(stream, 0, SEEK_SET) != 0) {
        fclose(stream);
        return 0;
    }
    file_size = (size_t)length;
    file = (unsigned char *)malloc(file_size);
    if (!file || fread(file, 1, file_size, stream) != file_size) {
        free(file);
        fclose(stream);
        return 0;
    }
    fclose(stream);
    if (memcmp(file, "\177ELF", 4u) != 0 || file[4] != 1u || file[5] != 2u)
        goto done;
    phoff = host_file_u32be(file + 28u);
    phentsize = host_file_u16be(file + 42u);
    phnum = host_file_u16be(file + 44u);
    if (phentsize < 32u)
        goto done;
    for (i = 0; i < phnum; ++i) {
        size_t ph = (size_t)phoff + (size_t)i * phentsize;
        if (ph > file_size || file_size - ph < phentsize)
            goto done;
        if (host_file_u32be(file + ph) == 2u) { /* PT_DYNAMIC */
            uint32_t address = host_file_u32be(file + ph + 8u);
            dynamic_size = host_file_u32be(file + ph + 16u);
            if (host_elf_vaddr_to_offset(file, file_size, phoff,
                    phentsize, phnum, address, &dynamic_offset) != 0)
                goto done;
            break;
        }
    }
    if (!dynamic_size)
        goto done;
    for (cursor = 0; cursor + 8u <= dynamic_size; cursor += 8u) {
        size_t at = dynamic_offset + cursor;
        int32_t tag;
        uint32_t value;
        if (at > file_size || file_size - at < 8u)
            goto done;
        tag = (int32_t)host_file_u32be(file + at);
        value = host_file_u32be(file + at + 4u);
        if (tag == 0)
            break;
        if (tag == DT_SYMTAB_VALUE)
            symtab = value;
        else if (tag == DT_STRTAB_VALUE)
            strtab = value;
        else if (tag == DT_MIPS_SYMTABNO_VALUE)
            symtabno = value;
    }
    if (!symtab || !strtab || !symtabno)
        goto done;
    for (i = 0; i < symtabno; ++i) {
        size_t symbol_offset, name_offset;
        uint32_t name_index, guest_address;
        uint16_t section_index;
        const char *name;
        IrixGuestFunctionEntry *translated;
        IrixGuestFunctionEntry *existing;
        if (host_elf_vaddr_to_offset(file, file_size, phoff, phentsize,
                phnum, symtab + (uint32_t)i * 16u, &symbol_offset) != 0 ||
            symbol_offset > file_size || file_size - symbol_offset < 16u)
            break;
        section_index = host_file_u16be(file + symbol_offset + 14u);
        if (section_index == 0u ||
            (file[symbol_offset + 12u] & 0x0fu) != 2u) /* STT_FUNC */
            continue;
        guest_address = host_file_u32be(file + symbol_offset + 4u);
        if (!guest_address || !irix_guest_ptr(rt, guest_address, 4u))
            continue;
        name_index = host_file_u32be(file + symbol_offset);
        if (host_elf_vaddr_to_offset(file, file_size, phoff, phentsize,
                phnum, strtab + name_index, &name_offset) != 0 ||
            name_offset >= file_size ||
            !memchr(file + name_offset, '\0', file_size - name_offset))
            continue;
        name = (const char *)(file + name_offset);
        if (strncmp(name, "Xt", 2u) != 0 &&
            strncmp(name, "_Xt", 3u) != 0 &&
            strncmp(name, "Xm", 2u) != 0 &&
            strncmp(name, "_Xm", 3u) != 0)
            continue;
        translated = host_translated_function(rt, name);
        if (!translated)
            continue;
        existing = irix_guest_find_function(rt, guest_address);
        if (existing && existing->address == guest_address)
            continue;
        if (irix_guest_register_recompiled_function(rt, guest_address,
                translated->function, translated->name) == 0)
            ++registered;
    }
done:
    free(file);
    return registered;
}

/*
 * The host ABI context maps fixed-address SGI DSOs without running rld. Their
 * quickstart GOTs can therefore retain function addresses from a different
 * libc build (for example, libX11's malloc slot points into an unrelated
 * libc helper). Rebind only undefined function GOT entries for which the
 * existing guest-to-host bridge table has an exact symbol implementation.
 */
static size_t host_patch_guest_dso_imports(IrixGuestRuntime *rt,
                                          const char *path)
{
    enum {
        DT_PLTGOT_VALUE = 3,
        DT_SYMTAB_VALUE = 6,
        DT_STRTAB_VALUE = 5,
        DT_MIPS_LOCAL_GOTNO_VALUE = 0x7000000a,
        DT_MIPS_SYMTABNO_VALUE = 0x70000011,
        DT_MIPS_GOTSYM_VALUE = 0x70000013
    };
    FILE *stream = fopen(path, "rb");
    unsigned char *file = NULL;
    long length;
    size_t file_size, dynamic_offset, patched = 0;
    uint32_t phoff, pltgot = 0, symtab = 0, strtab = 0;
    uint32_t local_gotno = 0, symtabno = 0, gotsym = 0;
    uint16_t phentsize, phnum;
    size_t dynamic_size = 0, cursor;
    uint16_t i;
    if (!stream)
        return 0;
    if (fseek(stream, 0, SEEK_END) != 0 || (length = ftell(stream)) < 52 ||
        fseek(stream, 0, SEEK_SET) != 0) {
        fclose(stream);
        return 0;
    }
    file_size = (size_t)length;
    file = (unsigned char *)malloc(file_size);
    if (!file || fread(file, 1, file_size, stream) != file_size) {
        free(file);
        fclose(stream);
        return 0;
    }
    fclose(stream);
    if (memcmp(file, "\177ELF", 4u) != 0 || file[4] != 1u || file[5] != 2u)
        goto done;
    phoff = host_file_u32be(file + 28u);
    phentsize = host_file_u16be(file + 42u);
    phnum = host_file_u16be(file + 44u);
    if (phentsize < 32u)
        goto done;
    for (i = 0; i < phnum; ++i) {
        size_t ph = (size_t)phoff + (size_t)i * phentsize;
        if (ph > file_size || file_size - ph < phentsize)
            goto done;
        if (host_file_u32be(file + ph) == 2u) { /* PT_DYNAMIC */
            uint32_t virtual_address = host_file_u32be(file + ph + 8u);
            uint32_t bytes = host_file_u32be(file + ph + 16u);
            if (host_elf_vaddr_to_offset(file, file_size, phoff,
                    phentsize, phnum, virtual_address,
                    &dynamic_offset) != 0)
                goto done;
            dynamic_size = bytes;
            break;
        }
    }
    if (!dynamic_size)
        goto done;
    for (cursor = 0; cursor + 8u <= dynamic_size; cursor += 8u) {
        size_t at = dynamic_offset + cursor;
        int32_t tag;
        uint32_t value;
        if (at > file_size || file_size - at < 8u)
            goto done;
        tag = (int32_t)host_file_u32be(file + at);
        value = host_file_u32be(file + at + 4u);
        if (tag == 0)
            break;
        switch (tag) {
        case DT_PLTGOT_VALUE: pltgot = value; break;
        case DT_SYMTAB_VALUE: symtab = value; break;
        case DT_STRTAB_VALUE: strtab = value; break;
        case DT_MIPS_LOCAL_GOTNO_VALUE: local_gotno = value; break;
        case DT_MIPS_SYMTABNO_VALUE: symtabno = value; break;
        case DT_MIPS_GOTSYM_VALUE: gotsym = value; break;
        default: break;
        }
    }
    if (!pltgot || !symtab || !strtab || !symtabno || gotsym >= symtabno)
        goto done;
    for (i = (uint16_t)gotsym; i < (uint16_t)symtabno; ++i) {
        size_t symbol_offset, name_offset, got_offset;
        uint32_t name_index, guest_function;
        uint16_t section_index;
        const char *name;
        if (host_elf_vaddr_to_offset(file, file_size, phoff, phentsize,
                phnum, symtab + (uint32_t)i * 16u, &symbol_offset) != 0 ||
            symbol_offset > file_size || file_size - symbol_offset < 16u)
            break;
        section_index = host_file_u16be(file + symbol_offset + 14u);
        if (section_index != 0u ||
            (file[symbol_offset + 12u] & 0x0fu) != 2u) /* SHN_UNDEF, STT_FUNC */
            continue;
        name_index = host_file_u32be(file + symbol_offset);
        if (host_elf_vaddr_to_offset(file, file_size, phoff, phentsize,
                phnum, strtab + name_index, &name_offset) != 0 ||
            name_offset >= file_size ||
            !memchr(file + name_offset, '\0', file_size - name_offset))
            continue;
        name = (const char *)(file + name_offset);
        guest_function = host_bridge_import_address(rt, name);
        if (!guest_function)
            continue;
        got_offset = pltgot + 4u * (local_gotno + (uint32_t)i - gotsym);
        if (!irix_guest_ptr(rt, got_offset, 4u))
            continue;
        irix_guest_write32be(rt, got_offset, guest_function);
        ++patched;
    }
done:
    free(file);
    return patched;
}

static int register_core_libraries(IrixGuestRuntime *rt)
{
    static const char *const names[] = { "Xt", "Xm", "Sgm", "vk", "Mrm", "Uil" };
    size_t i;
    for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        char symbol[96];
        IrixCoreLibraryRegister reg;
        snprintf(symbol, sizeof(symbol), "irix_register_recompiled_%s", names[i]);
        reg = (IrixCoreLibraryRegister)dlsym(RTLD_DEFAULT, symbol);
        if (!reg) {
            fprintf(stderr, "IRIX host ABI: missing translated registry %s\n", symbol);
            return -1;
        }
        reg(rt);
    }
    return 0;
}

static void host_patch_guest_library_imports(IrixGuestRuntime *rt)
{
    static const char *const paths[] = {
        "usr/lib32/libX11.so.1",
        "usr/lib32/libXt.so",
        "usr/lib32/libXm.so.1",
        "usr/lib32/libSgm.so.1",
        "usr/lib32/libvk.so.1",
        "usr/lib32/libMrm.so.1",
        "usr/lib32/libUil.so.1"
    };
    size_t i;
    for (i = 0; i < sizeof(paths) / sizeof(paths[0]); ++i) {
        char path[4096];
        size_t patched, aliases;
        if (snprintf(path, sizeof(path), "%s/%s",
                     IRIX_HOST_ABI_RUNTIME_TREE, paths[i]) >=
            (int)sizeof(path))
            continue;
        aliases = host_register_guest_dso_aliases(rt, path);
        if (aliases)
            fprintf(stderr,
                "libirixhostabi: registered %zu translated function aliases from %s\n",
                aliases, paths[i]);
        patched = host_patch_guest_dso_imports(rt, path);
        if (patched)
            fprintf(stderr,
                "libirixhostabi: rebound %zu imported functions in %s to guest bridge slots\n",
                patched, paths[i]);
    }
}

IrixHostAbiContext *irix_host_abi_context_create(void)
{
    IrixHostAbiContext *context = calloc(1, sizeof(*context));
    pthread_mutexattr_t mutex_attr;
    int mutex_status;
    if (!context)
        return NULL;
    if (pthread_mutexattr_init(&mutex_attr) != 0)
        goto fail;
    mutex_status = pthread_mutexattr_settype(&mutex_attr, PTHREAD_MUTEX_RECURSIVE);
    if (!mutex_status)
        mutex_status = pthread_mutex_init(&context->mutex, &mutex_attr);
    pthread_mutexattr_destroy(&mutex_attr);
    if (mutex_status)
        goto fail;
    irix_guest_runtime_init(&context->runtime);
    context->runtime.runtime_flags |= IRIX_GUEST_RUNTIME_HOST_ABI;
    irix_guest_set_rootfs(&context->runtime, IRIX_HOST_ABI_RUNTIME_TREE);
    if (load_core_image(&context->runtime) != 0 ||
        load_library_image(&context->runtime, "lib32/libc.so.1") != 0 ||
        load_library_image(&context->runtime, "usr/lib32/libX11.so.1") != 0 ||
        load_library_image(&context->runtime, "usr/lib32/libXt.so") != 0 ||
        load_library_image(&context->runtime, "usr/lib32/libXm.so.1") != 0 ||
        load_library_image(&context->runtime, "usr/lib32/libSgm.so.1") != 0 ||
        load_library_image(&context->runtime, "usr/lib32/libvk.so.1") != 0 ||
        load_library_image(&context->runtime, "usr/lib32/libMrm.so.1") != 0 ||
        load_library_image(&context->runtime, "usr/lib32/libUil.so.1") != 0 ||
        irix_libc_bridge_init(&context->runtime, 0x30000000u, 0x04000000u) != 0 ||
        irix_stdio_bridge_init(&context->runtime) != 0 ||
        irix_guest_map(&context->runtime, 0x7f600000u, 0x00800000u, 3u) != 0)
        goto fail_runtime;

    irix_posix_bridge_init(&context->runtime);
    irix_fs_bridge_init(&context->runtime);
    irix_clib_extra_bridge_init(&context->runtime);
    irix_cxx_bridge_init(&context->runtime);
    irix_runtime_libc_bridge_init(&context->runtime);
    irix_xrm_quark_bridge_init(&context->runtime);
    irix_xrm_compat_init(&context->runtime);
#ifdef IRIX_HAVE_FAST_QUARKS
    irix_xrm_quark_bridge_set_fast_table(irix_fast_quark_names,
                                         irix_fast_quark_count);
    irix_x11_bridge_init(&context->runtime, irix_fast_quark_names,
                         irix_fast_quark_count);
#else
    irix_x11_bridge_init(&context->runtime, NULL, 0);
#endif
    irix_x11_extra_init(&context->runtime);
    irix_x11_bulk_init(&context->runtime);
    irix_x11_keyboard_bridge_init(&context->runtime);
    irix_x11_connection_bridge_init(&context->runtime);
    irix_x11_context_bridge_init(&context->runtime);
    irix_x11_window_bridge_init(&context->runtime);
    irix_x11_image_bridge_init(&context->runtime);
    irix_x11_wm_bridge_init(&context->runtime);
    irix_runtime_x11_bridge_init(&context->runtime);
    irix_runtime_xt_bridge_init(&context->runtime);
    irix_host_abi_register_core_imports(&context->runtime, host_resolve);
    if (register_core_libraries(&context->runtime) != 0)
        goto fail_runtime;
    host_patch_guest_library_imports(&context->runtime);
    context->ready = 1;
    /* Install the SGI root lifecycle hooks from this translated image. The
     * desktop runner's original-DSO bootstrap uses a different global state
     * universe and must not initialize native host-ABI client widgets. */
    if (irix_host_abi_context_call(context, "_XmInitializeExtensions",
                                   NULL, 0u, NULL) != 0)
        goto fail_runtime;
    return context;

fail_runtime:
    irix_guest_runtime_destroy(&context->runtime);
    pthread_mutex_destroy(&context->mutex);
fail:
    free(context);
    return NULL;
}

void irix_host_abi_context_destroy(IrixHostAbiContext *context)
{
    if (!context)
        return;
    host_drop_callbacks_free(context);
    irix_guest_runtime_destroy(&context->runtime);
    pthread_mutex_destroy(&context->mutex);
    free(context);
}

int irix_host_abi_context_call_typed(IrixHostAbiContext *context,
                                     const char *name,
                                     const IrixN32AbiArgument *args,
                                     size_t arg_count,
                                     IrixHostAbiResultKind result_kind,
                                     uint64_t *result_bits)
{
    IrixGuestFunctionEntry *entry = NULL;
    IrixGuestCpu cpu;
    size_t i;
    int status;
    if (!context || !context->ready || !name ||
        arg_count > IRIX_HOST_ABI_MAX_ARGS ||
        (arg_count && !args) || result_kind < IRIX_HOST_ABI_RESULT_VOID ||
        result_kind > IRIX_HOST_ABI_RESULT_F64)
        return -1;
    for (i = 0; i < context->runtime.function_count; ++i) {
        IrixGuestFunctionEntry *candidate = &context->runtime.functions[i];
        if (candidate->name && strcmp(candidate->name, name) == 0) {
            entry = candidate;
            break;
        }
    }
    if (!entry)
        return -1;
    pthread_mutex_lock(&context->mutex);
    if (context->call_depth >= 32u) {
        pthread_mutex_unlock(&context->mutex);
        return -1;
    }
    /* A trapped native-to-guest call is reported to its caller as a failure,
     * but it must not poison later independent Xt/Motif calls. Preserve trap
     * state across recursive calls and clear it only at a new outer boundary. */
    if (context->call_depth == 0u) {
        context->runtime.trapped = 0;
        context->runtime.trap_code = 0;
        context->runtime.fault_addr = 0;
        context->runtime.trap_pc = 0;
    }
    memset(&cpu, 0, sizeof(cpu));
    cpu.gpr[28] = 0x60008000u;
    cpu.gpr[29] = 0x7fdfff00u - context->call_depth++ * 0x20000u;
    /* N32 PIC callees derive their module GP from the incoming $t9. */
    cpu.gpr[25] = entry->address;
    status = irix_n32_abi_pack_arguments(&context->runtime, &cpu, args,
                                         arg_count);
    if (status == 0) {
        int previous_jit_state = irix_mips_jit_set_thread_disabled(1);
        status = irix_guest_call(&context->runtime, &cpu, entry->address);
        (void)irix_mips_jit_set_thread_disabled(previous_jit_state);
    }
    if (context->runtime.trapped) {
        /* A broken callback/event-loop path can retry a failed translated
         * call continuously. Keep diagnostics useful without filling disks. */
        if (g_host_abi_trap_log_count++ < 32u) {
            IrixGuestAddr stack_base = (IrixGuestAddr)(uint32_t)cpu.gpr[29];
            unsigned offset;
            fprintf(stderr,
            "libirixhostabi: guest call %s trapped code=%u pc=%08x fault=%08x sp=%08llx ra=%08llx a0-a3=%08llx,%08llx,%08llx,%08llx t0=%08llx\n",
            name, context->runtime.trap_code, context->runtime.current_pc,
            context->runtime.fault_addr,
            (unsigned long long)cpu.gpr[29],
            (unsigned long long)cpu.gpr[31],
            (unsigned long long)cpu.gpr[4],
            (unsigned long long)cpu.gpr[5],
            (unsigned long long)cpu.gpr[6],
            (unsigned long long)cpu.gpr[7],
            (unsigned long long)cpu.gpr[12]);
            if (getenv("IRIX_HOST_ABI_TRACE_WIDGETS")) {
                unsigned reg;
                for (reg = 4u; reg <= 7u; ++reg) {
                    IrixGuestAddr candidate =
                        (IrixGuestAddr)(uint32_t)cpu.gpr[reg];
                    const char *message = candidate
                        ? irix_guest_cstr(&context->runtime, candidate, 256u)
                        : NULL;
                    if (message)
                        fprintf(stderr,
                            "libirixhostabi: guest trap a%u string @%08x: %.256s\n",
                            reg - 4u, candidate, message);
                }
            }
            for (offset = 0; offset < 0x180u; offset += 4u) {
                IrixGuestAddr at = stack_base - 0x100u + offset;
                uint32_t word = 0;
                if (irix_guest_ptr(&context->runtime, at, 4u))
                    word = irix_guest_read32be(&context->runtime, at);
                if (word >= 0x0b000000u && word < 0x10000000u)
                    fprintf(stderr,
                        "libirixhostabi: guest stack candidate +%03x: %08x\n",
                        (unsigned)(at - stack_base), word);
            }
        }
        status = -1;
    }
    if (status == 0 && result_bits) {
        switch (result_kind) {
        case IRIX_HOST_ABI_RESULT_VOID:
            *result_bits = 0;
            break;
        case IRIX_HOST_ABI_RESULT_I32:
            *result_bits = (uint64_t)(int64_t)(int32_t)cpu.gpr[2];
            break;
        case IRIX_HOST_ABI_RESULT_U32:
            *result_bits = (uint32_t)cpu.gpr[2];
            break;
        case IRIX_HOST_ABI_RESULT_PTR32:
            *result_bits = (uint32_t)cpu.gpr[2];
            break;
        case IRIX_HOST_ABI_RESULT_I64:
        case IRIX_HOST_ABI_RESULT_U64:
            *result_bits = cpu.gpr[2];
            break;
        case IRIX_HOST_ABI_RESULT_F32: {
            float value = irix_guest_fpr_get_s(&cpu, 0u);
            uint32_t bits;
            memcpy(&bits, &value, sizeof(bits));
            *result_bits = bits;
            break;
        }
        case IRIX_HOST_ABI_RESULT_F64: {
            double value = irix_guest_fpr_get_d(&cpu, 0u);
            memcpy(result_bits, &value, sizeof(value));
            break;
        }
        }
    }
    --context->call_depth;
    pthread_mutex_unlock(&context->mutex);
    return status;
}

int irix_host_abi_context_call(IrixHostAbiContext *context, const char *name,
                               const uint32_t *args, size_t arg_count,
                               uint32_t *result)
{
    IrixN32AbiArgument typed_args[IRIX_HOST_ABI_MAX_ARGS];
    uint64_t typed_result = 0;
    size_t i;

    if (arg_count > IRIX_HOST_ABI_MAX_ARGS || (arg_count && !args))
        return -1;
    for (i = 0; i < arg_count; ++i) {
        /* The legacy bridge API does not carry prototypes. Treat its words as
         * 32-bit guest addresses/opaque scalar tokens to preserve its prior
         * zero-extension behavior; new wrappers should use the typed entry. */
        typed_args[i].kind = IRIX_N32_ABI_PTR32;
        typed_args[i].flags = 0;
        typed_args[i].bits = args[i];
    }
    if (irix_host_abi_context_call_typed(context, name, typed_args, arg_count,
                                         result ? IRIX_HOST_ABI_RESULT_U32
                                                : IRIX_HOST_ABI_RESULT_VOID,
                                         &typed_result) != 0)
        return -1;
    if (result)
        *result = (uint32_t)typed_result;
    return 0;
}

static int host_call_guest_address(IrixHostAbiContext *context,
                                   IrixGuestAddr address,
                                   const uint32_t *args, size_t arg_count,
                                   uint32_t *result)
{
    IrixGuestFunctionEntry *entry;
    IrixGuestCpu cpu;
    size_t i;
    int status;
    if (!context || !context->ready || !address ||
        arg_count > IRIX_HOST_ABI_MAX_ARGS || (arg_count && !args))
        return -1;
    pthread_mutex_lock(&context->mutex);
    entry = irix_guest_find_function(&context->runtime, address);
    if (!entry) {
        pthread_mutex_unlock(&context->mutex);
        return -1;
    }
    if (context->call_depth >= 32u) {
        pthread_mutex_unlock(&context->mutex);
        return -1;
    }
    memset(&cpu, 0, sizeof(cpu));
    cpu.gpr[28] = 0x60008000u;
    cpu.gpr[29] = 0x7fdfff00u - context->call_depth++ * 0x20000u;
    cpu.gpr[25] = address;
    for (i = 0; i < arg_count && i < 8u; ++i)
        cpu.gpr[4u + i] = args[i];
    for (i = 8u; i < arg_count; ++i)
        irix_guest_write64be(&context->runtime,
            (IrixGuestAddr)(uint32_t)cpu.gpr[29] +
                (IrixGuestAddr)((i - 8u) * 8u),
            (uint64_t)args[i]);
    {
        int previous_jit_state = irix_mips_jit_set_thread_disabled(1);
        status = irix_guest_call_entry(&context->runtime, &cpu, address,
                                       entry);
        (void)irix_mips_jit_set_thread_disabled(previous_jit_state);
    }
    if (result)
        *result = (uint32_t)cpu.gpr[2];
    if (context->runtime.trapped)
        status = -1;
    --context->call_depth;
    pthread_mutex_unlock(&context->mutex);
    return status;
}

int irix_host_abi_context_copy_string(IrixHostAbiContext *context,
                                      const char *value,
                                      IrixGuestAddr *guest_address)
{
    if (!value)
        return -1;
    return irix_host_abi_context_copy_bytes(context, value, strlen(value) + 1u,
                                            guest_address);
}

int irix_host_abi_context_copy_bytes(IrixHostAbiContext *context,
                                     const void *bytes, size_t size,
                                     IrixGuestAddr *guest_address)
{
    IrixGuestAddr address;
    void *destination;
    if (!context || !context->ready || (!bytes && size) || !size ||
        !guest_address || size > UINT32_MAX)
        return -1;
    pthread_mutex_lock(&context->mutex);
    address = irix_guest_xt_compat_alloc(&context->runtime, (uint32_t)size);
    destination = address ? irix_guest_ptr(&context->runtime, address, size) : NULL;
    if (destination)
        memcpy(destination, bytes, size);
    pthread_mutex_unlock(&context->mutex);
    if (!destination)
        return -1;
    *guest_address = address;
    return 0;
}

int irix_host_abi_context_write32be(IrixHostAbiContext *context,
                                    IrixGuestAddr address, uint32_t value)
{
    int ok;
    if (!context || !context->ready || !address)
        return -1;
    pthread_mutex_lock(&context->mutex);
    ok = irix_guest_ptr(&context->runtime, address, 4u) != NULL;
    if (ok)
        irix_guest_write32be(&context->runtime, address, value);
    pthread_mutex_unlock(&context->mutex);
    return ok ? 0 : -1;
}

int irix_host_abi_context_read32be(IrixHostAbiContext *context,
                                   IrixGuestAddr address, uint32_t *value)
{
    int ok;
    if (!context || !context->ready || !address || !value)
        return -1;
    pthread_mutex_lock(&context->mutex);
    ok = irix_guest_ptr(&context->runtime, address, 4u) != NULL;
    if (ok)
        *value = irix_guest_read32be(&context->runtime, address);
    pthread_mutex_unlock(&context->mutex);
    return ok ? 0 : -1;
}

int irix_host_abi_context_has_function(IrixHostAbiContext *context,
                                       const char *name)
{
    size_t i;
    if (!context || !context->ready || !name)
        return 0;
    for (i = 0; i < context->runtime.function_count; ++i) {
        const IrixGuestFunctionEntry *entry = &context->runtime.functions[i];
        if (entry->name && strcmp(entry->name, name) == 0)
            return 1;
    }
    return 0;
}

static IrixGuestAddr context_arena_mark(IrixHostAbiContext *context)
{
    IrixGuestAddr mark;
    pthread_mutex_lock(&context->mutex);
    mark = context->runtime.compat_alloc_cur;
    pthread_mutex_unlock(&context->mutex);
    return mark;
}

static void context_arena_rewind(IrixHostAbiContext *context,
                                 IrixGuestAddr mark)
{
    pthread_mutex_lock(&context->mutex);
    context->runtime.compat_alloc_cur = mark;
    pthread_mutex_unlock(&context->mutex);
}

static IrixHostOpaqueHandle *host_handle_create(unsigned kind,
                                                 IrixGuestAddr guest_address,
                                                 IrixGuestAddr parent_address,
                                                 IrixGuestAddr app_address)
{
    IrixHostOpaqueHandle *handle;
    for (handle = g_host_opaque_handles; handle; handle = handle->next)
        if (handle->alive && handle->kind == kind &&
            handle->guest_address == guest_address)
            return handle;
    handle = (IrixHostOpaqueHandle *)calloc(1, sizeof(*handle));
    if (!handle)
        return NULL;
    handle->magic = IRIX_HOST_OPAQUE_MAGIC;
    handle->guest_address = guest_address;
    handle->parent_guest_address = parent_address;
    handle->app_guest_address = app_address;
    handle->kind = kind;
    handle->alive = 1;
    if (kind == IRIX_HOST_HANDLE_WIDGET) {
        IrixHostOpaqueHandle *parent;
        handle->widget_object = (Widget)&handle->widget_rec;
        handle->widget_rec.core.self = (Widget)&handle->widget_rec;
        handle->widget_rec.core.widget_class = (WidgetClass)&g_host_core_class;
        handle->widget_rec.core.sensitive = True;
        handle->widget_rec.core.ancestor_sensitive = True;
        handle->widget_rec.core.mapped_when_managed = True;
        for (parent = g_host_opaque_handles; parent; parent = parent->next)
            if (parent->alive && parent->kind == IRIX_HOST_HANDLE_WIDGET &&
                parent->guest_address == parent_address &&
                parent->app_guest_address == app_address) {
                handle->widget_rec.core.parent =
                    parent->widget_object;
                handle->host_display = parent->host_display;
                handle->host_screen = parent->host_screen;
                handle->guest_display_address = parent->guest_display_address;
                handle->guest_screen_address = parent->guest_screen_address;
                handle->widget_rec.core.screen = parent->host_screen;
                handle->widget_rec.core.depth = parent->widget_object->core.depth;
                handle->widget_rec.core.colormap = parent->widget_object->core.colormap;
                handle->widget_rec.core.background_pixel =
                    parent->widget_object->core.background_pixel;
                {
                    Widget *children = (Widget *)realloc(parent->host_children,
                        (parent->host_child_count + 1u) * sizeof(*children));
                    if (children) {
                        parent->host_children = children;
                        parent->host_children[parent->host_child_count++] =
                            handle->widget_object;
                    }
                }
                break;
            }
    }
    handle->next = g_host_opaque_handles;
    g_host_opaque_handles = handle;
    return handle;
}

static Widget host_widget_value(IrixHostOpaqueHandle *handle)
{
    return handle && handle->kind == IRIX_HOST_HANDLE_WIDGET
        ? handle->widget_object : NULL;
}

static WidgetRec *host_widget_rec(IrixHostOpaqueHandle *handle)
{
    return handle && handle->kind == IRIX_HOST_HANDLE_WIDGET
        ? (WidgetRec *)handle->widget_object : NULL;
}

static int host_widget_promote_class(IrixHostOpaqueHandle *handle,
                                     WidgetClass widget_class)
{
    WidgetRec *rec;
    size_t size;
    if (!handle || handle->kind != IRIX_HOST_HANDLE_WIDGET ||
        (!widget_class && !handle->is_shell))
        return -1;
    size = widget_class ? widget_class->core_class.widget_size : 0u;
    if (size < sizeof(WidgetRec))
        size = sizeof(WidgetRec);
    if (handle->is_shell && size < sizeof(ApplicationShellRec))
        size = sizeof(ApplicationShellRec);
    if (size > sizeof(WidgetRec) && size > handle->widget_storage_size) {
        Widget old_widget = handle->widget_object;
        void *storage = calloc(1, size);
        IrixHostOpaqueHandle *other;
        if (!storage)
            return -1;
        memcpy(storage, old_widget, handle->widget_storage
            ? handle->widget_storage_size : sizeof(WidgetRec));
        free(handle->widget_storage);
        handle->widget_storage = storage;
        handle->widget_storage_size = size;
        handle->widget_object = (Widget)storage;
        /* Constructors register the child before its concrete host record
         * size is known. Keep hierarchy references paired with that record. */
        for (other = g_host_opaque_handles; other; other = other->next) {
            Cardinal i;
            if (!other->alive || other->kind != IRIX_HOST_HANDLE_WIDGET)
                continue;
            if (other->widget_object->core.parent == old_widget)
                other->widget_object->core.parent = handle->widget_object;
            for (i = 0; i < other->host_child_count; ++i)
                if (other->host_children[i] == old_widget)
                    other->host_children[i] = handle->widget_object;
        }
    }
    rec = host_widget_rec(handle);
    rec->core.self = handle->widget_object;
    if (widget_class)
        rec->core.widget_class = widget_class;
    /* XfeFrameShell's native Realize method normally supplies these defaults. */
    if (handle->is_shell && rec->core.width <= 2)
        rec->core.width = 600;
    if (handle->is_shell && rec->core.height <= 2)
        rec->core.height = 480;
    return 0;
}

static void host_try_initialize_guest_perdisplay(
    IrixHostAbiContext *context, IrixHostOpaqueHandle *app_handle,
    uint32_t guest_display)
{
    IrixGuestAddr guest_name, guest_class, guest_argc;
    uint32_t args[8], per_display = 0;
    unsigned char zero[4] = { 0, 0, 0, 0 };
    if (!context || !app_handle || !getenv("IRIX_HOST_ABI_REPAIR_PER_DISPLAY") ||
        irix_host_abi_context_read32be(context, 0x50010fb0u,
                                       &per_display) != 0 || per_display)
        return;
    if (irix_host_abi_context_copy_string(context,
            app_handle->host_app_name ? app_handle->host_app_name : "netscape",
            &guest_name) != 0 ||
        irix_host_abi_context_copy_string(context,
            app_handle->host_app_class ? app_handle->host_app_class : "Netscape",
            &guest_class) != 0 ||
        irix_host_abi_context_copy_bytes(context, zero, sizeof(zero),
                                         &guest_argc) != 0)
        return;
    args[0] = app_handle->guest_address;
    args[1] = guest_display;
    args[2] = guest_name;
    args[3] = guest_class;
    args[4] = 0;
    args[5] = 0;
    args[6] = guest_argc;
    args[7] = 0;
    (void)host_call_guest_address(context, 0x5000fb6cu, args, 8u, NULL);
    (void)irix_host_abi_context_read32be(context, 0x50010fb0u, &per_display);
    fprintf(stderr,
        "libirixhostabi: explicit XtDisplayInitialize bootstrap app=%08x display=%08x per-display=%08x trapped=%d\n",
        app_handle->guest_address, guest_display, per_display,
        context->runtime.trapped);
}

static WidgetClass host_application_shell_class(void)
{
    if (!g_host_application_shell_class)
        host_prepare_xt_classes();
    return g_host_application_shell_class;
}

static void host_widget_set_display(IrixHostOpaqueHandle *handle,
                                    Display *display, Screen *screen)
{
    if (!handle || handle->kind != IRIX_HOST_HANDLE_WIDGET)
        return;
    handle->host_display = display;
    handle->host_screen = screen;
    host_widget_rec(handle)->core.screen = screen;
    if (screen) {
        host_widget_rec(handle)->core.depth = DefaultDepthOfScreen(screen);
        host_widget_rec(handle)->core.colormap = DefaultColormapOfScreen(screen);
        host_widget_rec(handle)->core.background_pixel = WhitePixelOfScreen(screen);
        if (handle->is_shell && handle->widget_storage_size >= sizeof(ShellRec))
            ((ShellWidget)handle->widget_object)->shell.visual =
                DefaultVisualOfScreen(screen);
    }
}

static IrixHostOpaqueHandle *host_handle_find(const void *opaque,
                                               unsigned kind)
{
    IrixHostOpaqueHandle *handle;
    for (handle = g_host_opaque_handles; handle; handle = handle->next)
        if ((handle->kind == IRIX_HOST_HANDLE_WIDGET
                 ? (const void *)handle->widget_object : (const void *)handle)
                == opaque && handle->alive &&
            handle->magic == IRIX_HOST_OPAQUE_MAGIC && handle->kind == kind)
            return handle;
    return NULL;
}

static IrixHostOpaqueHandle *host_handle_find_guest(IrixGuestAddr address,
                                                     unsigned kind)
{
    IrixHostOpaqueHandle *handle;
    for (handle = g_host_opaque_handles; handle; handle = handle->next)
        if (handle->alive && handle->magic == IRIX_HOST_OPAQUE_MAGIC &&
            handle->kind == kind && handle->guest_address == address)
            return handle;
    return NULL;
}

static IrixHostOpaqueHandle *host_app_find_by_display(Display *display)
{
    IrixHostOpaqueHandle *handle;
    for (handle = g_host_opaque_handles; handle; handle = handle->next)
        if (handle->alive && handle->kind == IRIX_HOST_HANDLE_APP_CONTEXT &&
            handle->host_display == display)
            return handle;
    return NULL;
}

static IrixHostOpaqueHandle *host_app_find_by_screen(Screen *screen)
{
    IrixHostOpaqueHandle *handle;
    for (handle = g_host_opaque_handles; handle; handle = handle->next)
        if (handle->alive && handle->kind == IRIX_HOST_HANDLE_APP_CONTEXT &&
            handle->host_screen == screen)
            return handle;
    return NULL;
}

static IrixHostOpaqueHandle *host_app_find_single(void)
{
    IrixHostOpaqueHandle *handle, *found = NULL;
    for (handle = g_host_opaque_handles; handle; handle = handle->next)
        if (handle->alive && handle->kind == IRIX_HOST_HANDLE_APP_CONTEXT) {
            if (found)
                return NULL;
            found = handle;
        }
    return found;
}

static char *context_guest_string_dup(IrixHostAbiContext *context,
                                      IrixGuestAddr address)
{
    char *copy;
    size_t length;
    if (!context || !address)
        return NULL;
    pthread_mutex_lock(&context->mutex);
    for (length = 0; length < 4096u; ++length) {
        if (!irix_guest_ptr(&context->runtime, address + (uint32_t)length, 1u) ||
            context->runtime.trapped)
            break;
        if (irix_guest_read8(&context->runtime,
                             address + (uint32_t)length) == 0)
            break;
    }
    if (length == 4096u || context->runtime.trapped) {
        pthread_mutex_unlock(&context->mutex);
        return NULL;
    }
    copy = (char *)malloc(length + 1u);
    if (copy) {
        size_t i;
        for (i = 0; i < length; ++i)
            copy[i] = (char)irix_guest_read8(&context->runtime,
                                             address + (uint32_t)i);
        copy[length] = '\0';
    }
    pthread_mutex_unlock(&context->mutex);
    return copy;
}

static int host_resource_matches(const char *name, const char *text)
{
    return name && text && strcmp(name, text) == 0;
}

static void host_handles_invalidate_widget_tree(IrixGuestAddr root,
                                                IrixGuestAddr app_address)
{
    IrixHostOpaqueHandle *handle;
    int changed;
    for (handle = g_host_opaque_handles; handle; handle = handle->next)
        if (handle->alive && handle->kind == IRIX_HOST_HANDLE_WIDGET &&
            handle->guest_address == root &&
            handle->app_guest_address == app_address)
            handle->alive = 0;
    do {
        changed = 0;
        for (handle = g_host_opaque_handles; handle; handle = handle->next) {
            IrixHostOpaqueHandle *parent;
            if (!handle->alive || handle->kind != IRIX_HOST_HANDLE_WIDGET ||
                handle->app_guest_address != app_address ||
                !handle->parent_guest_address)
                continue;
            for (parent = g_host_opaque_handles; parent; parent = parent->next)
                if (parent->kind == IRIX_HOST_HANDLE_WIDGET &&
                    parent->guest_address == handle->parent_guest_address &&
                    parent->app_guest_address == app_address && !parent->alive) {
                    handle->alive = 0;
                    changed = 1;
                    break;
                }
        }
    } while (changed);
}

static int context_default(IrixHostAbiContext **context)
{
    pthread_once(&g_default_context_once, create_default_context);
    *context = g_default_context;
    return *context && (*context)->ready;
}

/*
 * First host-ABI slice: XmStringCreateLtoR/XmStringFree. XmString is opaque
 * in Motif 1.2, so return a tracked host handle and keep the N32 value private.
 * The wrapper copies its C-string inputs to guest memory, calls the translated
 * implementation, then rewinds those short-lived input allocations.
 */
static XmString host_xm_string_create(const char *api, char *text,
                                      XmStringCharSet charset)
{
    IrixHostAbiContext *context;
    IrixHostXmStringHandle *handle;
    IrixGuestAddr guest_text, guest_charset;
    uint32_t args[2], guest_result = 0;
    IrixGuestAddr arena_mark;
    int status;
    pthread_once(&g_default_context_once, create_default_context);
    context = g_default_context;
    if (!context || !text || !charset)
        return NULL;
    handle = (IrixHostXmStringHandle *)calloc(1, sizeof(*handle));
    if (!handle)
        return NULL;
    pthread_mutex_lock(&g_host_handle_mutex);
    pthread_mutex_lock(&context->mutex);
    arena_mark = context->runtime.compat_alloc_cur;
    pthread_mutex_unlock(&context->mutex);
    status = irix_host_abi_context_copy_string(context, text, &guest_text);
    if (status == 0)
        status = irix_host_abi_context_copy_string(context, charset,
                                                  &guest_charset);
    if (status == 0) {
        args[0] = guest_text;
        args[1] = guest_charset;
        status = irix_host_abi_context_call(context, api,
                                            args, 2, &guest_result);
    }
    pthread_mutex_lock(&context->mutex);
    context->runtime.compat_alloc_cur = arena_mark;
    pthread_mutex_unlock(&context->mutex);
    if (status != 0 || !guest_result) {
        free(handle);
        pthread_mutex_unlock(&g_host_handle_mutex);
        return NULL;
    }
    handle->magic = IRIX_HOST_XMSTRING_MAGIC;
    handle->guest_address = guest_result;
    handle->next = g_host_xmstrings;
    g_host_xmstrings = handle;
    pthread_mutex_unlock(&g_host_handle_mutex);
    return (XmString)handle;
}

XmString XmStringCreateLtoR(char *text, XmStringCharSet charset)
{
    return host_xm_string_create("XmStringCreateLtoR", text, charset);
}

XmString XmStringCreate(char *text, XmStringCharSet charset)
{
    return host_xm_string_create("XmStringCreate", text, charset);
}

XmString XmStringSegmentCreate(char *text, XmStringCharSet charset,
                               XmStringDirection direction,
                               Boolean separator)
{
    IrixHostAbiContext *context;
    IrixHostXmStringHandle *handle;
    IrixGuestAddr mark, guest_text, guest_charset;
    uint32_t args[4], result = 0;
    int status;
    if (!text || !charset || !context_default(&context)) return NULL;
    handle = calloc(1, sizeof(*handle));
    if (!handle) return NULL;
    pthread_mutex_lock(&g_host_handle_mutex);
    mark = context_arena_mark(context);
    status = irix_host_abi_context_copy_string(context, text, &guest_text);
    if (!status)
        status = irix_host_abi_context_copy_string(context, charset,
                                                    &guest_charset);
    if (!status) {
        args[0] = guest_text; args[1] = guest_charset;
        args[2] = (uint32_t)direction; args[3] = separator ? 1u : 0u;
        status = irix_host_abi_context_call(context, "XmStringSegmentCreate",
                                             args, 4u, &result);
    }
    context_arena_rewind(context, mark);
    if (status || !result) {
        free(handle);
        handle = NULL;
    } else {
        handle->magic = IRIX_HOST_XMSTRING_MAGIC;
        handle->guest_address = result;
        handle->next = g_host_xmstrings;
        g_host_xmstrings = handle;
    }
    pthread_mutex_unlock(&g_host_handle_mutex);
    return (XmString)handle;
}

XmString XmStringConcat(XmString first, XmString second)
{
    IrixHostAbiContext *context;
    IrixHostXmStringHandle *item, *result_handle;
    uint32_t args[2] = {0, 0}, result = 0;
    if (!first || !second || !context_default(&context)) return NULL;
    result_handle = calloc(1, sizeof(*result_handle));
    if (!result_handle) return NULL;
    pthread_mutex_lock(&g_host_handle_mutex);
    for (item = g_host_xmstrings; item; item = item->next) {
        if ((XmString)item == first) args[0] = item->guest_address;
        if ((XmString)item == second) args[1] = item->guest_address;
    }
    if (!args[0] || !args[1] || irix_host_abi_context_call(context,
            "XmStringConcat", args, 2u, &result) != 0 || !result) {
        free(result_handle); result_handle = NULL;
    } else {
        result_handle->magic = IRIX_HOST_XMSTRING_MAGIC;
        result_handle->guest_address = result;
        result_handle->next = g_host_xmstrings;
        g_host_xmstrings = result_handle;
    }
    pthread_mutex_unlock(&g_host_handle_mutex);
    return (XmString)result_handle;
}

int XmStringLength(XmString string)
{
    IrixHostAbiContext *context;
    IrixHostXmStringHandle *item;
    uint32_t arg = 0, result = 0;
    if (!string || !context_default(&context)) return 0;
    pthread_mutex_lock(&g_host_handle_mutex);
    for (item = g_host_xmstrings; item; item = item->next)
        if ((XmString)item == string) arg = item->guest_address;
    if (!arg || irix_host_abi_context_call(context, "XmStringLength", &arg,
            1u, &result) != 0) result = 0;
    pthread_mutex_unlock(&g_host_handle_mutex);
    return (int)result;
}

Boolean XmStringGetLtoR(XmString string, XmStringCharSet charset, char **text)
{
    IrixHostAbiContext *context;
    IrixHostXmStringHandle *item;
    IrixGuestAddr mark, guest_string = 0, guest_charset, guest_out;
    uint32_t args[3], converted = 0, result = 0;
    const char *guest_text;
    char *host_text = NULL;
    if (text) *text = NULL;
    if (!string || !charset || !text || !context_default(&context))
        return False;
    pthread_mutex_lock(&g_host_handle_mutex);
    mark = context_arena_mark(context);
    for (item = g_host_xmstrings; item; item = item->next)
        if ((XmString)item == string) guest_string = item->guest_address;
    if (!guest_string || irix_host_abi_context_copy_string(context, charset,
            &guest_charset) != 0 || irix_host_abi_context_copy_bytes(context,
            &converted, sizeof(converted), &guest_out) != 0)
        goto done;
    args[0] = guest_string;
    args[1] = guest_charset;
    args[2] = guest_out;
    if (irix_host_abi_context_call(context, "XmStringGetLtoR", args, 3u,
            &result) != 0 || !result ||
        irix_host_abi_context_read32be(context, guest_out, &converted) != 0 ||
        !converted)
        goto done;
    guest_text = irix_guest_cstr(&context->runtime, converted, 8192u);
    if (guest_text) host_text = XtMalloc((Cardinal)strlen(guest_text) + 1u);
    if (host_text) strcpy(host_text, guest_text);
    (void)irix_host_abi_context_call(context, "XtFree", &converted, 1u, NULL);
done:
    context_arena_rewind(context, mark);
    pthread_mutex_unlock(&g_host_handle_mutex);
    if (!host_text) return False;
    *text = host_text;
    return True;
}

static Boolean host_xm_string_compare(const char *api, XmString a, XmString b)
{
    IrixHostAbiContext *context;
    IrixHostXmStringHandle *item;
    uint32_t args[2] = { 0, 0 }, result = 0;
    if (!a || !b) return a == b;
    if (!context_default(&context)) return False;
    pthread_mutex_lock(&g_host_handle_mutex);
    for (item = g_host_xmstrings; item; item = item->next) {
        if ((XmString)item == a) args[0] = item->guest_address;
        if ((XmString)item == b) args[1] = item->guest_address;
    }
    if (args[0] && args[1] &&
        irix_host_abi_context_call(context, api, args, 2u, &result) != 0)
        result = 0;
    pthread_mutex_unlock(&g_host_handle_mutex);
    return result ? True : False;
}

Boolean XmStringByteCompare(XmString a, XmString b)
{
    return host_xm_string_compare("XmStringByteCompare", a, b);
}

Boolean XmStringCompare(XmString a, XmString b)
{
    return host_xm_string_compare("XmStringCompare", a, b);
}

XmString XmStringCopy(XmString string)
{
    IrixHostAbiContext *context;
    IrixHostXmStringHandle *item, *copy;
    uint32_t address = 0, result = 0;
    if (!string || !context_default(&context)) return NULL;
    copy = calloc(1, sizeof(*copy));
    if (!copy) return NULL;
    pthread_mutex_lock(&g_host_handle_mutex);
    for (item = g_host_xmstrings; item; item = item->next)
        if ((XmString)item == string) address = item->guest_address;
    if (!address || irix_host_abi_context_call(context, "XmStringCopy",
            &address, 1u, &result) != 0 || !result) {
        free(copy); copy = NULL;
    } else {
        copy->magic = IRIX_HOST_XMSTRING_MAGIC;
        copy->guest_address = result;
        copy->next = g_host_xmstrings;
        g_host_xmstrings = copy;
    }
    pthread_mutex_unlock(&g_host_handle_mutex);
    return (XmString)copy;
}

void XmStringFree(XmString string)
{
    IrixHostAbiContext *context;
    IrixHostXmStringHandle **link, *handle;
    uint32_t arg;
    pthread_once(&g_default_context_once, create_default_context);
    context = g_default_context;
    if (!context || !string)
        return;
    pthread_mutex_lock(&g_host_handle_mutex);
    for (link = &g_host_xmstrings; *link; link = &(*link)->next)
        if ((XmString)*link == string)
            break;
    handle = *link;
    if (!handle || handle->magic != IRIX_HOST_XMSTRING_MAGIC) {
        pthread_mutex_unlock(&g_host_handle_mutex);
        return;
    }
    *link = handle->next;
    arg = handle->guest_address;
    handle->magic = 0;
    (void)irix_host_abi_context_call(context, "XmStringFree", &arg, 1, NULL);
    free(handle);
    pthread_mutex_unlock(&g_host_handle_mutex);
}

#include "IrixHostAbiFonts.inc"


/*
 * First native Widget slice. It intentionally accepts no resource arguments
 * yet: Arg values need resource-type-aware conversion, and must never be
 * copied from a host Arg array into N32 storage.
 */
Widget XtAppInitialize(XtAppContext *app_context_return,
                       _Xconst _XtString application_class,
                       XrmOptionDescList options, Cardinal num_options,
                       int *argc_in_out, _XtString *argv_in_out,
                       String *fallback_resources, ArgList args,
                       Cardinal num_args)
{
    IrixHostAbiContext *context;
    IrixGuestAddr arena_mark, guest_class, guest_argc, guest_argv;
    IrixGuestAddr guest_app_slot, guest_app = 0;
    IrixGuestAddr guest_options = 0, guest_fallbacks = 0;
    IrixGuestAddr guest_display = 0, guest_screen = 0;
    uint32_t guest_shell = 0, guest_argc_value;
    uint32_t call_args[9];
    IrixN32XrmOptionDescRec *option_records = NULL;
    uint32_t *fallback_records = NULL;
    IrixGuestAddr *guest_arg_addresses = NULL;
    size_t i, argc, fallback_count = 0, fallback_text_size = 0;
    int status = -1;
    char zero[4] = { 0, 0, 0, 0 };
    unsigned char *argv_bytes;
    IrixHostOpaqueHandle *app_handle, *shell_handle;
    Screen *host_screen;
    int guest_started = 0;

    if (!app_context_return || !application_class || !argc_in_out ||
        !argv_in_out || *argc_in_out < 0 || *argc_in_out > 256 ||
        num_options > 512u || (num_options && !options) || num_args != 0 ||
        (args && num_args != 0) ||
        !context_default(&context))
        return NULL;
    host_prepare_xt_classes();
    argc = (size_t)*argc_in_out;
    if (argc && !argv_in_out[0])
        return NULL;
    if (fallback_resources) {
        while (fallback_resources[fallback_count]) {
            size_t line_size = strlen(fallback_resources[fallback_count]);
            if (++fallback_count > 8192u ||
                line_size > SIZE_MAX - fallback_text_size - 1u)
                return NULL;
            fallback_text_size += line_size + 1u;
        }
    }

    pthread_mutex_lock(&g_host_handle_mutex);
    arena_mark = context_arena_mark(context);
    argv_bytes = (unsigned char *)calloc(argc + 1u, 4u);
    guest_arg_addresses = (IrixGuestAddr *)calloc(argc ? argc : 1u,
                                                   sizeof(*guest_arg_addresses));
    option_records = num_options
        ? (IrixN32XrmOptionDescRec *)calloc(num_options,
                                             sizeof(*option_records)) : NULL;
    fallback_records = fallback_count
        ? (uint32_t *)calloc(fallback_count + 1u, sizeof(*fallback_records))
        : NULL;
    if (!argv_bytes || !guest_arg_addresses ||
        (num_options && !option_records) ||
        (fallback_count && !fallback_records))
        goto done;
    if (irix_host_abi_context_copy_string(context, application_class,
                                          &guest_class) != 0 ||
        irix_host_abi_context_copy_bytes(context, zero, sizeof(zero),
                                         &guest_app_slot) != 0 ||
        irix_host_abi_context_copy_bytes(context, zero, sizeof(zero),
                                         &guest_argc) != 0 ||
        irix_host_abi_context_copy_bytes(context, argv_bytes,
                                         (argc + 1u) * 4u, &guest_argv) != 0)
        goto rewind;
    guest_argc_value = (uint32_t)argc;
    if (irix_host_abi_context_write32be(context, guest_argc,
                                        guest_argc_value) != 0)
        goto rewind;
    for (i = 0; i < argc; ++i) {
        if (!argv_in_out[i] ||
            irix_host_abi_context_copy_string(context, argv_in_out[i],
                                              &guest_arg_addresses[i]) != 0 ||
            irix_host_abi_context_write32be(context,
                                            guest_argv + (uint32_t)(i * 4u),
                                            guest_arg_addresses[i]) != 0)
            goto rewind;
    }
    if (num_options) {
        for (i = 0; i < num_options; ++i) {
            if (!options[i].option ||
                irix_host_abi_context_copy_string(context, options[i].option,
                                                  &option_records[i].option) != 0)
                goto rewind;
            if (options[i].specifier &&
                irix_host_abi_context_copy_string(context, options[i].specifier,
                                                  &option_records[i].specifier) != 0)
                goto rewind;
            option_records[i].arg_kind = (uint32_t)options[i].argKind;
            if (options[i].value &&
                irix_host_abi_context_copy_string(context,
                                                  (const char *)options[i].value,
                                                  &option_records[i].value) != 0)
                goto rewind;
        }
        if (irix_host_abi_context_copy_bytes(context, option_records,
                    num_options * sizeof(*option_records), &guest_options) != 0)
            goto rewind;
        for (i = 0; i < num_options; ++i) {
            IrixGuestAddr record = guest_options + (uint32_t)(i * 16u);
            if (irix_host_abi_context_write32be(context, record,
                    option_records[i].option) != 0 ||
                irix_host_abi_context_write32be(context, record + 4u,
                    option_records[i].specifier) != 0 ||
                irix_host_abi_context_write32be(context, record + 8u,
                    option_records[i].arg_kind) != 0 ||
                irix_host_abi_context_write32be(context, record + 12u,
                    option_records[i].value) != 0)
                goto rewind;
        }
    }
    if (fallback_count) {
        for (i = 0; i < fallback_count; ++i) {
            IrixGuestAddr guest_value;
            if (!fallback_resources[i] ||
                irix_host_abi_context_copy_string(context, fallback_resources[i],
                                                  &guest_value) != 0)
                goto rewind;
            fallback_records[i] = guest_value;
        }
        if (irix_host_abi_context_copy_bytes(context, fallback_records,
                    (fallback_count + 1u) * sizeof(*fallback_records),
                    &guest_fallbacks) != 0)
            goto rewind;
        for (i = 0; i <= fallback_count; ++i)
            if (irix_host_abi_context_write32be(context,
                    guest_fallbacks + (uint32_t)(i * 4u),
                    fallback_records[i]) != 0)
                goto rewind;
    }
    call_args[0] = guest_app_slot;
    call_args[1] = guest_class;
    call_args[2] = guest_options;
    call_args[3] = num_options;
    call_args[4] = guest_argc;
    call_args[5] = guest_argv;
    call_args[6] = guest_fallbacks;
    call_args[7] = 0;
    call_args[8] = 0;
    guest_started = 1;
    status = irix_host_abi_context_call(context, "XtAppInitialize", call_args,
                                        9u, &guest_shell);
    if (status != 0 || !guest_shell ||
        irix_host_abi_context_read32be(context, guest_app_slot,
                                       &guest_app) != 0 || !guest_app) {
        status = -1;
        goto rewind;
    }
    status = -1;
    if (irix_host_abi_context_read32be(context, guest_argc, &guest_argc_value) != 0 ||
        guest_argc_value > argc) {
        status = -1;
        goto rewind;
    }
    *argc_in_out = (int)guest_argc_value;
    for (i = 0; i < guest_argc_value; ++i) {
        uint32_t address = 0;
        size_t j;
        if (irix_host_abi_context_read32be(context,
                guest_argv + (uint32_t)(i * 4u), &address) != 0) {
            status = -1;
            goto rewind;
        }
        for (j = 0; j < argc; ++j)
            if (guest_arg_addresses[j] == address)
                break;
        if (j == argc) {
            status = -1;
            goto rewind;
        }
        argv_in_out[i] = argv_in_out[j];
    }
    argv_in_out[guest_argc_value] = NULL;
    app_handle = host_handle_create(IRIX_HOST_HANDLE_APP_CONTEXT,
                                    guest_app, 0, guest_app);
    shell_handle = host_handle_create(IRIX_HOST_HANDLE_WIDGET,
                                      guest_shell, 0, guest_app);
    if (!app_handle || !shell_handle) {
        status = -1;
        goto rewind;
    }
    shell_handle->is_shell = 1u;
    if (!app_handle->host_app_name) {
        const char *program = argc ? argv_in_out[0] : application_class;
        const char *base = strrchr(program, '/');
        app_handle->host_app_name = strdup(base ? base + 1 : program);
        app_handle->host_app_class = strdup(application_class);
    }
    (void)host_widget_promote_class(shell_handle,
        host_application_shell_class());
    host_widget_rec(shell_handle)->core.name = strdup(
        app_handle->host_app_name ? app_handle->host_app_name : application_class);
    if (irix_host_abi_context_call(context, "XtDisplayOfObject", &guest_shell,
                                   1u, &guest_display) != 0 || !guest_display) {
        fprintf(stderr, "libirixhostabi: XtAppInitialize could not get guest Display (shell=%08x, display=%08x)\n",
                guest_shell, guest_display);
        status = -1;
        goto rewind;
    }
    if (irix_host_abi_context_call(context, "XtScreenOfObject", &guest_shell,
                                   1u, &guest_screen) != 0 || !guest_screen) {
        fprintf(stderr, "libirixhostabi: XtAppInitialize could not get guest Screen (shell=%08x, screen=%08x)\n",
                guest_shell, guest_screen);
        status = -1;
        goto rewind;
    }
    app_handle->guest_display_address = guest_display;
    app_handle->guest_screen_address = guest_screen;
    app_handle->host_display = irix_x11_primary_display_from_guest(guest_display);
    host_screen = irix_x11_primary_screen_from_guest(guest_display,
                                                      guest_screen);
    if (!app_handle->host_display || !host_screen)
        fprintf(stderr, "libirixhostabi: screen mapping failed dpy=%08x screen=%08x host=%p screen-host=%p\n",
                guest_display, guest_screen, (void *)app_handle->host_display,
                (void *)host_screen);
    shell_handle->guest_display_address = guest_display;
    shell_handle->guest_screen_address = guest_screen;
    fprintf(stderr,
        "libirixhostabi: XtAppInitialize guest app=%08x shell=%08x display=%08x screen=%08x\n",
        guest_app, guest_shell, guest_display, guest_screen);
    app_handle->host_screen = host_screen;
    host_xt_adopt_display(app_handle);
    host_widget_set_display(shell_handle, app_handle->host_display, host_screen);
    {
        uint32_t per_display = 0;
        (void)irix_host_abi_context_read32be(context, 0x50010fb0u,
                                             &per_display);
        fprintf(stderr,
            "libirixhostabi: XtAppInitialize per-display head=%08x\n",
            per_display);
    }
    if (!app_handle->host_display || !host_screen) {
        status = -1;
        goto rewind;
    }
    if (fallback_resources) {
        char *resource_text = (char *)malloc(fallback_text_size + 1u);
        char *cursor = resource_text;
        if (!resource_text) {
            status = -1;
            goto rewind;
        }
        for (i = 0; i < fallback_count; ++i) {
            size_t line_size = strlen(fallback_resources[i]);
            memcpy(cursor, fallback_resources[i], line_size);
            cursor += line_size;
            *cursor++ = '\n';
        }
        *cursor = '\0';
        app_handle->host_resource_database = XrmGetStringDatabase(resource_text);
        free(resource_text);
        if (!app_handle->host_resource_database) {
            status = -1;
            goto rewind;
        }
    }
    *app_context_return = (XtAppContext)app_handle;
    status = 0;

rewind:
    /* Xt/Xlib retain guest allocations made while opening the Display. */
    if (!guest_started)
        context_arena_rewind(context, arena_mark);
done:
    free(argv_bytes);
    free(guest_arg_addresses);
    free(option_records);
    free(fallback_records);
    pthread_mutex_unlock(&g_host_handle_mutex);
    return status == 0 ? host_widget_value(shell_handle) : NULL;
}

XrmDatabase XtDatabase(Display *display)
{
    IrixHostOpaqueHandle *app_handle;
    XrmDatabase database = NULL;
    if (!display)
        return NULL;
    pthread_mutex_lock(&g_host_handle_mutex);
    app_handle = host_app_find_by_display(display);
    if (app_handle)
        database = app_handle->host_resource_database;
    pthread_mutex_unlock(&g_host_handle_mutex);
    return database ? database : XrmGetDatabase(display);
}

Window XtWindowOfObject(Widget widget)
{
    IrixHostAbiContext *context;
    IrixHostOpaqueHandle *handle;
    uint32_t window = 0;
    if (!context_default(&context)) return None;
    pthread_mutex_lock(&g_host_handle_mutex);
    handle = host_handle_find(widget, IRIX_HOST_HANDLE_WIDGET);
    if (handle && !irix_host_abi_context_call(context, "XtWindowOfObject",
            &handle->guest_address, 1, &window))
        handle->widget_object->core.window = window;
    pthread_mutex_unlock(&g_host_handle_mutex);
    return window;
}

#undef XtWindow
Window XtWindow(Widget widget)
{
    return XtWindowOfObject(widget);
}

#undef XtParent
Widget XtParent(Widget widget)
{
    IrixHostOpaqueHandle *handle, *parent;
    Widget result = NULL;
    pthread_mutex_lock(&g_host_handle_mutex);
    handle = host_handle_find(widget, IRIX_HOST_HANDLE_WIDGET);
    if (handle && handle->parent_guest_address) {
        for (parent = g_host_opaque_handles; parent; parent = parent->next)
            if (parent->alive && parent->kind == IRIX_HOST_HANDLE_WIDGET &&
                parent->guest_address == handle->parent_guest_address) {
                result = parent->widget_object;
                break;
            }
    }
    pthread_mutex_unlock(&g_host_handle_mutex);
    return result;
}

Widget XtWindowToWidget(Display *display, Window window)
{
    IrixHostAbiContext *context;
    IrixHostOpaqueHandle *app_handle, *item;
    uint32_t args[2], guest_widget = 0;
    Widget result = NULL;
    if (!context_default(&context)) return NULL;
    pthread_mutex_lock(&g_host_handle_mutex);
    app_handle = host_app_find_by_display(display);
    if (app_handle) {
        args[0] = app_handle->guest_display_address;
        args[1] = (uint32_t)window;
        if (!irix_host_abi_context_call(context, "XtWindowToWidget", args, 2u,
                                        &guest_widget) && guest_widget)
            for (item = g_host_opaque_handles; item; item = item->next)
                if (item->alive && item->kind == IRIX_HOST_HANDLE_WIDGET &&
                    item->guest_address == guest_widget) {
                    result = item->widget_object;
                    break;
                }
    }
    pthread_mutex_unlock(&g_host_handle_mutex);
    return result;
}

Widget XtNameToWidget(Widget root, _Xconst char *name)
{
    IrixHostAbiContext *context;
    IrixHostOpaqueHandle *handle, *item;
    IrixGuestAddr mark, guest_name;
    uint32_t args[2], guest_widget = 0;
    Widget result = NULL;
    if (!name || !context_default(&context)) return NULL;
    pthread_mutex_lock(&g_host_handle_mutex);
    handle = host_handle_find(root, IRIX_HOST_HANDLE_WIDGET);
    if (handle) {
        mark = context_arena_mark(context);
        if (!irix_host_abi_context_copy_string(context, name, &guest_name)) {
            args[0] = handle->guest_address;
            args[1] = guest_name;
            if (!irix_host_abi_context_call(context, "XtNameToWidget", args,
                    2u, &guest_widget) && guest_widget)
                for (item = g_host_opaque_handles; item; item = item->next)
                    if (item->alive && item->kind == IRIX_HOST_HANDLE_WIDGET &&
                        item->guest_address == guest_widget) {
                        result = item->widget_object;
                        break;
                    }
        }
        context_arena_rewind(context, mark);
    }
    pthread_mutex_unlock(&g_host_handle_mutex);
    return result;
}

GC XtGetGC(Widget widget, XtGCMask mask, XGCValues *values)
{
    Display *display = XtDisplayOfObject(widget);
    Window drawable = XtWindowOfObject(widget);
    if (!display || !values) return NULL;
    if (!drawable) {
        Screen *screen = XtScreenOfObject(widget);
        if (!screen) return NULL;
        drawable = RootWindowOfScreen(screen);
    }
    return XCreateGC(display, drawable, mask, values);
}

GC XtAllocateGC(Widget widget, Cardinal depth, XtGCMask mask,
                XGCValues *values, XtGCMask dynamic_mask,
                XtGCMask dont_care)
{
    Display *display = XtDisplayOfObject(widget);
    Screen *screen = XtScreenOfObject(widget);
    Window drawable;
    Pixmap temporary = None;
    GC gc;
    (void)dynamic_mask;
    (void)dont_care;
    if (!display || !screen || !values) return NULL;
    drawable = RootWindowOfScreen(screen);
    if ((unsigned)DefaultDepthOfScreen(screen) != depth) {
        temporary = XCreatePixmap(display, drawable, 1, 1, depth);
        if (!temporary) return NULL;
        drawable = temporary;
    }
    gc = XCreateGC(display, drawable, mask, values);
    if (temporary) XFreePixmap(display, temporary);
    return gc;
}

void XtReleaseGC(Widget widget, GC gc)
{
    Display *display = XtDisplayOfObject(widget);
    if (display && gc) XFreeGC(display, gc);
}

void XtConfigureWidget(Widget widget, Position x, Position y,
                       Dimension width, Dimension height,
                       Dimension border_width)
{
    IrixHostAbiContext *context;
    IrixHostOpaqueHandle *handle;
    uint32_t args[6];
    if (!context_default(&context)) return;
    pthread_mutex_lock(&g_host_handle_mutex);
    handle = host_handle_find(widget, IRIX_HOST_HANDLE_WIDGET);
    if (handle) {
        if (getenv("IRIX_HOST_ABI_TRACE_WIDGETS") &&
            host_widget_rec(handle)->core.name &&
            !strcmp(host_widget_rec(handle)->core.name, "vsb"))
            fprintf(stderr, "host ABI configure vsb request x=%d y=%d w=%u h=%u bw=%u parent=%08x\n",
                x, y, width, height, border_width, handle->parent_guest_address);
        args[0] = handle->guest_address; args[1] = (uint32_t)(int32_t)x;
        args[2] = (uint32_t)(int32_t)y; args[3] = width;
        args[4] = height; args[5] = border_width;
        (void)irix_host_abi_context_call(context, "XtConfigureWidget", args, 6, NULL);
        widget->core.x = x; widget->core.y = y;
        widget->core.width = width; widget->core.height = height;
        widget->core.border_width = border_width;
    }
    pthread_mutex_unlock(&g_host_handle_mutex);
}

Display *XtDisplayOfObject(Widget widget)
{
    IrixHostAbiContext *context;
    IrixHostOpaqueHandle *handle;
    uint32_t guest_display = 0;
    Display *display = NULL;
    if (!context_default(&context))
        return NULL;
    pthread_mutex_lock(&g_host_handle_mutex);
    handle = host_handle_find(widget, IRIX_HOST_HANDLE_WIDGET);
    if (handle) {
        if (handle->host_display)
            display = handle->host_display;
        else if (irix_host_abi_context_call(context, "XtDisplayOfObject",
                &handle->guest_address, 1u, &guest_display) == 0 &&
                guest_display) {
            display = irix_x11_primary_display_from_guest(guest_display);
            handle->guest_display_address = guest_display;
            handle->host_display = display;
        }
    }
    if (!display)
        fprintf(stderr, "libirixhostabi: XtDisplayOfObject widget=%p handle=%p stored-display=%p guest-display=%08x\n",
                (void *)widget, (void *)handle,
                handle ? (void *)handle->host_display : NULL,
                handle ? handle->guest_display_address : 0u);
    pthread_mutex_unlock(&g_host_handle_mutex);
    return display;
}

XtAppContext XtWidgetToApplicationContext(Widget widget)
{
    IrixHostOpaqueHandle *widget_handle, *app_handle;
    XtAppContext result = NULL;
    pthread_mutex_lock(&g_host_handle_mutex);
    widget_handle = host_handle_find(widget, IRIX_HOST_HANDLE_WIDGET);
    if (widget_handle) {
        for (app_handle = g_host_opaque_handles; app_handle;
             app_handle = app_handle->next) {
            if (app_handle->alive &&
                app_handle->kind == IRIX_HOST_HANDLE_APP_CONTEXT &&
                app_handle->guest_address == widget_handle->app_guest_address) {
                result = (XtAppContext)app_handle;
                break;
            }
        }
    }
    pthread_mutex_unlock(&g_host_handle_mutex);
    return result;
}

String XtName(Widget widget)
{
    /* C++ Netscape calls XtName as a function; translated libXt expects a
     * guest Widget address. The host widget record is authoritative here. */
    return widget ? widget->core.name : NULL;
}

Boolean _XtCheckSubclassFlag(Widget object, _XtXtEnum type_flag)
{
    WidgetClass widget_class = object ? object->core.widget_class : NULL;
    return widget_class &&
        (widget_class->core_class.class_inited & (Cardinal)type_flag) != 0;
}

void XtAppWarning(XtAppContext app_context, const char *message)
{
    IrixHostOpaqueHandle *handle;
    pthread_mutex_lock(&g_host_handle_mutex);
    handle = host_handle_find(app_context, IRIX_HOST_HANDLE_APP_CONTEXT);
    pthread_mutex_unlock(&g_host_handle_mutex);
    if (handle && message)
        fprintf(stderr, "libirixhostabi: Xt warning: %s", message);
}

#undef XtDisplay
Display *XtDisplay(Widget widget)
{
    return XtDisplayOfObject(widget);
}

Boolean XtIsSubclass(Widget widget, WidgetClass widget_class)
{
    IrixHostOpaqueHandle *handle;
    WidgetClass actual_class;
    Boolean result = False;
    static unsigned trace_count;

    if (!widget || !widget_class)
        return False;
    pthread_mutex_lock(&g_host_handle_mutex);
    handle = host_handle_find(widget, IRIX_HOST_HANDLE_WIDGET);
    if (handle) {
        if (handle->is_shell &&
            (widget_class == g_host_shell_widget_class ||
             widget_class == g_host_top_level_shell_class ||
             widget_class == g_host_application_shell_class ||
             widget_class == vendorShellWidgetClass)) {
            result = True;
        } else {
            for (actual_class = host_widget_rec(handle)->core.widget_class;
                 actual_class; actual_class = actual_class->core_class.superclass) {
                if (actual_class == widget_class) {
                    result = True;
                    break;
                }
            }
        }
    }
    if (getenv("IRIX_HOST_ABI_TRACE_WIDGETS") &&
        (host_native_insert_depth || trace_count++ < 96u ||
         (handle && handle->widget_object->core.name &&
          !strcmp(handle->widget_object->core.name, "dashBoard")))) {
        Dl_info actual_info, target_info;
        const char *actual_name = "?", *target_name = "?";
        if (handle && dladdr((void *)host_widget_rec(handle)->core.widget_class,
                             &actual_info) != 0)
            actual_name = host_widget_rec(handle)->core.widget_class->core_class.class_name;
        if (widget_class && dladdr((void *)widget_class, &target_info) != 0)
            target_name = widget_class->core_class.class_name;
        fprintf(stderr,
            "libirixhostabi: XtIsSubclass widget=%p handle=%p class=%p(%s) target=%p(%s) result=%d\n",
            (void *)widget, (void *)handle,
            handle ? (void *)host_widget_rec(handle)->core.widget_class : NULL,
            actual_name ? actual_name : "(unnamed)", (void *)widget_class,
            target_name ? target_name : "(unnamed)", result);
        if (handle && handle->widget_object->core.name &&
            !strcmp(handle->widget_object->core.name, "dashBoard")) {
            WidgetClass current;
            for (current = host_widget_rec(handle)->core.widget_class;
                 current; current = current->core_class.superclass)
                fprintf(stderr, "host ABI dashBoard class chain %p %s\n",
                    (void *)current, current->core_class.class_name);
        }
    }
    pthread_mutex_unlock(&g_host_handle_mutex);
    return result;
}

_XtString XtResolvePathname(Display *display, _Xconst _XtString type,
                            _Xconst _XtString filename,
                            _Xconst _XtString suffix,
                            _Xconst _XtString path,
                            Substitution substitutions,
                            Cardinal num_substitutions,
                            XtFilePredicate predicate)
{
    static int reported_unavailable;
    (void)display;
    (void)type;
    (void)filename;
    (void)suffix;
    (void)path;
    (void)substitutions;
    (void)num_substitutions;
    (void)predicate;
    if (!reported_unavailable) {
        fprintf(stderr, "libirixhostabi: XtResolvePathname resource-file lookup is not bridged yet\n");
        reported_unavailable = 1;
    }
    return NULL;
}

Screen *XtScreenOfObject(Widget widget)
{
    IrixHostAbiContext *context;
    IrixHostOpaqueHandle *handle;
    uint32_t guest_screen = 0;
    Screen *screen = NULL;
    if (!context_default(&context))
        return NULL;
    pthread_mutex_lock(&g_host_handle_mutex);
    handle = host_handle_find(widget, IRIX_HOST_HANDLE_WIDGET);
    /* Native callers need the host Screen paired with the facade Widget.
     * Child handles inherit that mapping from their parent; a translated
     * XtScreenOfObject call is only a fallback for handles without it. */
    if (handle)
        screen = handle->host_screen;
    if (!screen && handle && handle->guest_display_address &&
        irix_host_abi_context_call(context, "XtScreenOfObject",
            &handle->guest_address, 1u, &guest_screen) == 0 && guest_screen)
        screen = irix_x11_primary_screen_from_guest(handle->guest_display_address,
                                                    guest_screen);
    if (handle && screen) {
        handle->host_screen = screen;
        host_widget_rec(handle)->core.screen = screen;
    }
    pthread_mutex_unlock(&g_host_handle_mutex);
    return screen;
}

XtGeometryResult XtMakeGeometryRequest(Widget widget,
    XtWidgetGeometry *request, XtWidgetGeometry *reply)
{
    IrixHostAbiContext *context;
    IrixHostOpaqueHandle *handle, *sibling;
    IrixGuestAddr mark, guest_request, guest_reply = 0;
    uint32_t args[3], result = XtGeometryNo;
    unsigned char req[24] = {0}, rep[24] = {0};
    if (!request || !context_default(&context)) return XtGeometryNo;
    pthread_mutex_lock(&g_host_handle_mutex);
    handle = host_handle_find(widget, IRIX_HOST_HANDLE_WIDGET);
    if (!handle) goto done;
    sibling = request->sibling ? host_handle_find(request->sibling,
        IRIX_HOST_HANDLE_WIDGET) : NULL;
    if (request->sibling && !sibling) goto done;
    req[0]=(request->request_mode>>24)&255; req[1]=(request->request_mode>>16)&255;
    req[2]=(request->request_mode>>8)&255; req[3]=request->request_mode&255;
    req[4]=(uint16_t)request->x>>8; req[5]=(uint16_t)request->x;
    req[6]=(uint16_t)request->y>>8; req[7]=(uint16_t)request->y;
    req[8]=request->width>>8; req[9]=request->width;
    req[10]=request->height>>8; req[11]=request->height;
    req[12]=request->border_width>>8; req[13]=request->border_width;
    req[16]=(sibling?sibling->guest_address:0)>>24;
    req[17]=(sibling?sibling->guest_address:0)>>16;
    req[18]=(sibling?sibling->guest_address:0)>>8;
    req[19]=(sibling?sibling->guest_address:0);
    req[20]=(request->stack_mode>>24)&255; req[21]=(request->stack_mode>>16)&255;
    req[22]=(request->stack_mode>>8)&255; req[23]=request->stack_mode&255;
    mark=context_arena_mark(context);
    if (irix_host_abi_context_copy_bytes(context,req,sizeof(req),&guest_request)) goto rewind;
    if (reply && irix_host_abi_context_copy_bytes(context,rep,sizeof(rep),&guest_reply)) goto rewind;
    args[0]=handle->guest_address; args[1]=guest_request; args[2]=guest_reply;
    if (!irix_host_abi_context_call(context,"XtMakeGeometryRequest",args,3,&result)) {
        const unsigned char *out=reply?irix_guest_ptr(&context->runtime,guest_reply,sizeof(rep)):NULL;
        if (out) {
            reply->request_mode=(out[0]<<24)|(out[1]<<16)|(out[2]<<8)|out[3];
            reply->x=(int16_t)((out[4]<<8)|out[5]); reply->y=(int16_t)((out[6]<<8)|out[7]);
            reply->width=(out[8]<<8)|out[9]; reply->height=(out[10]<<8)|out[11];
            reply->border_width=(out[12]<<8)|out[13];
            reply->sibling=NULL;
            reply->stack_mode=(out[20]<<24)|(out[21]<<16)|(out[22]<<8)|out[23];
        }
        if (result==XtGeometryYes || result==XtGeometryDone) {
            widget->core.x=request->x; widget->core.y=request->y;
            widget->core.width=request->width; widget->core.height=request->height;
            widget->core.border_width=request->border_width;
        }
    }
rewind:
    context_arena_rewind(context,mark);
done:
    pthread_mutex_unlock(&g_host_handle_mutex);
    return (XtGeometryResult)result;
}

XtGeometryResult XtMakeResizeRequest(Widget widget, Dimension width,
    Dimension height, Dimension *reply_width, Dimension *reply_height)
{
    XtWidgetGeometry request, reply;
    XtGeometryResult result;
    memset(&request,0,sizeof(request)); memset(&reply,0,sizeof(reply));
    request.request_mode=CWWidth|CWHeight; request.width=width; request.height=height;
    result=XtMakeGeometryRequest(widget,&request,&reply);
    if (reply_width) *reply_width=reply.width;
    if (reply_height) *reply_height=reply.height;
    return result;
}

#undef XtScreen
Screen *XtScreen(Widget widget)
{
    return XtScreenOfObject(widget);
}

/* XFE is compiled against native Motif, so its Screen pointer is a host
 * Xlib object. The translated Motif cache expects the guest Screen address.
 * Keep the cache operation in translated Motif, translating at the ABI edge. */
static Pixmap host_motif_get_pixmap(Screen *screen, char *image_name,
                                    Pixel foreground, Pixel background,
                                    int depth, int by_depth)
{
    IrixHostAbiContext *context;
    IrixHostOpaqueHandle *app_handle;
    IrixGuestAddr mark, guest_name;
    uint32_t args[5], result = None;
    if (!screen || !image_name || !context_default(&context))
        return XmUNSPECIFIED_PIXMAP;
    pthread_mutex_lock(&g_host_handle_mutex);
    app_handle = host_app_find_by_screen(screen);
    if (!app_handle || !app_handle->guest_screen_address)
        goto done;
    mark = context_arena_mark(context);
    if (irix_host_abi_context_copy_string(context, image_name, &guest_name)) {
        context_arena_rewind(context, mark);
        goto done;
    }
    args[0] = app_handle->guest_screen_address;
    args[1] = guest_name;
    args[2] = (uint32_t)foreground;
    args[3] = (uint32_t)background;
    args[4] = (uint32_t)depth;
    if (irix_host_abi_context_call(context,
            by_depth ? "XmGetPixmapByDepth" : "XmGetPixmap",
            args, by_depth ? 5u : 4u, &result) != 0)
        result = XmUNSPECIFIED_PIXMAP;
    context_arena_rewind(context, mark);
done:
    pthread_mutex_unlock(&g_host_handle_mutex);
    return (Pixmap)result;
}

Pixmap XmGetPixmapByDepth(Screen *screen, char *image_name,
                          Pixel foreground, Pixel background, int depth)
{
    return host_motif_get_pixmap(screen, image_name, foreground, background,
                                 depth, 1);
}

Pixmap XmGetPixmap(Screen *screen, char *image_name,
                   Pixel foreground, Pixel background)
{
    return host_motif_get_pixmap(screen, image_name, foreground, background,
                                 0, 0);
}

Widget XmGetXmDisplay(Display *display)
{
    IrixHostAbiContext *context;
    IrixHostOpaqueHandle *app_handle, *widget_handle = NULL;
    uint32_t args[1], guest_widget = 0;
    if (!display || !context_default(&context))
        return NULL;
    pthread_mutex_lock(&g_host_handle_mutex);
    app_handle = host_app_find_by_display(display);
    if (app_handle) {
        args[0] = app_handle->guest_display_address;
        if (irix_host_abi_context_call(context, "XmGetXmDisplay", args, 1u,
                                       &guest_widget) == 0 && guest_widget) {
            widget_handle = host_handle_create(IRIX_HOST_HANDLE_WIDGET,
                guest_widget, 0, app_handle->guest_address);
        }
        /* Netscape 4 reads display.userGrabbed directly. During early bring-up
         * the translated XmGetXmDisplay may trap before its per-display object
         * exists; still return a zero-initialized native prefix for this
         * native-only query instead of letting the caller dereference NULL. */
        if (!widget_handle)
            widget_handle = host_handle_create(IRIX_HOST_HANDLE_WIDGET,
                guest_widget, 0, app_handle->guest_address);
        if (widget_handle) {
            if (widget_handle->widget_storage_size <
                sizeof(IrixHostXmDisplayWidgetPrefix)) {
                void *storage = calloc(1,
                    sizeof(IrixHostXmDisplayWidgetPrefix));
                if (!storage) {
                    widget_handle->alive = 0;
                    widget_handle = NULL;
                    goto done;
                }
                memcpy(storage, widget_handle->widget_object,
                       sizeof(WidgetRec));
                free(widget_handle->widget_storage);
                widget_handle->widget_storage = storage;
                widget_handle->widget_storage_size =
                    sizeof(IrixHostXmDisplayWidgetPrefix);
                widget_handle->widget_object = (Widget)storage;
                widget_handle->widget_object->core.self =
                    widget_handle->widget_object;
            }
            widget_handle->guest_display_address =
                app_handle->guest_display_address;
            widget_handle->guest_screen_address =
                app_handle->guest_screen_address;
            host_widget_set_display(widget_handle, display,
                                    app_handle->host_screen);
        }
    }
done:
    pthread_mutex_unlock(&g_host_handle_mutex);
    return host_widget_value(widget_handle);
}

Widget XtHooksOfDisplay(Display *display)
{
    IrixHostAbiContext *context;
    IrixHostOpaqueHandle *app_handle, *widget_handle = NULL;
    uint32_t args[1], guest_widget = 0;
    if (!display || !context_default(&context))
        return NULL;
    pthread_mutex_lock(&g_host_handle_mutex);
    app_handle = host_app_find_by_display(display);
    if (app_handle) {
        args[0] = app_handle->guest_display_address;
        if (irix_host_abi_context_call(context, "XtHooksOfDisplay", args, 1u,
                                       &guest_widget) == 0 && guest_widget) {
            widget_handle = host_handle_create(IRIX_HOST_HANDLE_WIDGET,
                guest_widget, 0, app_handle->guest_address);
            if (widget_handle) {
                widget_handle->guest_display_address =
                    app_handle->guest_display_address;
                widget_handle->guest_screen_address =
                    app_handle->guest_screen_address;
                host_widget_set_display(widget_handle, display,
                                        app_handle->host_screen);
            }
        }
    }
    pthread_mutex_unlock(&g_host_handle_mutex);
    return host_widget_value(widget_handle);
}

void XmGetColors(Screen *screen, Colormap color_map, Pixel background,
                 Pixel *foreground_return, Pixel *top_shadow_return,
                 Pixel *bottom_shadow_return, Pixel *select_return)
{
    IrixHostAbiContext *context;
    IrixHostOpaqueHandle *app_handle;
    IrixGuestAddr arena_mark, guest_pixels = 0;
    unsigned char zero_pixels[16] = { 0 };
    uint32_t args[7], guest_values[4] = { 0, 0, 0, 0 };
    int status = -1;
    if (!context_default(&context) || !screen)
        return;
    pthread_mutex_lock(&g_host_handle_mutex);
    app_handle = host_app_find_by_screen(screen);
    if (app_handle && app_handle->guest_screen_address) {
        arena_mark = context_arena_mark(context);
        if (irix_host_abi_context_copy_bytes(context, zero_pixels,
                sizeof(zero_pixels), &guest_pixels) == 0) {
            args[0] = app_handle->guest_screen_address;
            args[1] = (uint32_t)color_map;
            args[2] = (uint32_t)background;
            args[3] = guest_pixels;
            args[4] = guest_pixels + 4u;
            args[5] = guest_pixels + 8u;
            args[6] = guest_pixels + 12u;
            status = irix_host_abi_context_call(context, "XmGetColors",
                                                args, 7u, NULL);
            if (status == 0) {
                unsigned i;
                for (i = 0; i < 4u; ++i)
                    if (irix_host_abi_context_read32be(context,
                            guest_pixels + (uint32_t)(i * 4u),
                            &guest_values[i]) != 0) {
                        status = -1;
                        break;
                    }
            }
        }
        context_arena_rewind(context, arena_mark);
    }
    pthread_mutex_unlock(&g_host_handle_mutex);
    if (status != 0)
        return;
    if (foreground_return) *foreground_return = (Pixel)guest_values[0];
    if (top_shadow_return) *top_shadow_return = (Pixel)guest_values[1];
    if (bottom_shadow_return) *bottom_shadow_return = (Pixel)guest_values[2];
    if (select_return) *select_return = (Pixel)guest_values[3];
}

/*
 * The native XfeFrameShell class pointer belongs to Netscape's host ABI and
 * cannot be passed into translated N32 Xt.  Use IRIX Xt's TopLevelShell class
 * for the guest widget tree and retain the native class only in the host-side
 * facade used by Xfe's type checks.  Its class slot is the exported N32 object
 * in the IRIX 6.5 libXt image; the pointed-to class record remains in guest
 * memory.  Initial Xfe resources are intentionally omitted until their pointer
 * and XID fields have explicit conversions.
 */
Widget XtCreatePopupShell(_Xconst _XtString name, WidgetClass widget_class,
                          Widget parent, ArgList args, Cardinal arg_count)
{
    enum { IRIX_LIBXT_TOP_LEVEL_SHELL_CLASS_SLOT = 0x5003a370u };
    static int reported_resource_omission;
    IrixHostAbiContext *context;
    IrixHostOpaqueHandle *parent_handle, *widget_handle;
    IrixGuestAddr arena_mark, guest_name;
    uint32_t guest_class = 0, call_args[5], guest_widget = 0;
    uint32_t parent_display = 0, per_display_head = 0, first_display = 0;
    int status;
    if (!name || !context_default(&context))
        return NULL;
    pthread_mutex_lock(&g_host_handle_mutex);
    parent_handle = host_handle_find(parent, IRIX_HOST_HANDLE_WIDGET);
    if (!parent_handle) {
        pthread_mutex_unlock(&g_host_handle_mutex);
        return NULL;
    }
    {
        IrixHostOpaqueHandle *app_handle = NULL;
        for (app_handle = g_host_opaque_handles; app_handle;
             app_handle = app_handle->next)
            if (app_handle->alive &&
                app_handle->kind == IRIX_HOST_HANDLE_APP_CONTEXT &&
                app_handle->guest_address == parent_handle->app_guest_address)
                break;
        host_try_initialize_guest_perdisplay(context, app_handle,
            parent_handle->guest_display_address);
    }
    if (irix_host_abi_context_read32be(context,
            IRIX_LIBXT_TOP_LEVEL_SHELL_CLASS_SLOT, &guest_class) != 0 ||
        !guest_class) {
        pthread_mutex_unlock(&g_host_handle_mutex);
        return NULL;
    }
    (void)irix_host_abi_context_call(context, "XtDisplayOfObject",
        &parent_handle->guest_address, 1u, &parent_display);
    (void)irix_host_abi_context_read32be(context, 0x50010fb0u,
                                         &per_display_head);
    if (per_display_head)
        (void)irix_host_abi_context_read32be(context, per_display_head,
                                             &first_display);
    arena_mark = context_arena_mark(context);
    status = irix_host_abi_context_copy_string(context, name, &guest_name);
    if (status == 0) {
        call_args[0] = guest_name;
        call_args[1] = guest_class;
        call_args[2] = parent_handle->guest_address;
        call_args[3] = 0;
        call_args[4] = 0;
        status = irix_host_abi_context_call(context, "XtCreatePopupShell",
                                            call_args, 5u, &guest_widget);
    }
    context_arena_rewind(context, arena_mark);
    widget_handle = status == 0 && guest_widget
        ? host_handle_create(IRIX_HOST_HANDLE_WIDGET, guest_widget,
                             parent_handle->guest_address,
                             parent_handle->app_guest_address)
        : NULL;
    if (widget_handle) {
        if (widget_class) {
            WidgetClass ancestor;
            unsigned depth = 0;
            widget_class->core_class.class_inited |= 0x04u;
            for (ancestor = widget_class->core_class.superclass;
                 ancestor && depth++ < 32u;
                 ancestor = ancestor->core_class.superclass)
                widget_class->core_class.class_inited |=
                    ancestor->core_class.class_inited;
        }
        widget_handle->is_shell = 1u;
        host_prepare_xt_classes();
        (void)host_widget_promote_class(widget_handle,
            widget_class ? widget_class : g_host_top_level_shell_class);
        host_widget_rec(widget_handle)->core.name = strdup(name);
        if (widget_class)
            host_native_widget_initialize(widget_handle, widget_class, args,
                                           arg_count);
        /* Xt shell initialize methods may clear fields assigned during handle
         * creation. Reapply the host display/screen defaults afterwards. */
        host_widget_set_display(widget_handle, widget_handle->host_display,
                                 widget_handle->host_screen);
        host_apply_initial_geometry(widget_handle, args, arg_count);
    }
    if (arg_count && !reported_resource_omission) {
        fprintf(stderr,
            "libirixhostabi: XtCreatePopupShell substituted IRIX TopLevelShell for a native widget class and skipped %u Arg resources pending typed conversion\n",
            (unsigned)arg_count);
        reported_resource_omission = 1;
    }
    fprintf(stderr,
        "libirixhostabi: XtCreatePopupShell guest-status=%d registered=%d trap=%d pc=%08x fault=%08x class=%08x guest-widget=%08x host-handle=%p parent=%p guest-parent=%08x display=%08x stored-display=%08x per-display=%08x first-display=%08x\n",
        status, irix_host_abi_context_has_function(context,
            "XtCreatePopupShell"), context->runtime.trapped,
        context->runtime.current_pc, context->runtime.fault_addr,
        guest_class, guest_widget, (void *)widget_handle, (void *)parent,
        parent_handle->guest_address, parent_display,
        parent_handle->guest_display_address, per_display_head,
        first_display);
    pthread_mutex_unlock(&g_host_handle_mutex);
    (void)args;
    return host_widget_value(widget_handle);
}

Widget XtVaCreatePopupShell(_Xconst _XtString name, WidgetClass widget_class,
                            Widget parent, ...)
{
    const char *resource_name;
    va_list ap;
    unsigned resource_count = 0;
    static unsigned trace_count;
    if (!name)
        return NULL;
    va_start(ap, parent);
    while ((resource_name = va_arg(ap, const char *)) != NULL) {
        (void)va_arg(ap, XtArgVal);
        ++resource_count;
    }
    va_end(ap);
    if (getenv("IRIX_HOST_ABI_TRACE_WIDGETS") && trace_count++ < 128u)
        fprintf(stderr,
            "libirixhostabi: XtVaCreatePopupShell name=%s class=%p parent=%p argc=%u\n",
            name, (void *)widget_class, (void *)parent, resource_count);
    return XtCreatePopupShell(name, widget_class, parent, NULL,
                              (Cardinal)resource_count);
}

char *XtMalloc(Cardinal size)
{
    return (char *)malloc(size ? (size_t)size : 1u);
}

char *XtCalloc(Cardinal count, Cardinal size)
{
    if (size && (size_t)count > SIZE_MAX / (size_t)size)
        return NULL;
    return (char *)calloc(count ? (size_t)count : 1u,
                          size ? (size_t)size : 1u);
}

char *XtRealloc(char *old, Cardinal size)
{
    return (char *)realloc(old, size ? (size_t)size : 1u);
}

void XtFree(char *pointer)
{
    free(pointer);
}

#include "IrixHostAbiNativeClass.inc"

static Widget host_va_create_widget(const char *name, WidgetClass widget_class,
                                    Widget parent, va_list ap, int managed)
{
    Arg args[256];
    Cardinal count = 0;
    const char *resource;
    Widget widget;
    while ((resource = va_arg(ap, const char *)) != NULL) {
        if (count == 256) return NULL;
        args[count].name = (String)resource;
        args[count++].value = va_arg(ap, XtArgVal);
    }
    widget = XtCreateWidget(name, widget_class, parent, args, count);
    if (widget && managed) XtManageChild(widget);
    return widget;
}

Widget XtVaCreateManagedWidget(_Xconst _XtString name, WidgetClass widget_class,
                               Widget parent, ...)
{
    Widget widget;
    va_list ap;
    va_start(ap, parent);
    widget = host_va_create_widget(name, widget_class, parent, ap, 1);
    va_end(ap);
    return widget;
}

Widget XtCreateManagedWidget(_Xconst _XtString name, WidgetClass widget_class,
                             Widget parent, ArgList args, Cardinal count)
{
    Widget widget = XtCreateWidget(name, widget_class, parent, args, count);
    if (widget) XtManageChild(widget);
    return widget;
}

Widget XtVaCreateWidget(_Xconst _XtString name, WidgetClass widget_class,
                        Widget parent, ...)
{
    Widget widget;
    va_list ap;
    va_start(ap, parent);
    widget = host_va_create_widget(name, widget_class, parent, ap, 0);
    va_end(ap);
    return widget;
}

static IrixGuestAddr host_guest_widget_class_slot(WidgetClass widget_class)
{
    static const struct {
        const char *name;
        IrixGuestAddr slot;
    } classes[] = {
        { "XmArrowButton", 0x5006ef90u },
        { "XmBulletinBoard", 0x50074120u },
        { "XmCascadeButton", 0x500785a8u },
        { "XmDrawingArea", 0x50096350u },
        { "XmForm", 0x500a6370u },
        { "XmFrame", 0x500a85a8u },
        { "XmLabel", 0x500b2398u },
        { "XmList", 0x500c4eb0u },
        { "XmMainWindow", 0x500c6fb0u },
        { "XmManager", 0x5018d518u },
        { "XmPanedWindow", 0x500d51a8u },
        { "XmPrimitive", 0x50189b58u },
        { "XmPushButton", 0x500d9988u },
        { "XmRowColumn", 0x501024f0u },
        { "XmScale", 0x50108920u },
        { "XmScrollBar", 0x50112310u },
        { "XmScrolledWindow", 0x50118c28u },
        { "XmSeparator", 0x5011e170u },
        { "XmText", 0x50128d28u },
        { "XmTextField", 0x50176ce0u },
        { "XmToggleButton", 0x5014c430u }
    };
    WidgetClass current;
    unsigned depth = 0;
    for (current = widget_class; current && depth++ < 32u;
         current = current->core_class.superclass) {
        size_t i;
        const char *name = current->core_class.class_name;
        if (!name) continue;
        for (i = 0; i < sizeof(classes) / sizeof(classes[0]); ++i)
            if (!strcmp(name, classes[i].name))
                return classes[i].slot;
    }
    return 0x50074120u;
}

static void host_apply_initial_geometry(IrixHostOpaqueHandle *handle,
                                        ArgList args, Cardinal arg_count)
{
    CorePart *core;
    Cardinal i;
    int supplied = 0;
    int orientation = 1;
    if (!handle || !handle->widget_object || !args) return;
    core = &handle->widget_object->core;
    if (getenv("IRIX_HOST_ABI_TRACE_WIDGETS") && core->name &&
        (!strcmp(core->name, "vsb") || !strcmp(core->name, "hsb")))
        fprintf(stderr, "host ABI initial geometry %s facade-class=%s arg-count=%u\n",
            core->name, core->widget_class && core->widget_class->core_class.class_name
                ? core->widget_class->core_class.class_name : "?",
            (unsigned)arg_count);
    for (i = 0; i < arg_count; ++i) {
        XtArgVal value = args[i].value;
        if (host_resource_matches(args[i].name, "x")) {
            core->x = (Position)(intptr_t)value;
            supplied = 1;
        } else if (host_resource_matches(args[i].name, "y")) {
            core->y = (Position)(intptr_t)value;
            supplied = 1;
        } else if (host_resource_matches(args[i].name, "width")) {
            core->width = (Dimension)value;
            supplied = 1;
        } else if (host_resource_matches(args[i].name, "height")) {
            core->height = (Dimension)value;
            supplied = 1;
        } else if (host_resource_matches(args[i].name, "borderWidth")) {
            core->border_width = (Dimension)value;
            supplied = 1;
        } else if (host_resource_matches(args[i].name, "orientation")) {
            orientation = (int)value;
        }
    }
    /* Motif normally supplies the cross-axis scrollbar thickness during its
     * class initializer. The native/guest initializer is not shared across
     * this ABI boundary, so seed the standard Motif default dimensions here
     * when an application-created scrollbar has no geometry yet. */
    if (handle->widget_object->core.widget_class &&
        handle->widget_object->core.widget_class->core_class.class_name &&
        !strcmp(handle->widget_object->core.widget_class->core_class.class_name,
                "XmScrollBar") && (!core->width || !core->height)) {
        IrixHostOpaqueHandle *parent = host_handle_find_guest(
            handle->parent_guest_address, IRIX_HOST_HANDLE_WIDGET);
        Dimension parent_width = parent ? parent->widget_object->core.width : 0;
        Dimension parent_height = parent ? parent->widget_object->core.height : 0;
        if (orientation == 2) {
            if (!core->width) core->width = 16;
            if (!core->height) core->height = parent_height ? parent_height : 100;
        } else {
            if (!core->width) core->width = parent_width ? parent_width : 100;
            if (!core->height) core->height = 16;
        }
        supplied = 1;
    }
    if (supplied && core->width && core->height)
        XtConfigureWidget(host_widget_value(handle), core->x, core->y,
                          core->width, core->height, core->border_width);
}

Widget XtCreateWidget(_Xconst _XtString name, WidgetClass widget_class,
                      Widget parent, ArgList args, Cardinal arg_count)
{
    IrixHostAbiContext *context;
    IrixHostOpaqueHandle *parent_handle, *widget_handle;
    IrixGuestAddr arena_mark, guest_name;
    uint32_t guest_class = 0, call_args[5], guest_widget = 0;
    IrixGuestAddr guest_class_slot;
    int status;
    static int reported;
    if (!name || !widget_class || (arg_count && !args) ||
        !context_default(&context))
        return NULL;
    pthread_mutex_lock(&g_host_handle_mutex);
    parent_handle = host_handle_find(parent, IRIX_HOST_HANDLE_WIDGET);
    guest_class_slot = host_guest_widget_class_slot(widget_class);
    if (!parent_handle || irix_host_abi_context_read32be(context,
            guest_class_slot, &guest_class) != 0 ||
        !guest_class) {
        pthread_mutex_unlock(&g_host_handle_mutex);
        return NULL;
    }
    arena_mark = context_arena_mark(context);
    status = irix_host_abi_context_copy_string(context, name, &guest_name);
    if (status == 0) {
        call_args[0] = guest_name;
        call_args[1] = guest_class;
        call_args[2] = parent_handle->guest_address;
        call_args[3] = 0;
        call_args[4] = 0;
        status = irix_host_abi_context_call(context, "XtCreateWidget",
                                            call_args, 5u, &guest_widget);
    }
    context_arena_rewind(context, arena_mark);
    widget_handle = status == 0 && guest_widget
        ? host_handle_create(IRIX_HOST_HANDLE_WIDGET, guest_widget,
                             parent_handle->guest_address,
                             parent_handle->app_guest_address)
        : NULL;
    if (widget_handle) {
        WidgetClass ancestor;
        unsigned depth = 0;
        widget_class->core_class.class_inited |= 0x04u;
        for (ancestor = widget_class->core_class.superclass;
             ancestor && depth++ < 32; ancestor = ancestor->core_class.superclass)
            widget_class->core_class.class_inited |= ancestor->core_class.class_inited;
        (void)host_widget_promote_class(widget_handle, widget_class);
        host_widget_rec(widget_handle)->core.name = strdup(name);
        host_native_prepare_constraint(parent_handle, widget_handle);
        host_native_widget_initialize(widget_handle, widget_class, args, arg_count);
        host_native_constraint_initialize(parent_handle, widget_handle,
                                          args, arg_count);
        host_sync_guest_geometry(context, widget_handle);
        host_apply_initial_geometry(widget_handle, args, arg_count);
        host_native_composite_insert_child(parent_handle, widget_handle);
        if (getenv("IRIX_HOST_ABI_TRACE_WIDGETS") &&
            (strstr(name, "vsb") || strstr(name, "hsb") ||
             strstr(name, "ScrollBar"))) {
            CorePart *core = &host_widget_rec(widget_handle)->core;
            fprintf(stderr, "host ABI scrollbar geometry %s class=%s x=%d y=%d w=%u h=%u bw=%u args=%u\n",
                name, widget_class->core_class.class_name,
                core->x, core->y, core->width, core->height,
                core->border_width, (unsigned)arg_count);
        }
    }
    if (!reported) {
        fprintf(stderr,
            "libirixhostabi: XtCreateWidget currently maps native widget classes to IRIX classes; unsupported native Arg resources are skipped (first list has %u entries)\n",
            (unsigned)arg_count);
        reported = 1;
    }
    pthread_mutex_unlock(&g_host_handle_mutex);
    return host_widget_value(widget_handle);
}

void XtAddEventHandler(Widget widget, EventMask event_mask,
                       Boolean nonmaskable, XtEventHandler proc,
                       XtPointer closure)
{
    IrixHostOpaqueHandle *handle;
    (void)event_mask;
    (void)nonmaskable;
    (void)proc;
    (void)closure;
    pthread_mutex_lock(&g_host_handle_mutex);
    handle = host_handle_find(widget, IRIX_HOST_HANDLE_WIDGET);
    pthread_mutex_unlock(&g_host_handle_mutex);
    if (handle)
        host_report_callbacks_skipped();
}

void XtInsertEventHandler(Widget widget, EventMask event_mask,
                          Boolean nonmaskable, XtEventHandler proc,
                          XtPointer closure, XtListPosition position)
{
    (void)event_mask;
    (void)nonmaskable;
    (void)proc;
    (void)closure;
    (void)position;
    XtAddEventHandler(widget, event_mask, nonmaskable, proc, closure);
}

void XtRemoveEventHandler(Widget widget, EventMask event_mask,
                          Boolean nonmaskable, XtEventHandler proc,
                          XtPointer closure)
{
    (void)widget;
    (void)event_mask;
    (void)nonmaskable;
    (void)proc;
    (void)closure;
}

void XtGetApplicationResources(Widget widget, XtPointer base,
                               XtResourceList resources,
                               Cardinal resource_count, ArgList args,
                               Cardinal arg_count)
{
    IrixHostOpaqueHandle *handle;
    Cardinal i;
    static int reported_arg_omission;
    if (!widget || !base || !resources)
        return;
    pthread_mutex_lock(&g_host_handle_mutex);
    handle = host_handle_find(widget, IRIX_HOST_HANDLE_WIDGET);
    pthread_mutex_unlock(&g_host_handle_mutex);
    if (!handle)
        return;

    /*
     * Netscape's startup resources are native XtResource records and write to
     * native application structs.  Apply their compiled-in defaults directly
     * for now; guest Xt must never receive those host pointers or offsets.
     */
    for (i = 0; i < resource_count; ++i) {
        XtResource *resource = &resources[i];
        unsigned char *destination;
        uintptr_t immediate;
        size_t copy_size;
        if (!resource->resource_size ||
            resource->resource_size > sizeof(immediate) ||
            (resource->default_type &&
             strcmp(resource->default_type, XtRCallProc) == 0))
            continue;
        destination = (unsigned char *)base + resource->resource_offset;
        if (resource->resource_size == sizeof(Pixel) &&
            resource->resource_type &&
            strcmp(resource->resource_type, XtRPixel) == 0 &&
            resource->default_type &&
            strcmp(resource->default_type, XtRString) == 0 &&
            resource->default_addr) {
            const char *color_name = (const char *)resource->default_addr;
            Pixel pixel = 0;
            if (!strcmp(color_name, XtDefaultForeground)) {
                pixel = handle->host_screen
                    ? BlackPixelOfScreen(handle->host_screen) : 0u;
            } else if (!strcmp(color_name, XtDefaultBackground)) {
                pixel = handle->host_screen
                    ? WhitePixelOfScreen(handle->host_screen) : 0u;
            } else if (handle->host_display && handle->host_screen) {
                XColor exact, screen;
                Colormap colormap = DefaultColormapOfScreen(handle->host_screen);
                if (XAllocNamedColor(handle->host_display, colormap,
                                     color_name, &screen, &exact))
                    pixel = screen.pixel;
            }
            memcpy(destination, &pixel, sizeof(pixel));
            continue;
        }
        immediate = (uintptr_t)resource->default_addr;
        copy_size = resource->resource_size;
        if (copy_size > sizeof(immediate))
            copy_size = sizeof(immediate);
        memcpy(destination, &immediate, copy_size);
    }
    if (arg_count && !reported_arg_omission) {
        fprintf(stderr,
            "libirixhostabi: XtGetApplicationResources applied compiled defaults and skipped %u Arg overrides\n",
            (unsigned)arg_count);
        reported_arg_omission = 1;
    }
    (void)args;
}

void XtGetSubresources(Widget widget, XtPointer base,
                       _Xconst _XtString name,
                       _Xconst _XtString class_name,
                       XtResourceList resources,
                       Cardinal resource_count, ArgList args,
                       Cardinal arg_count)
{
    IrixHostOpaqueHandle *handle;
    Cardinal i;
    static int reported_arg_omission;
    if (!widget || !base || !resources)
        return;
    pthread_mutex_lock(&g_host_handle_mutex);
    handle = host_handle_find(widget, IRIX_HOST_HANDLE_WIDGET);
    pthread_mutex_unlock(&g_host_handle_mutex);
    if (!handle)
        return;
    for (i = 0; i < resource_count; ++i) {
        XtResource *resource = &resources[i];
        uintptr_t default_value;
        if (!resource->resource_size ||
            resource->resource_size > sizeof(default_value) ||
            (resource->default_type &&
             strcmp(resource->default_type, XtRCallProc) == 0))
            continue;
        default_value = (uintptr_t)resource->default_addr;
        memcpy((unsigned char *)base + resource->resource_offset,
               &default_value, resource->resource_size);
    }
    if (arg_count && !reported_arg_omission) {
        fprintf(stderr,
            "libirixhostabi: XtGetSubresources applied compiled defaults and skipped %u Arg overrides\n",
            (unsigned)arg_count);
        reported_arg_omission = 1;
    }
    (void)name;
    (void)class_name;
    (void)args;
}

void XtGetApplicationNameAndClass(Display *display, String *name_return,
                                  String *class_return)
{
    IrixHostAbiContext *context;
    IrixHostOpaqueHandle *app_handle;
    IrixGuestAddr output;
    uint32_t args[2], guest_name = 0, guest_class = 0;
    char *host_name, *host_class;
    unsigned char zero[8] = { 0 };
    if (name_return) *name_return = NULL;
    if (class_return) *class_return = NULL;
    if ((!name_return && !class_return) || !context_default(&context))
        return;
    if (!display)
        fprintf(stderr, "libirixhostabi: XtGetApplicationNameAndClass received NULL Display\n");
    pthread_mutex_lock(&g_host_handle_mutex);
    app_handle = host_app_find_by_display(display);
    if (!app_handle)
        app_handle = host_app_find_single();
    if (!app_handle) {
        fprintf(stderr, "libirixhostabi: XtGetApplicationNameAndClass display=%p has no app-context mapping\n", (void *)display);
        goto unlock;
    }
    fprintf(stderr, "libirixhostabi: XtGetApplicationNameAndClass display=%p mapped=%p name=%p class=%p\n",
            (void *)display, (void *)app_handle->host_display,
            (void *)app_handle->host_app_name, (void *)app_handle->host_app_class);
    if (!app_handle->host_app_name || !app_handle->host_app_class) {
        if (irix_host_abi_context_copy_bytes(context, zero, sizeof(zero),
                                             &output) != 0)
            goto unlock;
        args[0] = app_handle->guest_display_address;
        args[1] = output;
        if (irix_host_abi_context_call(context, "XtGetApplicationNameAndClass",
                                       args, 2u, NULL) != 0 ||
            irix_host_abi_context_read32be(context, output, &guest_name) != 0 ||
            irix_host_abi_context_read32be(context, output + 4u,
                                           &guest_class) != 0)
            goto unlock;
        host_name = context_guest_string_dup(context, guest_name);
        host_class = context_guest_string_dup(context, guest_class);
        if (!host_name || !host_class) {
            free(host_name);
            free(host_class);
            goto unlock;
        }
        app_handle->host_app_name = host_name;
        app_handle->host_app_class = host_class;
    }
    if (name_return) *name_return = app_handle->host_app_name;
    if (class_return) *class_return = app_handle->host_app_class;
unlock:
    pthread_mutex_unlock(&g_host_handle_mutex);
}

XtTranslations XtParseTranslationTable(const char *table)
{
    IrixHostAbiContext *context;
    IrixHostOpaqueHandle *handle = NULL;
    IrixGuestAddr arena_mark, guest_table;
    uint32_t guest_translations = 0;
    uint32_t args[1];
    int status;
    if (!table || !context_default(&context))
        return NULL;
    arena_mark = context_arena_mark(context);
    status = irix_host_abi_context_copy_string(context, table, &guest_table);
    if (status == 0) {
        args[0] = guest_table;
        status = irix_host_abi_context_call(context, "XtParseTranslationTable",
                                            args, 1u, &guest_translations);
    }
    context_arena_rewind(context, arena_mark);
    if (status != 0 || !guest_translations)
        return NULL;
    pthread_mutex_lock(&g_host_handle_mutex);
    handle = host_handle_create(IRIX_HOST_HANDLE_TRANSLATIONS,
                                guest_translations, 0, 0);
    pthread_mutex_unlock(&g_host_handle_mutex);
    return handle ? (XtTranslations)handle : NULL;
}

void XtOverrideTranslations(Widget widget, XtTranslations translations)
{
    IrixHostAbiContext *context;
    IrixHostOpaqueHandle *widget_handle, *translations_handle;
    uint32_t args[2];
    if (!context_default(&context))
        return;
    pthread_mutex_lock(&g_host_handle_mutex);
    widget_handle = host_handle_find(widget, IRIX_HOST_HANDLE_WIDGET);
    translations_handle = host_handle_find(
        translations, IRIX_HOST_HANDLE_TRANSLATIONS);
    if (widget_handle && translations_handle) {
        args[0] = widget_handle->guest_address;
        args[1] = translations_handle->guest_address;
        (void)irix_host_abi_context_call(context, "XtOverrideTranslations",
                                         args, 2u, NULL);
    }
    pthread_mutex_unlock(&g_host_handle_mutex);
}

void XtAugmentTranslations(Widget widget, XtTranslations translations)
{
    IrixHostAbiContext *context;
    IrixHostOpaqueHandle *widget_handle, *translations_handle;
    uint32_t args[2];
    if (!context_default(&context))
        return;
    pthread_mutex_lock(&g_host_handle_mutex);
    widget_handle = host_handle_find(widget, IRIX_HOST_HANDLE_WIDGET);
    translations_handle = host_handle_find(
        translations, IRIX_HOST_HANDLE_TRANSLATIONS);
    if (widget_handle && translations_handle) {
        args[0] = widget_handle->guest_address;
        args[1] = translations_handle->guest_address;
        (void)irix_host_abi_context_call(context, "XtAugmentTranslations",
                                         args, 2u, NULL);
    }
    pthread_mutex_unlock(&g_host_handle_mutex);
}

static void host_report_callbacks_skipped(void)
{
    pthread_mutex_lock(&g_host_handle_mutex);
    if (!g_reported_callbacks_skipped) {
        fprintf(stderr, "libirixhostabi: native Xt callbacks are not bridged yet; registrations are being skipped\n");
        g_reported_callbacks_skipped = 1;
    }
    pthread_mutex_unlock(&g_host_handle_mutex);
}

typedef struct IrixHostAbiTimer {
    XtIntervalId id;
    uint64_t deadline_ns;
    XtTimerCallbackProc callback;
    XtPointer closure;
    struct IrixHostAbiTimer *next;
} IrixHostAbiTimer;
static IrixHostAbiTimer *g_host_abi_timers;
static unsigned g_host_abi_timer_event_pending;

static void host_sync_guest_widget_windows(IrixHostAbiContext *context,
                                           IrixGuestAddr app_address)
{
    (void)context;
    (void)app_address;
}
static XtIntervalId g_host_abi_next_timer = 1;

static uint64_t host_abi_monotonic_ns(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0;
    return (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
}

void XtAddCallback(Widget widget, const char *callback_name,
                   XtCallbackProc callback, XtPointer closure)
{
    IrixHostOpaqueHandle *handle;
    (void)callback_name;
    (void)callback;
    (void)closure;
    pthread_mutex_lock(&g_host_handle_mutex);
    handle = host_handle_find(widget, IRIX_HOST_HANDLE_WIDGET);
    pthread_mutex_unlock(&g_host_handle_mutex);
    if (handle)
        host_report_callbacks_skipped();
}

void XtRemoveCallback(Widget widget, const char *callback_name,
                      XtCallbackProc callback, XtPointer closure)
{
    IrixHostOpaqueHandle *handle;
    (void)callback_name;
    (void)callback;
    (void)closure;
    pthread_mutex_lock(&g_host_handle_mutex);
    handle = host_handle_find(widget, IRIX_HOST_HANDLE_WIDGET);
    pthread_mutex_unlock(&g_host_handle_mutex);
    if (handle)
        host_report_callbacks_skipped();
}

void XtAddCallbacks(Widget widget, const char *callback_name,
                    XtCallbackList callbacks)
{
    IrixHostOpaqueHandle *handle;
    (void)callback_name;
    (void)callbacks;
    pthread_mutex_lock(&g_host_handle_mutex);
    handle = host_handle_find(widget, IRIX_HOST_HANDLE_WIDGET);
    pthread_mutex_unlock(&g_host_handle_mutex);
    if (handle)
        host_report_callbacks_skipped();
}

void XtRemoveCallbacks(Widget widget, const char *callback_name,
                       XtCallbackList callbacks)
{
    IrixHostOpaqueHandle *handle;
    (void)callback_name;
    (void)callbacks;
    pthread_mutex_lock(&g_host_handle_mutex);
    handle = host_handle_find(widget, IRIX_HOST_HANDLE_WIDGET);
    pthread_mutex_unlock(&g_host_handle_mutex);
    if (handle)
        host_report_callbacks_skipped();
}

void XtRemoveAllCallbacks(Widget widget, const char *callback_name)
{
    IrixHostOpaqueHandle *handle;
    (void)callback_name;
    pthread_mutex_lock(&g_host_handle_mutex);
    handle = host_handle_find(widget, IRIX_HOST_HANDLE_WIDGET);
    pthread_mutex_unlock(&g_host_handle_mutex);
    if (handle)
        host_report_callbacks_skipped();
}

Atom XmInternAtom(Display *display, char *name, Boolean only_if_exists)
{
    IrixHostAbiContext *context;
    IrixHostOpaqueHandle *app_handle;
    IrixGuestAddr arena_mark, guest_name;
    uint32_t call_args[3], guest_atom = 0;
    int status;

    if (!display || !name || !context_default(&context))
        return None;
    pthread_mutex_lock(&g_host_handle_mutex);
    app_handle = host_app_find_by_display(display);
    if (!app_handle || !app_handle->guest_display_address) {
        pthread_mutex_unlock(&g_host_handle_mutex);
        return None;
    }
    arena_mark = context_arena_mark(context);
    status = irix_host_abi_context_copy_string(context, name, &guest_name);
    if (status == 0) {
        call_args[0] = app_handle->guest_display_address;
        call_args[1] = guest_name;
        call_args[2] = only_if_exists ? 1u : 0u;
        status = irix_host_abi_context_call(context, "XmInternAtom",
                                            call_args, 3u, &guest_atom);
    }
    context_arena_rewind(context, arena_mark);
    pthread_mutex_unlock(&g_host_handle_mutex);
    return status == 0 ? (Atom)guest_atom : None;
}

void XmAddWMProtocolCallback(Widget widget, Atom protocol,
                             XtCallbackProc callback, XtPointer closure)
{
    IrixHostOpaqueHandle *handle;
    (void)protocol;
    (void)callback;
    (void)closure;
    pthread_mutex_lock(&g_host_handle_mutex);
    handle = host_handle_find(widget, IRIX_HOST_HANDLE_WIDGET);
    pthread_mutex_unlock(&g_host_handle_mutex);
    if (handle)
        host_report_callbacks_skipped();
}

void XmAddProtocolCallback(Widget widget, Atom property, Atom protocol,
                           XtCallbackProc callback, XtPointer closure)
{
    IrixHostOpaqueHandle *handle;
    (void)property; (void)protocol; (void)callback; (void)closure;
    pthread_mutex_lock(&g_host_handle_mutex);
    handle = host_handle_find(widget, IRIX_HOST_HANDLE_WIDGET);
    pthread_mutex_unlock(&g_host_handle_mutex);
    if (handle) host_report_callbacks_skipped();
}

void XmRemoveProtocolCallback(Widget widget, Atom property, Atom protocol,
                              XtCallbackProc callback, XtPointer closure)
{
    IrixHostOpaqueHandle *handle;
    (void)property; (void)protocol; (void)callback; (void)closure;
    pthread_mutex_lock(&g_host_handle_mutex);
    handle = host_handle_find(widget, IRIX_HOST_HANDLE_WIDGET);
    pthread_mutex_unlock(&g_host_handle_mutex);
    if (handle) host_report_callbacks_skipped();
}

XtIntervalId XtAppAddTimeOut(XtAppContext app_context,
                             unsigned long interval,
                             XtTimerCallbackProc callback,
                             XtPointer closure)
{
    IrixHostOpaqueHandle *handle;
    IrixHostAbiTimer *timer;
    const char *fast_ms = getenv("IRIX_HOST_ABI_FAST_TIMERS_MS");
    uint64_t delay_ms = interval;
    pthread_mutex_lock(&g_host_handle_mutex);
    handle = host_handle_find(app_context, IRIX_HOST_HANDLE_APP_CONTEXT);
    if (!handle || !callback) {
        pthread_mutex_unlock(&g_host_handle_mutex);
        return (XtIntervalId)0;
    }
    if (fast_ms && *fast_ms) {
        char *end = NULL;
        unsigned long parsed = strtoul(fast_ms, &end, 10);
        if (end && !*end && parsed) delay_ms = parsed;
    }
    timer = (IrixHostAbiTimer *)calloc(1, sizeof(*timer));
    if (!timer) {
        pthread_mutex_unlock(&g_host_handle_mutex);
        return (XtIntervalId)0;
    }
    timer->id = g_host_abi_next_timer++;
    if (!timer->id) timer->id = g_host_abi_next_timer++;
    timer->deadline_ns = host_abi_monotonic_ns() + delay_ms * 1000000ull;
    timer->callback = callback;
    timer->closure = closure;
    timer->next = g_host_abi_timers;
    g_host_abi_timers = timer;
    pthread_mutex_unlock(&g_host_handle_mutex);
    return timer->id;
}

void XtRemoveTimeOut(XtIntervalId timer)
{
    IrixHostAbiTimer **cursor;
    pthread_mutex_lock(&g_host_handle_mutex);
    for (cursor = &g_host_abi_timers; *cursor; cursor = &(*cursor)->next) {
        if ((*cursor)->id == timer) {
            IrixHostAbiTimer *removed = *cursor;
            *cursor = removed->next;
            free(removed);
            break;
        }
    }
    pthread_mutex_unlock(&g_host_handle_mutex);
}

XtInputId XtAppAddInput(XtAppContext app_context, int source,
                        XtPointer condition, XtInputCallbackProc callback,
                        XtPointer closure)
{
    IrixHostOpaqueHandle *handle;
    (void)source;
    (void)condition;
    (void)callback;
    (void)closure;
    pthread_mutex_lock(&g_host_handle_mutex);
    handle = host_handle_find(app_context, IRIX_HOST_HANDLE_APP_CONTEXT);
    pthread_mutex_unlock(&g_host_handle_mutex);
    if (handle)
        host_report_callbacks_skipped();
    return (XtInputId)0;
}

XtInputMask XtAppPending(XtAppContext app_context)
{
    IrixHostOpaqueHandle *handle;
    IrixHostAbiTimer *due = NULL, **cursor;
    uint64_t now = host_abi_monotonic_ns();
    pthread_mutex_lock(&g_host_handle_mutex);
    handle = host_handle_find(app_context, IRIX_HOST_HANDLE_APP_CONTEXT);
    if (!handle) {
        pthread_mutex_unlock(&g_host_handle_mutex);
        return 0;
    }
    if (g_host_abi_timer_event_pending) {
        pthread_mutex_unlock(&g_host_handle_mutex);
        return XtIMTimer;
    }
    for (cursor = &g_host_abi_timers; *cursor; cursor = &(*cursor)->next) {
        if ((*cursor)->deadline_ns <= now) {
            due = *cursor;
            *cursor = due->next;
            break;
        }
    }
    pthread_mutex_unlock(&g_host_handle_mutex);
    if (due) {
        XtIntervalId id = due->id;
        XtTimerCallbackProc callback = due->callback;
        XtPointer closure = due->closure;
        free(due);
        callback(closure, &id);
        pthread_mutex_lock(&g_host_handle_mutex);
        ++g_host_abi_timer_event_pending;
        pthread_mutex_unlock(&g_host_handle_mutex);
        return XtIMTimer;
    }
    if (handle->host_display &&
        XEventsQueued(handle->host_display, QueuedAfterReading) > 0)
        return XtIMXEvent;
    return 0;
}

Boolean XtAppPeekEvent(XtAppContext app_context, XEvent *event_return)
{
    IrixHostOpaqueHandle *handle;
    if (!event_return)
        return False;
    pthread_mutex_lock(&g_host_handle_mutex);
    handle = host_handle_find(app_context, IRIX_HOST_HANDLE_APP_CONTEXT);
    if (!handle || !handle->host_display) {
        memset(event_return, 0, sizeof(*event_return));
        pthread_mutex_unlock(&g_host_handle_mutex);
        return False;
    }
    if (g_host_abi_timer_event_pending) {
        memset(event_return, 0, sizeof(*event_return));
        pthread_mutex_unlock(&g_host_handle_mutex);
        return False;
    }
    {
        Display *display = handle->host_display;
        pthread_mutex_unlock(&g_host_handle_mutex);
        XPeekEvent(display, event_return);
    }
    return True;
}

void XtAppNextEvent(XtAppContext app_context, XEvent *event_return)
{
    IrixHostOpaqueHandle *handle;
    Display *display = NULL;
    if (!event_return)
        return;
    pthread_mutex_lock(&g_host_handle_mutex);
    handle = host_handle_find(app_context, IRIX_HOST_HANDLE_APP_CONTEXT);
    if (handle)
        display = handle->host_display;
    pthread_mutex_unlock(&g_host_handle_mutex);
    if (display)
        XNextEvent(display, event_return);
    else
        memset(event_return, 0, sizeof(*event_return));
}

Boolean XtDispatchEvent(XEvent *event)
{
    IrixHostAbiContext *context;
    IrixHostOpaqueHandle *app_handle;
    IrixGuestAddr mark, guest_event;
    uint32_t args[1], result = False;
    unsigned char zero[96] = {0};
    int status = -1;

    if (!event || !context_default(&context))
        return False;
    pthread_mutex_lock(&g_host_handle_mutex);
    app_handle = host_app_find_by_display(event->xany.display);
    mark = context_arena_mark(context);
    if (app_handle && app_handle->guest_display_address &&
        !irix_host_abi_context_copy_bytes(context, zero, sizeof(zero),
                                          &guest_event) &&
        irix_x11_image_write_n32_event(&context->runtime, guest_event,
                                       app_handle->guest_display_address,
                                       event)) {
        args[0] = guest_event;
        status = irix_host_abi_context_call(context, "XtDispatchEvent",
                                             args, 1u, &result);
    }
    context_arena_rewind(context, mark);
    pthread_mutex_unlock(&g_host_handle_mutex);
    return status == 0 ? (Boolean)result : False;
}

void XtAppProcessEvent(XtAppContext app_context, XtInputMask mask)
{
    IrixHostAbiContext *context;
    IrixHostOpaqueHandle *handle;
    uint32_t args[2];
    if ((mask & XtIMTimer) != 0u) {
        pthread_mutex_lock(&g_host_handle_mutex);
        if (g_host_abi_timer_event_pending) {
            --g_host_abi_timer_event_pending;
            pthread_mutex_unlock(&g_host_handle_mutex);
            return;
        }
        pthread_mutex_unlock(&g_host_handle_mutex);
    }
    if ((mask & XtIMXEvent) != 0u) {
        /* Dispatch using the guest Xt context that owns these widgets. */
    }
    if (!context_default(&context)) return;
    pthread_mutex_lock(&g_host_handle_mutex);
    handle = host_handle_find(app_context, IRIX_HOST_HANDLE_APP_CONTEXT);
    if (handle) {
        args[0] = handle->guest_address;
        args[1] = (uint32_t)mask;
        (void)irix_host_abi_context_call(context, "XtAppProcessEvent",
                                         args, 2u, NULL);
    }
    pthread_mutex_unlock(&g_host_handle_mutex);
}

void XtRemoveInput(XtInputId input)
{
    (void)input;
}

XtCallbackStatus XtHasCallbacks(Widget widget, _Xconst _XtString name)
{
    IrixHostAbiContext *context;
    IrixHostOpaqueHandle *handle;
    IrixGuestAddr mark, guest_name;
    uint32_t args[2], result = XtCallbackNoList;
    if (!name || !context_default(&context)) return XtCallbackNoList;
    pthread_mutex_lock(&g_host_handle_mutex);
    handle=host_handle_find(widget,IRIX_HOST_HANDLE_WIDGET);
    if (handle) {
        mark=context_arena_mark(context);
        if (!irix_host_abi_context_copy_string(context,name,&guest_name)) {
            args[0]=handle->guest_address; args[1]=guest_name;
            if (irix_host_abi_context_call(context,"XtHasCallbacks",args,2,&result))
                result=XtCallbackNoList;
        }
        context_arena_rewind(context,mark);
    }
    pthread_mutex_unlock(&g_host_handle_mutex);
    return (XtCallbackStatus)result;
}

static const char *host_scalar_resource_name(const char *name);

static int host_native_get_resource(Widget widget, const char *name,
                                    void *destination)
{
    WidgetClass wc;
    if (!widget || !name || !destination) return 0;
    for (wc = XtClass(widget); wc; wc = wc->core_class.superclass) {
        Cardinal i;
        for (i = 0; i < wc->core_class.num_resources; ++i) {
            XtResource *resource = &wc->core_class.resources[i];
            if (resource->resource_name == name ||
                (resource->resource_name &&
                 !strcmp(resource->resource_name, name))) {
                if (resource->resource_offset > XtClass(widget)->core_class.widget_size ||
                    resource->resource_size > XtClass(widget)->core_class.widget_size -
                        resource->resource_offset)
                    return 0;
                memcpy(destination, (char *)widget + resource->resource_offset,
                       resource->resource_size);
                return 1;
            }
        }
    }
    return 0;
}

static int host_xt_get_scalar(IrixHostAbiContext *context,
                              IrixHostOpaqueHandle *handle,
                              const char *name, uint32_t *value)
{
    IrixGuestAddr mark, guest_name, guest_args;
    const char *canonical = host_scalar_resource_name(name);
    IrixN32ArgRec zero_record = {0, 0};
    uint32_t call_args[3];
    int status = -1;
    if (!canonical) return -1;
    mark = context_arena_mark(context);
    if (!irix_host_abi_context_copy_string(context, canonical, &guest_name) &&
        !irix_host_abi_context_copy_bytes(context, &zero_record,
            sizeof(zero_record), &guest_args) &&
        !irix_host_abi_context_write32be(context, guest_args, guest_name) &&
        !irix_host_abi_context_write32be(context, guest_args + 4u, 0u)) {
        call_args[0] = handle->guest_address;
        call_args[1] = guest_args;
        call_args[2] = 1u;
        status = irix_host_abi_context_call(context, "XtGetValues",
                                             call_args, 3u, NULL);
        if (!status)
            status = irix_host_abi_context_read32be(context, guest_args + 4u,
                                                     value);
    }
    context_arena_rewind(context, mark);
    return status;
}

/* A host Motif class facade has no host Motif initializer of its own.  The
 * translated IRIX initializer has already populated the guest widget, so
 * copy the small, ABI-stable geometry values back to its host facade instead
 * of re-running guest class methods on host Widget records. */
static void host_sync_guest_geometry(IrixHostAbiContext *context,
                                     IrixHostOpaqueHandle *handle)
{
    CorePart *core;
    uint32_t value;
    static const char *const fields[] = { "x", "y", "width", "height",
                                           "borderWidth" };
    Cardinal i;
    int changed = 0;
    if (!context || !handle || !handle->widget_object)
        return;
    core = &host_widget_rec(handle)->core;
    for (i = 0; i < XtNumber(fields); ++i) {
        if (host_xt_get_scalar(context, handle, fields[i], &value) != 0)
            continue;
        if (!strcmp(fields[i], "x")) core->x = (Position)(int32_t)value;
        else if (!strcmp(fields[i], "y")) core->y = (Position)(int32_t)value;
        else if (!strcmp(fields[i], "width")) core->width = (Dimension)value;
        else if (!strcmp(fields[i], "height")) core->height = (Dimension)value;
        else core->border_width = (Dimension)value;
        changed = 1;
    }
    if (changed && getenv("IRIX_HOST_ABI_TRACE_WIDGETS") && core->name &&
        (!strcmp(core->name, "vsb") || !strcmp(core->name, "hsb")))
        fprintf(stderr,
            "host ABI copied guest geometry %s x=%d y=%d w=%u h=%u bw=%u\n",
            core->name, core->x, core->y, core->width, core->height,
            core->border_width);
}

void XtVaGetValues(Widget widget, ...)
{
    IrixHostAbiContext *context = NULL;
    IrixHostOpaqueHandle *handle;
    const char *name;
    va_list ap;
    unsigned unsupported = 0;
    static int reported;
    static unsigned trace_count;

    pthread_mutex_lock(&g_host_handle_mutex);
    handle = host_handle_find(widget, IRIX_HOST_HANDLE_WIDGET);
    if (!handle) {
        pthread_mutex_unlock(&g_host_handle_mutex);
        return;
    }
    va_start(ap, widget);
    while ((name = va_arg(ap, const char *)) != NULL) {
        void *destination = va_arg(ap, void *);
        int native_found = host_native_get_resource(widget, name, destination);
        int font_resource = host_resource_matches(name, "defaultFontList") ||
                            host_resource_matches(name, "buttonFontList") ||
                            host_resource_matches(name, "fontList");
        if (getenv("IRIX_HOST_ABI_TRACE_WIDGETS") && destination &&
            (host_resource_matches(name, "defaultFontList") ||
             host_resource_matches(name, "buttonFontList") ||
             host_resource_matches(name, "fontList")))
            fprintf(stderr,
                "libirixhostabi: font query class=%s resource=%s native=%d value=%08x\n",
                widget->core.widget_class &&
                    widget->core.widget_class->core_class.class_name
                    ? widget->core.widget_class->core_class.class_name : "?",
                name, native_found, *(uint32_t *)destination);
        if (widget->core.widget_class &&
            !strcmp(widget->core.widget_class->core_class.class_name,
                    "XfeCascade"))
            fprintf(stderr, "libirixhostabi: cascade get resource=%p sub=%p eq=%d native=%d value=%08x\n",
                    (void *)name, (void *)XmNsubMenuId,
                    host_resource_matches(name, XmNsubMenuId), native_found,
                    native_found ? *(uint32_t *)destination : 0u);
        if (getenv("IRIX_HOST_ABI_TRACE_WIDGETS") && trace_count++ < 64u)
            fprintf(stderr,
                "libirixhostabi: XtVaGetValues widget=%p resource=%p(%s) destination=%p screen=%p matches={visual:%d colormap:%d screen:%d depth:%d}\n",
                (void *)widget, (void *)name, name, destination,
                (void *)handle->host_screen,
                name == XtNvisual || host_resource_matches(name, "visual"),
                name == XtNcolormap || host_resource_matches(name, "colormap"),
                name == XtNscreen,
                name == XtNdepth || host_resource_matches(name, "depth"));
        if (!destination)
            continue;
        /* The application may pass Xt/Xm resource pointers from the
         * translated data image.  Their bytes are not necessarily readable
         * as host strings, even when pointer identity matches the exported
         * Xt/Xm token.  In particular, strcmp on the translated XtStrings
         * table can make every resource compare equal to the empty string.
         */
        if (host_resource_matches(name, "userData")) {
            memcpy(destination, &handle->host_user_data, sizeof(XtPointer));
        } else if (name == XtNvisual || host_resource_matches(name, "visual")) {
            Visual *visual = handle->host_screen
                ? DefaultVisualOfScreen(handle->host_screen) : NULL;
            memcpy(destination, &visual, sizeof(visual));
        } else if (name == XtNcolormap || host_resource_matches(name, "colormap")) {
            Colormap colormap = handle->host_screen
                ? DefaultColormapOfScreen(handle->host_screen) : None;
            memcpy(destination, &colormap, sizeof(colormap));
        } else if (name == XtNscreen) {
            Screen *screen = handle->host_screen;
            memcpy(destination, &screen, sizeof(screen));
        } else if (name == XtNwidth || host_resource_matches(name, "width")) {
            Dimension width = widget->core.width;
            memcpy(destination, &width, sizeof(width));
        } else if (name == XtNheight || host_resource_matches(name, "height")) {
            Dimension height = widget->core.height;
            memcpy(destination, &height, sizeof(height));
        } else if (host_resource_matches(name, "modifyVerifyCallback")) {
            static XtCallbackRec empty_callback_list[] = {{NULL, NULL}};
            XtCallbackList callbacks = empty_callback_list;
            memcpy(destination, &callbacks, sizeof(callbacks));
        } else if (name == XtNdepth || host_resource_matches(name, "depth")) {
            Cardinal depth = handle->host_screen
                ? (Cardinal)DefaultDepthOfScreen(handle->host_screen) : 0u;
            if (getenv("IRIX_HOST_ABI_TRACE_WIDGETS") && trace_count < 64u)
                fprintf(stderr,
                    "libirixhostabi: XtVaGetValues depth=%u root-depth=%d\n",
                    (unsigned)depth,
                    handle->host_screen ? handle->host_screen->root_depth : -1);
            memcpy(destination, &depth, sizeof(depth));
        } else if (name == XtNchildren || host_resource_matches(name, "children")) {
            Widget *children = handle->host_children;
            memcpy(destination, &children, sizeof(children));
        } else if (host_resource_matches(name, "numChildren")) {
            Cardinal child_count = handle->host_child_count;
            memcpy(destination, &child_count, sizeof(child_count));
        } else if (context_default(&context) == 0) {
            uint32_t guest_value = 0;
            int status = host_xt_get_scalar(context, handle, name,
                                              &guest_value);
            if (getenv("IRIX_HOST_ABI_TRACE_WIDGETS") &&
                font_resource)
                fprintf(stderr,
                    "libirixhostabi: XtVaGetValues font resource=%s guest-widget=%08x status=%d value=%08x native=%d\n",
                    name, handle->guest_address, status, guest_value,
                    native_found);
            if (getenv("IRIX_HOST_ABI_TRACE_WIDGETS") &&
                host_resource_matches(name, "cancelButton"))
                fprintf(stderr,
                    "libirixhostabi: cancelButton query guest-widget=%08x status=%d value=%08x\n",
                    handle->guest_address, status, guest_value);
            /* FontList values belong to the translated IRIX Xt/Motif
             * address space. Reading a similarly named host resource copies
             * a host XmFontList pointer into code that passes it to guest
             * library functions. */
            if (native_found && !font_resource) {
                /* Resource is owned by a native widget subclass. */
            } else if (!status && (host_resource_matches(name, XmNsubMenuId) ||
                host_resource_matches(name, "subMenuId") ||
                host_resource_matches(name, "cancelButton") ||
                host_resource_matches(name, "helpButton") ||
                host_resource_matches(name, "okButton") ||
                host_resource_matches(name, "defaultButton") ||
                host_resource_matches(name, "workWindow") ||
                host_resource_matches(name, "clipWindow") ||
                host_resource_matches(name, "verticalScrollBar") ||
                host_resource_matches(name, "horizontalScrollBar"))) {
                IrixHostOpaqueHandle *child;
                Widget host_child = NULL;
                for (child = g_host_opaque_handles; child; child = child->next)
                    if (child->alive && child->kind == IRIX_HOST_HANDLE_WIDGET &&
                        child->guest_address == guest_value) {
                        host_child = child->widget_object;
                        break;
                    }
                /* Some translated Motif constructors create their internal
                 * children without passing through a host-side constructor
                 * wrapper.  Preserve those child references as opaque
                 * widget handles so callers can safely manage them. */
                if (!host_child && guest_value) {
                    IrixHostOpaqueHandle *internal_child = host_handle_create(
                        IRIX_HOST_HANDLE_WIDGET, guest_value,
                        handle->guest_address, handle->app_guest_address);
                    if (internal_child) {
                        host_child = internal_child->widget_object;
                        internal_child->widget_rec.core.name = strdup(name);
                    }
                }
                memcpy(destination, &host_child, sizeof(host_child));
                if (!host_child && getenv("IRIX_HOST_ABI_TRACE_WIDGETS"))
                    fprintf(stderr,
                        "libirixhostabi: XtVaGetValues unresolved child resource=%s guest=%08x\n",
                        name, guest_value);
            } else if (!status) {
                uintptr_t scalar = guest_value;
                memcpy(destination, &scalar, sizeof(uint32_t));
            } else {
                ++unsupported;
                if (getenv("IRIX_HOST_ABI_TRACE_WIDGETS"))
                    fprintf(stderr,
                        "libirixhostabi: XtVaGetValues skipped resource=%s widget=%08x status=%d native=%d\n",
                        name, handle->guest_address, status, native_found);
            }
        } else {
            ++unsupported;
        }
    }
    va_end(ap);
    if (unsupported && !reported) {
        fprintf(stderr,
            "libirixhostabi: XtVaGetValues supplied host screen defaults and skipped %u unsupported resources\n",
            unsupported);
        reported = 1;
    }
    pthread_mutex_unlock(&g_host_handle_mutex);
}

static const char *host_scalar_resource_name(const char *name)
{
    struct ScalarName { const char *token; const char *text; };
    const struct ScalarName known[] = {
        { XtNx, "x" }, { XtNy, "y" }, { XtNwidth, "width" },
        { XtNheight, "height" }, { XtNborderWidth, "borderWidth" },
        { XtNbackground, "background" }, { XmNforeground, "foreground" },
        { XtNcolormap, "colormap" }, { XtNdepth, "depth" },
        { XtNsensitive, "sensitive" }, { XtNmappedWhenManaged, "mappedWhenManaged" },
        { XmNlabelPixmap, "labelPixmap" },
        { XmNlabelInsensitivePixmap, "labelInsensitivePixmap" },
        { XmNtopShadowPixmap, "topShadowPixmap" },
        { XmNbottomShadowPixmap, "bottomShadowPixmap" }
    };
    static const char *const custom[] = {
        "pixmap", "pixmapMask", "raisedPixmap", "raisedPixmapMask",
        "dragInitiatorProtocolStyle", "dragReceiverProtocolStyle",
        "defaultFontList", "buttonFontList", "fontList",
        "subMenuId", "cancelButton", "helpButton", "okButton",
        "defaultButton", "workWindow", "clipWindow",
        "verticalScrollBar", "horizontalScrollBar"
    };
    size_t i;
    if (!name) return NULL;
    for (i = 0; i < sizeof(known) / sizeof(known[0]); ++i)
        if (name == known[i].token ||
            host_resource_matches(name, known[i].text)) return known[i].text;
    for (i = 0; i < sizeof(known) / sizeof(known[0]); ++i)
        if (!strcmp(name, known[i].text)) return known[i].text;
    for (i = 0; i < sizeof(custom) / sizeof(custom[0]); ++i)
        if (host_resource_matches(name, custom[i])) return custom[i];
    return NULL;
}

void XtSetValues(Widget widget, ArgList args, Cardinal count)
{
    IrixHostAbiContext *context;
    IrixHostOpaqueHandle *handle;
    IrixN32ArgRec records[256];
    const char *names[256];
    IrixGuestAddr mark, guest_args;
    uint32_t call_args[3];
    Cardinal i, used = 0;
    if ((count && !args) || count > 256u || !context_default(&context)) return;
    pthread_mutex_lock(&g_host_handle_mutex);
    handle = host_handle_find(widget, IRIX_HOST_HANDLE_WIDGET);
    if (!handle) goto unlock;
    mark = context_arena_mark(context);
    for (i = 0; i < count; ++i) {
        const char *name;
        uint32_t guest_value = (uint32_t)args[i].value;
        int typed = 0;
        if (host_resource_matches(args[i].name, "userData")) {
            handle->host_user_data = (XtPointer)(uintptr_t)args[i].value;
            continue;
        }
        /* Callback lists have no native-to-guest function bridge yet. */
        if (host_resource_matches(args[i].name, "modifyVerifyCallback"))
            continue;
        if (host_resource_matches(args[i].name, "labelString")) {
            IrixHostXmStringHandle *string;
            name = "labelString";
            typed = 1;
            for (string = g_host_xmstrings; string; string = string->next)
                if ((uintptr_t)string == (uintptr_t)args[i].value) break;
            if (args[i].value && !string) {
                static unsigned reported_unknown_xmstring;
                if (reported_unknown_xmstring++ < 16u)
                    fprintf(stderr, "libirixhostabi: XtSetValues skipped unknown native XmString resource=%s\n",
                            args[i].name ? args[i].name : "(null)");
                continue;
            }
            guest_value = string ? string->guest_address : 0;
        } else {
            name = host_scalar_resource_name(args[i].name);
        }
        if (!name || (!typed && (uint64_t)args[i].value > UINT32_MAX &&
            !((!strcmp(name, "x") || !strcmp(name, "y")) &&
              (intptr_t)args[i].value >= INT16_MIN &&
              (intptr_t)args[i].value <= INT16_MAX))) {
            static unsigned reported_unconverted;
            if (reported_unconverted++ < 64u)
                fprintf(stderr, "libirixhostabi: XtSetValues skipped unconverted resource %s value=%p\n",
                        args[i].name ? args[i].name : "(null)",
                        (void *)args[i].value);
            continue;
        }
        if (irix_host_abi_context_copy_string(context, name,
                                              &records[used].name) != 0)
            goto done;
        names[used] = name;
        records[used++].value = guest_value;
    }
    if (!used) goto done;
    if (irix_host_abi_context_copy_bytes(context, records,
            used * sizeof(*records), &guest_args) != 0) goto done;
    for (i = 0; i < used; ++i) {
        irix_guest_write32be(&context->runtime, guest_args + i * 8u, records[i].name);
        irix_guest_write32be(&context->runtime, guest_args + i * 8u + 4u, records[i].value);
    }
    call_args[0] = handle->guest_address; call_args[1] = guest_args; call_args[2] = used;
    if (irix_host_abi_context_call(context, "XtSetValues", call_args, 3u, NULL) == 0) {
        CorePart *core = &host_widget_rec(handle)->core;
        for (i = 0; i < used; ++i) {
            uint32_t value = records[i].value;
            if (!strcmp(names[i], "width")) core->width = value;
            else if (!strcmp(names[i], "height")) core->height = value;
            else if (!strcmp(names[i], "x")) core->x = (int32_t)value;
            else if (!strcmp(names[i], "y")) core->y = (int32_t)value;
            else if (!strcmp(names[i], "borderWidth")) core->border_width = value;
            else if (!strcmp(names[i], "background")) core->background_pixel = value;
            else if (!strcmp(names[i], "colormap")) core->colormap = value;
            else if (!strcmp(names[i], "depth")) core->depth = value;
            else if (!strcmp(names[i], "sensitive")) core->sensitive = !!value;
            else if (!strcmp(names[i], "mappedWhenManaged")) core->mapped_when_managed = !!value;
        }
    }
done:
    context_arena_rewind(context, mark);
unlock:
    pthread_mutex_unlock(&g_host_handle_mutex);
}

void XtVaSetValues(Widget widget, ...)
{
    Arg args[256];
    Cardinal count = 0;
    const char *name;
    va_list ap;
    va_start(ap, widget);
    while ((name = va_arg(ap, const char *)) != NULL) {
        if (count == 256u) {
            va_end(ap);
            return;
        }
        args[count].name = (String)name;
        args[count++].value = va_arg(ap, XtArgVal);
    }
    va_end(ap);
    XtSetValues(widget, args, count);
}

#include "IrixHostAbiDropSite.inc"

Widget XmCreateLabel(Widget parent, char *name, ArgList args,
                     Cardinal arg_count)
{
    IrixHostAbiContext *context;
    IrixHostOpaqueHandle *parent_handle, *widget_handle;
    IrixGuestAddr arena_mark, guest_name;
    uint32_t call_args[4], guest_widget = 0;
    int status;
    host_prepare_xt_classes();
    if (!name || arg_count != 0 || (args && arg_count != 0) ||
        !context_default(&context))
        return NULL;
    pthread_mutex_lock(&g_host_handle_mutex);
    parent_handle = host_handle_find(parent, IRIX_HOST_HANDLE_WIDGET);
    if (!parent_handle) {
        pthread_mutex_unlock(&g_host_handle_mutex);
        return NULL;
    }
    arena_mark = context_arena_mark(context);
    status = irix_host_abi_context_copy_string(context, name, &guest_name);
    if (status == 0) {
        call_args[0] = parent_handle->guest_address;
        call_args[1] = guest_name;
        call_args[2] = 0;
        call_args[3] = 0;
        status = irix_host_abi_context_call(context, "XmCreateLabel",
                                            call_args, 4u, &guest_widget);
    }
    context_arena_rewind(context, arena_mark);
    widget_handle = status == 0 && guest_widget
        ? host_handle_create(IRIX_HOST_HANDLE_WIDGET, guest_widget,
                             parent_handle->guest_address,
                             parent_handle->app_guest_address)
        : NULL;
    if (widget_handle) {
        (void)host_widget_promote_class(widget_handle,
            host_motif_class_from_facade("xmLabelWidgetClass"));
        host_widget_rec(widget_handle)->core.name = strdup(name);
    }
    pthread_mutex_unlock(&g_host_handle_mutex);
    return host_widget_value(widget_handle);
}

/*
 * Netscape passes a host-ABI ArgList to Motif convenience constructors.  The
 * popup-menu path currently needs only scalar resources plus the native
 * Visual pointer added by XFE_DisplayFactory, so translate that pointer back
 * to the canonical guest Visual shadow before entering N32 libXm.
 */
static IrixGuestAddr host_guest_visual(IrixHostAbiContext *context,
                                       IrixHostOpaqueHandle *widget_handle,
                                       uintptr_t host_visual)
{
    IrixGuestAddr screen, depths, root_visual = 0;
    uint32_t requested_visual_id = 0;
    XVisualInfo template_info;
    XVisualInfo *visuals = NULL;
    int visual_count = 0, i;
    uint32_t depth_count = 0, depth_index;
    if (!context || !widget_handle ||
        !widget_handle->guest_screen_address)
        return 0;
    screen = widget_handle->guest_screen_address;
    if (irix_host_abi_context_read32be(context, screen + 28u,
                                       &depth_count) != 0 ||
        irix_host_abi_context_read32be(context, screen + 32u,
                                       &depths) != 0 ||
        irix_host_abi_context_read32be(context, screen + 40u,
                                       &root_visual) != 0 ||
        !depths || depth_count > 256u)
        return 0;

    /*
     * Do not dereference the resource value: some native XFE objects carry a
     * translated Visual token here.  Match it against pointers returned by
     * Xlib for this display, then resolve only the matched visual ID.
     */
    if (widget_handle->host_display) {
        memset(&template_info, 0, sizeof(template_info));
        template_info.screen = widget_handle->host_screen
            ? XScreenNumberOfScreen(widget_handle->host_screen)
            : DefaultScreen(widget_handle->host_display);
        visuals = XGetVisualInfo(widget_handle->host_display,
            VisualScreenMask, &template_info, &visual_count);
        for (i = 0; visuals && i < visual_count; ++i) {
            if ((uintptr_t)visuals[i].visual == host_visual) {
                requested_visual_id = (uint32_t)visuals[i].visualid;
                break;
            }
        }
        if (visuals)
            XFree(visuals);
    }
    if (!requested_visual_id && (!root_visual ||
        irix_host_abi_context_read32be(context, root_visual + 4u,
                                       &requested_visual_id) != 0))
        return 0;
    for (depth_index = 0; depth_index < depth_count; ++depth_index) {
        IrixGuestAddr depth = depths + depth_index * 12u;
        IrixGuestAddr visuals = 0;
        uint32_t visual_count = 0, visual_index;
        if (irix_host_abi_context_read32be(context, depth + 4u,
                                           &visual_count) != 0 ||
            irix_host_abi_context_read32be(context, depth + 8u,
                                           &visuals) != 0 ||
            !visuals || visual_count > 4096u)
            continue;
        for (visual_index = 0; visual_index < visual_count; ++visual_index) {
            IrixGuestAddr guest_visual = visuals + visual_index * 32u;
            uint32_t guest_visual_id = 0;
            if (irix_host_abi_context_read32be(context, guest_visual + 4u,
                                               &guest_visual_id) == 0 &&
                guest_visual_id == requested_visual_id)
                return guest_visual;
        }
    }
    return 0;
}

static Widget host_xm_create_widget(const char *constructor, Widget parent,
                                    char *name, ArgList args,
                                    Cardinal arg_count)
{
    IrixHostAbiContext *context;
    IrixHostOpaqueHandle *parent_handle, *widget_handle;
    IrixGuestAddr arena_mark, guest_name, guest_args = 0;
    IrixN32ArgRec *guest_records = NULL;
    uint32_t call_args[4], guest_widget = 0;
    Cardinal i;
    int status;
    static int reported;
    const char *class_symbol = NULL;

    host_prepare_xt_classes();
    if (!strcmp(constructor, "XmCreatePopupMenu") ||
        !strcmp(constructor, "XmCreateMenuBar") ||
        !strcmp(constructor, "XmCreatePulldownMenu"))
        class_symbol = "xmRowColumnWidgetClass";
    else if (!strcmp(constructor, "XmCreateForm"))
        class_symbol = "xmFormWidgetClass";
    else if (!strcmp(constructor, "XmCreateFormDialog"))
        class_symbol = "xmFormWidgetClass";
    else if (!strcmp(constructor, "XmCreatePanedWindow"))
        class_symbol = "xmPanedWindowWidgetClass";
    else if (!strcmp(constructor, "XmCreateDialogShell"))
        class_symbol = "xmDialogShellWidgetClass";
    else if (!strcmp(constructor, "XmCreateScrollBar"))
        class_symbol = "xmScrollBarWidgetClass";
    else if (!strcmp(constructor, "XmCreateScrolledList"))
        class_symbol = "xmListWidgetClass";
    else if (!strcmp(constructor, "XmCreateSeparator"))
        class_symbol = "xmSeparatorWidgetClass";
    else if (!strcmp(constructor, "XmCreateMessageBox"))
        class_symbol = "xmMessageBoxWidgetClass";
    else if (!strcmp(constructor, "XmCreateInformationDialog") ||
             !strcmp(constructor, "XmCreateMessageDialog") ||
             !strcmp(constructor, "XmCreatePromptDialog") ||
             !strcmp(constructor, "XmCreateQuestionDialog") ||
             !strcmp(constructor, "XmCreateTemplateDialog"))
        class_symbol = "xmMessageBoxWidgetClass";
    else if (!strcmp(constructor, "XmCreateDrawingArea"))
        class_symbol = "xmDrawingAreaWidgetClass";
    else if (!strcmp(constructor, "XmCreateText"))
        class_symbol = "xmTextWidgetClass";
    else if (!strcmp(constructor, "XmCreateTextField"))
        class_symbol = "xmTextFieldWidgetClass";
    else if (!strcmp(constructor, "XmCreateArrowButton") ||
             !strcmp(constructor, "XmCreateArrowButtonGadget"))
        class_symbol = "xmArrowButtonWidgetClass";
    else if (!strcmp(constructor, "XmCreateCascadeButton") ||
             !strcmp(constructor, "XmCreateCascadeButtonGadget"))
        class_symbol = "xmCascadeButtonWidgetClass";
    else if (!strcmp(constructor, "XmCreateDrawnButton"))
        class_symbol = "xmDrawnButtonWidgetClass";
    else if (!strcmp(constructor, "XmCreatePushButton") ||
             !strcmp(constructor, "XmCreatePushButtonGadget"))
        class_symbol = "xmPushButtonWidgetClass";
    else if (!strcmp(constructor, "XmCreateToggleButton") ||
             !strcmp(constructor, "XmCreateToggleButtonGadget"))
        class_symbol = "xmToggleButtonWidgetClass";
    else if (!strcmp(constructor, "XmCreateFrame"))
        class_symbol = "xmFrameWidgetClass";
    else if (!strcmp(constructor, "XmCreateFileSelectionBox"))
        class_symbol = "xmFileSelectionBoxWidgetClass";
    else if (!strcmp(constructor, "XmCreateOptionMenu") ||
             !strcmp(constructor, "XmCreateRadioBox"))
        class_symbol = "xmRowColumnWidgetClass";
    else if (!strcmp(constructor, "DtCreateComboBox"))
        class_symbol = "dtComboBoxWidgetClass";
    if (!name || (arg_count && !args) || arg_count > IRIX_HOST_ABI_MAX_ARGS ||
        !context_default(&context))
        return NULL;
    pthread_mutex_lock(&g_host_handle_mutex);
    parent_handle = host_handle_find(parent, IRIX_HOST_HANDLE_WIDGET);
    if (!parent_handle) {
        if (getenv("IRIX_HOST_ABI_TRACE_WIDGETS") &&
            !strcmp(constructor, "XmCreateDialogShell"))
            fprintf(stderr,
                "libirixhostabi: %s parent=%p has no opaque handle\n",
                constructor, (void *)parent);
        pthread_mutex_unlock(&g_host_handle_mutex);
        return NULL;
    }
    if (getenv("IRIX_HOST_ABI_TRACE_WIDGETS") &&
        !strcmp(constructor, "XmCreateDialogShell"))
        fprintf(stderr,
            "libirixhostabi: %s parent=%p handle=%p guest-parent=%08x app=%08x\n",
            constructor, (void *)parent, (void *)parent_handle,
            parent_handle->guest_address, parent_handle->app_guest_address);
    if (arg_count) {
        guest_records = (IrixN32ArgRec *)calloc(arg_count,
                                                sizeof(*guest_records));
        if (!guest_records) {
            pthread_mutex_unlock(&g_host_handle_mutex);
            return NULL;
        }
    }
    arena_mark = context_arena_mark(context);
    status = irix_host_abi_context_copy_string(context, name, &guest_name);
    for (i = 0; status == 0 && i < arg_count; ++i) {
        const char *resource = args[i].name;
        uintptr_t value = (uintptr_t)args[i].value;
        if (!resource || irix_host_abi_context_copy_string(context, resource,
                (IrixGuestAddr *)&guest_records[i].name) != 0) {
            status = -1;
            break;
        }
        if (host_resource_matches(resource, XmNvisual) ||
            host_resource_matches(resource, XtNvisual)) {
            IrixGuestAddr visual = host_guest_visual(context, parent_handle,
                                                      value);
            if (!visual) {
                status = -1;
                break;
            }
            value = visual;
        }
        if (host_resource_matches(resource, "messageString") ||
            host_resource_matches(resource, "labelString")) {
            IrixHostXmStringHandle *string;
            for (string = g_host_xmstrings; string; string = string->next)
                if ((uintptr_t)string == value) break;
            if (value && !string) { status = -1; break; }
            value = string ? string->guest_address : 0;
        }
        guest_records[i].value = (uint32_t)value;
    }
    if (status == 0 && arg_count) {
        if (irix_host_abi_context_copy_bytes(context, guest_records,
                arg_count * sizeof(*guest_records), &guest_args) != 0)
            status = -1;
        for (i = 0; status == 0 && i < arg_count; ++i) {
            if (irix_host_abi_context_write32be(context,
                    guest_args + (uint32_t)(i * sizeof(*guest_records)),
                    guest_records[i].name) != 0 ||
                irix_host_abi_context_write32be(context,
                    guest_args + (uint32_t)(i * sizeof(*guest_records) + 4u),
                    guest_records[i].value) != 0)
                status = -1;
        }
    }
    if (status == 0) {
        call_args[0] = parent_handle->guest_address;
        call_args[1] = guest_name;
        call_args[2] = guest_args;
        call_args[3] = (uint32_t)arg_count;
        status = irix_host_abi_context_call(context, constructor,
                                            call_args, 4u, &guest_widget);
    }
    if (getenv("IRIX_HOST_ABI_TRACE_WIDGETS") &&
        !strcmp(constructor, "XmCreateDialogShell"))
        fprintf(stderr,
            "libirixhostabi: %s status=%d guest-widget=%08x trapped=%d pc=%08x fault=%08x\n",
            constructor, status, guest_widget, context->runtime.trapped,
            context->runtime.current_pc, context->runtime.fault_addr);
    context_arena_rewind(context, arena_mark);
    widget_handle = status == 0 && guest_widget
        ? host_handle_create(IRIX_HOST_HANDLE_WIDGET, guest_widget,
                             parent_handle->guest_address,
                             parent_handle->app_guest_address)
        : NULL;
    if (widget_handle) {
        if (class_symbol) {
            WidgetClass widget_class = host_motif_class_from_facade(class_symbol);
            if (!widget_class) {
                WidgetClass *class_slot = (WidgetClass *)dlsym(
                    RTLD_DEFAULT, class_symbol);
                if (class_slot) widget_class = *class_slot;
            }
            if (widget_class)
                (void)host_widget_promote_class(widget_handle, widget_class);
        }
        host_widget_rec(widget_handle)->core.name = strdup(name);
    }
    if (!reported) {
        fprintf(stderr,
            "libirixhostabi: %s translated %u N32 Arg records (status=%d guest-widget=%08x)\n",
            constructor,
            (unsigned)arg_count, status, guest_widget);
        reported = 1;
    }
    free(guest_records);
    pthread_mutex_unlock(&g_host_handle_mutex);
    return host_widget_value(widget_handle);
}

Widget XmCreatePopupMenu(Widget parent, char *name, ArgList args,
                         Cardinal arg_count)
{
    return host_xm_create_widget("XmCreatePopupMenu", parent, name, args,
                                 arg_count);
}

Widget XmCreateMenuBar(Widget parent, char *name, ArgList args,
                       Cardinal arg_count)
{
    return host_xm_create_widget("XmCreateMenuBar", parent, name, args,
                                 arg_count);
}

Widget XmCreatePulldownMenu(Widget parent, char *name, ArgList args,
                            Cardinal arg_count)
{
    return host_xm_create_widget("XmCreatePulldownMenu", parent, name, args,
                                 arg_count);
}

Widget XmCreateMessageBox(Widget parent, char *name, ArgList args,
                          Cardinal arg_count)
{
    Widget result = host_xm_create_widget("XmCreateMessageBox", parent, name,
                                          args, arg_count);
    if (getenv("IRIX_HOST_ABI_TRACE_WIDGETS"))
        fprintf(stderr,
            "libirixhostabi: XmCreateMessageBox parent=%p name=%s args=%u result=%p\n",
            (void *)parent, name ? name : "(null)", (unsigned)arg_count,
            (void *)result);
    return result;
}

Widget XmCreateDialogShell(Widget parent, char *name, ArgList args,
                           Cardinal arg_count)
{
    IrixHostOpaqueHandle *handle;
    Widget shell;
    host_prepare_xt_classes();
    shell = XtCreatePopupShell(name, g_host_shell_widget_class, parent,
                               args, arg_count);
    if (!shell)
        return NULL;
    pthread_mutex_lock(&g_host_handle_mutex);
    handle = host_handle_find(shell, IRIX_HOST_HANDLE_WIDGET);
    if (handle) {
        host_prepare_xt_classes();
        (void)host_widget_promote_class(handle,
            host_motif_class_from_facade("xmDialogShellWidgetClass"));
        if (!host_widget_rec(handle)->core.name)
            host_widget_rec(handle)->core.name = strdup(name);
    }
    pthread_mutex_unlock(&g_host_handle_mutex);
    return shell;
}

Widget XmCreateForm(Widget parent, char *name, ArgList args,
                    Cardinal arg_count)
{
    return host_xm_create_widget("XmCreateForm", parent, name, args,
                                 arg_count);
}

Widget XmCreatePanedWindow(Widget parent, char *name, ArgList args,
                           Cardinal arg_count)
{
    return host_xm_create_widget("XmCreatePanedWindow", parent, name, args,
                                 arg_count);
}

Widget XmCreateScrollBar(Widget parent, char *name, ArgList args,
                         Cardinal arg_count)
{
    return host_xm_create_widget("XmCreateScrollBar", parent, name, args,
                                 arg_count);
}

Widget XmCreateScrolledList(Widget parent, char *name, ArgList args,
                            Cardinal arg_count)
{
    return host_xm_create_widget("XmCreateScrolledList", parent, name, args,
                                 arg_count);
}

Widget XmCreateSeparator(Widget parent, char *name, ArgList args,
                         Cardinal arg_count)
{
    return host_xm_create_widget("XmCreateSeparator", parent, name, args,
                                 arg_count);
}

Widget XmCreateDrawingArea(Widget parent, char *name, ArgList args,
                           Cardinal arg_count)
{
    return host_xm_create_widget("XmCreateDrawingArea", parent, name, args,
                                 arg_count);
}

Widget XmCreateText(Widget parent, char *name, ArgList args,
                    Cardinal arg_count)
{
    return host_xm_create_widget("XmCreateText", parent, name, args,
                                 arg_count);
}

Widget XmCreateTextField(Widget parent, char *name, ArgList args,
                         Cardinal arg_count)
{
    return host_xm_create_widget("XmCreateTextField", parent, name, args,
                                 arg_count);
}

#define IRIX_HOST_XM_CREATE_WRAPPER(name) \
    Widget name(Widget parent, char *widget_name, ArgList args, \
                Cardinal arg_count) \
    { \
        return host_xm_create_widget(#name, parent, widget_name, args, \
                                     arg_count); \
    }

IRIX_HOST_XM_CREATE_WRAPPER(XmCreateArrowButton)
IRIX_HOST_XM_CREATE_WRAPPER(XmCreateArrowButtonGadget)
IRIX_HOST_XM_CREATE_WRAPPER(XmCreateCascadeButton)
IRIX_HOST_XM_CREATE_WRAPPER(XmCreateCascadeButtonGadget)
IRIX_HOST_XM_CREATE_WRAPPER(XmCreateDrawnButton)
IRIX_HOST_XM_CREATE_WRAPPER(XmCreateFileSelectionBox)
IRIX_HOST_XM_CREATE_WRAPPER(XmCreateFormDialog)
IRIX_HOST_XM_CREATE_WRAPPER(XmCreateFrame)
IRIX_HOST_XM_CREATE_WRAPPER(XmCreateInformationDialog)
IRIX_HOST_XM_CREATE_WRAPPER(XmCreateLabelGadget)
IRIX_HOST_XM_CREATE_WRAPPER(XmCreateMessageDialog)
IRIX_HOST_XM_CREATE_WRAPPER(XmCreateOptionMenu)
IRIX_HOST_XM_CREATE_WRAPPER(XmCreatePromptDialog)
IRIX_HOST_XM_CREATE_WRAPPER(XmCreatePushButton)
IRIX_HOST_XM_CREATE_WRAPPER(XmCreatePushButtonGadget)
IRIX_HOST_XM_CREATE_WRAPPER(XmCreateQuestionDialog)
IRIX_HOST_XM_CREATE_WRAPPER(XmCreateRadioBox)
IRIX_HOST_XM_CREATE_WRAPPER(XmCreateRowColumn)
IRIX_HOST_XM_CREATE_WRAPPER(XmCreateScale)
IRIX_HOST_XM_CREATE_WRAPPER(XmCreateScrolledText)
IRIX_HOST_XM_CREATE_WRAPPER(XmCreateScrolledWindow)
IRIX_HOST_XM_CREATE_WRAPPER(XmCreateSelectionDialog)
IRIX_HOST_XM_CREATE_WRAPPER(XmCreateSeparatorGadget)
IRIX_HOST_XM_CREATE_WRAPPER(XmCreateTemplateDialog)
IRIX_HOST_XM_CREATE_WRAPPER(XmCreateToggleButton)
IRIX_HOST_XM_CREATE_WRAPPER(XmCreateToggleButtonGadget)

#undef IRIX_HOST_XM_CREATE_WRAPPER

Widget DtCreateComboBox(Widget parent, char *name, Arg *args, int arg_count)
{
    if (arg_count < 0) return NULL;
    return host_xm_create_widget("DtCreateComboBox", parent, name, args,
                                 (Cardinal)arg_count);
}

void XmScrolledWindowSetAreas(Widget scrolled_window, Widget horizontal,
                              Widget vertical, Widget work_window)
{
    IrixHostAbiContext *context;
    IrixHostOpaqueHandle *handles[4];
    uint32_t guest_args[4];
    Widget host_widgets[4];
    unsigned i;
    if (!context_default(&context)) return;
    host_widgets[0] = scrolled_window;
    host_widgets[1] = horizontal;
    host_widgets[2] = vertical;
    host_widgets[3] = work_window;
    pthread_mutex_lock(&g_host_handle_mutex);
    for (i = 0; i < 4u; ++i) {
        handles[i] = host_widgets[i]
            ? host_handle_find(host_widgets[i], IRIX_HOST_HANDLE_WIDGET)
            : NULL;
        guest_args[i] = handles[i] ? handles[i]->guest_address : 0u;
    }
    if (handles[0])
        (void)irix_host_abi_context_call(context, "XmScrolledWindowSetAreas",
                                         guest_args, 4u, NULL);
    pthread_mutex_unlock(&g_host_handle_mutex);
}

XtGeometryResult _XmMakeGeometryRequest(Widget widget,
                                         XtWidgetGeometry *request)
{
    IrixHostAbiContext *context;
    IrixHostOpaqueHandle *handle, *sibling;
    IrixGuestAddr mark, guest_request = 0;
    uint8_t raw[24] = { 0 };
    uint32_t guest_args[2], guest_result = XtGeometryNo;
    int status;
    if (!request || !context_default(&context)) return XtGeometryNo;
    pthread_mutex_lock(&g_host_handle_mutex);
    handle = host_handle_find(widget, IRIX_HOST_HANDLE_WIDGET);
    sibling = request->sibling
        ? host_handle_find(request->sibling, IRIX_HOST_HANDLE_WIDGET) : NULL;
    if (!handle || (request->sibling && !sibling)) {
        pthread_mutex_unlock(&g_host_handle_mutex);
        return XtGeometryNo;
    }
    raw[0] = (uint8_t)(request->request_mode >> 24);
    raw[1] = (uint8_t)(request->request_mode >> 16);
    raw[2] = (uint8_t)(request->request_mode >> 8);
    raw[3] = (uint8_t)request->request_mode;
    raw[4] = (uint8_t)((uint16_t)request->x >> 8);
    raw[5] = (uint8_t)request->x;
    raw[6] = (uint8_t)((uint16_t)request->y >> 8);
    raw[7] = (uint8_t)request->y;
    raw[8] = (uint8_t)(request->width >> 8); raw[9] = (uint8_t)request->width;
    raw[10] = (uint8_t)(request->height >> 8); raw[11] = (uint8_t)request->height;
    raw[12] = (uint8_t)(request->border_width >> 8);
    raw[13] = (uint8_t)request->border_width;
    {
        uint32_t guest_sibling = sibling ? sibling->guest_address : 0u;
        raw[16] = (uint8_t)(guest_sibling >> 24);
        raw[17] = (uint8_t)(guest_sibling >> 16);
        raw[18] = (uint8_t)(guest_sibling >> 8);
        raw[19] = (uint8_t)guest_sibling;
    }
    raw[20] = (uint8_t)((uint32_t)request->stack_mode >> 24);
    raw[21] = (uint8_t)((uint32_t)request->stack_mode >> 16);
    raw[22] = (uint8_t)((uint32_t)request->stack_mode >> 8);
    raw[23] = (uint8_t)request->stack_mode;
    mark = context_arena_mark(context);
    status = irix_host_abi_context_copy_bytes(context, raw, sizeof(raw),
                                               &guest_request);
    if (!status) {
        guest_args[0] = handle->guest_address;
        guest_args[1] = guest_request;
        status = irix_host_abi_context_call(context, "_XmMakeGeometryRequest",
                                            guest_args, 2u, &guest_result);
    }
    if (!status) {
        pthread_mutex_lock(&context->mutex);
        {
            uint8_t *guest_raw = (uint8_t *)irix_guest_ptr(
                &context->runtime, guest_request, sizeof(raw));
            if (guest_raw) {
                request->request_mode =
                    ((uint32_t)guest_raw[0] << 24) |
                    ((uint32_t)guest_raw[1] << 16) |
                    ((uint32_t)guest_raw[2] << 8) | guest_raw[3];
                request->x = (Position)((guest_raw[4] << 8) | guest_raw[5]);
                request->y = (Position)((guest_raw[6] << 8) | guest_raw[7]);
                request->width = (Dimension)((guest_raw[8] << 8) | guest_raw[9]);
                request->height = (Dimension)((guest_raw[10] << 8) | guest_raw[11]);
                request->border_width = (Dimension)((guest_raw[12] << 8) | guest_raw[13]);
                request->stack_mode = (int)(((uint32_t)guest_raw[20] << 24) |
                    ((uint32_t)guest_raw[21] << 16) |
                    ((uint32_t)guest_raw[22] << 8) | guest_raw[23]);
            }
        }
        pthread_mutex_unlock(&context->mutex);
    }
    context_arena_rewind(context, mark);
    pthread_mutex_unlock(&g_host_handle_mutex);
    return status ? XtGeometryNo : (XtGeometryResult)guest_result;
}

void DtComboBoxAddItem(Widget combo, XmString item, int position,
                       Boolean unique)
{
    IrixHostAbiContext *context;
    IrixHostOpaqueHandle *combo_handle;
    IrixHostXmStringHandle *string_handle;
    uint32_t args[4];
    if (!context_default(&context)) return;
    pthread_mutex_lock(&g_host_handle_mutex);
    combo_handle = host_handle_find(combo, IRIX_HOST_HANDLE_WIDGET);
    string_handle = NULL;
    if (item) {
        for (string_handle = g_host_xmstrings; string_handle;
             string_handle = string_handle->next)
            if ((XmString)(uintptr_t)string_handle == item) break;
    }
    if (combo_handle && (!item || string_handle)) {
        args[0] = combo_handle->guest_address;
        args[1] = string_handle ? string_handle->guest_address : 0u;
        args[2] = (uint32_t)position;
        args[3] = unique ? 1u : 0u;
        (void)irix_host_abi_context_call(context, "DtComboBoxAddItem",
                                         args, 4u, NULL);
    } else if (combo_handle) {
        fprintf(stderr, "libirixhostabi: DtComboBoxAddItem skipped unknown XmString %p\n",
                (void *)item);
    }
    pthread_mutex_unlock(&g_host_handle_mutex);
}

void XtManageChildren(WidgetList children, Cardinal child_count)
{
    enum { HOST_ABI_MAX_MANAGED_CHILDREN = 4096 };
    IrixHostAbiContext *context;
    IrixGuestAddr arena_mark, guest_children;
    uint32_t *guest_addresses;
    uint32_t call_args[2];
    Cardinal i;
    int status = 0;

    if (!child_count)
        return;
    if (!children || child_count > HOST_ABI_MAX_MANAGED_CHILDREN ||
        !context_default(&context))
        return;
    guest_addresses = (uint32_t *)calloc((size_t)child_count,
                                         sizeof(*guest_addresses));
    if (!guest_addresses)
        return;

    pthread_mutex_lock(&g_host_handle_mutex);
    arena_mark = context_arena_mark(context);
    status = irix_host_abi_context_copy_bytes(context, guest_addresses,
        (size_t)child_count * sizeof(*guest_addresses), &guest_children);
    for (i = 0; status == 0 && i < child_count; ++i) {
        IrixHostOpaqueHandle *handle = host_handle_find(children[i],
                                                        IRIX_HOST_HANDLE_WIDGET);
        if (!handle) {
            status = -1;
            break;
        }
        guest_addresses[i] = handle->guest_address;
        status = irix_host_abi_context_write32be(context,
            guest_children + (uint32_t)(i * sizeof(*guest_addresses)),
            guest_addresses[i]);
    }
    if (status == 0) {
        call_args[0] = guest_children;
        call_args[1] = (uint32_t)child_count;
        status = irix_host_abi_context_call(context, "XtManageChildren",
                                            call_args, 2u, NULL);
    }
    context_arena_rewind(context, arena_mark);
    pthread_mutex_unlock(&g_host_handle_mutex);
    free(guest_addresses);
}

void XtUnmanageChildren(WidgetList children, Cardinal child_count)
{
    Cardinal i;
    if (!children || child_count > 4096u)
        return;
    /* Route each element through the host ABI adapter so Netscape's native
     * Widget pointers are translated to their guest handles consistently. */
    for (i = 0; i < child_count; ++i)
        XtUnmanageChild(children[i]);
}

void XtManageChild(Widget child)
{
    IrixHostAbiContext *context;
    IrixHostOpaqueHandle *handle, *parent;
    uint32_t arg;
    if (!context_default(&context)) return;
    pthread_mutex_lock(&g_host_handle_mutex);
    handle = host_handle_find(child, IRIX_HOST_HANDLE_WIDGET);
    if (handle) {
        arg = handle->guest_address;
        if (!irix_host_abi_context_call(context, "XtManageChild", &arg, 1u,
                                        NULL)) {
            handle->managed = 1;
            parent = host_handle_find_guest(handle->parent_guest_address,
                                             IRIX_HOST_HANDLE_WIDGET);
            if (!host_native_insert_depth)
                host_native_composite_change_managed(parent);
        }
    }
    pthread_mutex_unlock(&g_host_handle_mutex);
}

void XtUnmanageChild(Widget child)
{
    IrixHostAbiContext *context;
    IrixHostOpaqueHandle *handle, *parent;
    uint32_t arg;
    if (!context_default(&context)) return;
    pthread_mutex_lock(&g_host_handle_mutex);
    handle = host_handle_find(child, IRIX_HOST_HANDLE_WIDGET);
    if (handle) {
        arg = handle->guest_address;
        if (!irix_host_abi_context_call(context, "XtUnmanageChild", &arg, 1u,
                                        NULL)) {
            handle->managed = 0;
            parent = host_handle_find_guest(handle->parent_guest_address,
                                             IRIX_HOST_HANDLE_WIDGET);
            host_native_composite_change_managed(parent);
        }
    }
    pthread_mutex_unlock(&g_host_handle_mutex);
}

void XtResizeWidget(Widget widget, Dimension width, Dimension height,
                    Dimension border_width)
{
    IrixHostAbiContext *context;
    IrixHostOpaqueHandle *handle;
    uint32_t args[4];
    if (!context_default(&context)) return;
    pthread_mutex_lock(&g_host_handle_mutex);
    handle = host_handle_find(widget, IRIX_HOST_HANDLE_WIDGET);
    if (handle) {
        args[0] = handle->guest_address;
        args[1] = width;
        args[2] = height;
        args[3] = border_width;
        if (!irix_host_abi_context_call(context, "XtResizeWidget", args, 4u,
                                        NULL)) {
            widget->core.width = width;
            widget->core.height = height;
            widget->core.border_width = border_width;
        }
    }
    pthread_mutex_unlock(&g_host_handle_mutex);
}

void XtMoveWidget(Widget widget, Position x, Position y)
{
    IrixHostAbiContext *context;
    IrixHostOpaqueHandle *handle;
    uint32_t args[3];
    if (!context_default(&context)) return;
    pthread_mutex_lock(&g_host_handle_mutex);
    handle = host_handle_find(widget, IRIX_HOST_HANDLE_WIDGET);
    if (handle) {
        args[0] = handle->guest_address;
        args[1] = (uint32_t)(int32_t)x;
        args[2] = (uint32_t)(int32_t)y;
        if (!irix_host_abi_context_call(context, "XtMoveWidget", args, 3u,
                                        NULL)) {
            widget->core.x = x;
            widget->core.y = y;
        }
    }
    pthread_mutex_unlock(&g_host_handle_mutex);
}

Boolean XtIsManaged(Widget widget)
{
    IrixHostAbiContext *context;
    IrixHostOpaqueHandle *handle;
    uint32_t result = 0;
    if (!context_default(&context)) return False;
    pthread_mutex_lock(&g_host_handle_mutex);
    handle = host_handle_find(widget, IRIX_HOST_HANDLE_WIDGET);
    if (handle) {
        (void)irix_host_abi_context_call(context, "XtIsManaged",
            &handle->guest_address, 1u, &result);
        handle->managed = !!result;
        if (getenv("IRIX_HOST_ABI_TRACE_WIDGETS"))
            fprintf(stderr, "host ABI XtIsManaged %s=%u\n",
                widget->core.name ? widget->core.name : "?", handle->managed);
    }
    pthread_mutex_unlock(&g_host_handle_mutex);
    return handle ? (Boolean)handle->managed : False;
}

#undef XtIsRealized
Boolean XtIsRealized(Widget widget)
{
    IrixHostAbiContext *context;
    IrixHostOpaqueHandle *handle;
    uint32_t result = 0;
    if (!context_default(&context)) return False;
    pthread_mutex_lock(&g_host_handle_mutex);
    handle = host_handle_find(widget, IRIX_HOST_HANDLE_WIDGET);
    if (handle) {
        (void)irix_host_abi_context_call(context, "XtIsRealized",
            &handle->guest_address, 1u, &result);
        handle->realized = !!result;
    }
    pthread_mutex_unlock(&g_host_handle_mutex);
    return handle ? (Boolean)handle->realized : False;
}

void XtRealizeWidget(Widget widget)
{
    IrixHostAbiContext *context;
    IrixHostOpaqueHandle *handle;
    uint32_t arg;
    if (!context_default(&context)) return;
    pthread_mutex_lock(&g_host_handle_mutex);
    handle = host_handle_find(widget, IRIX_HOST_HANDLE_WIDGET);
    if (handle) {
        arg = handle->guest_address;
        if (!irix_host_abi_context_call(context, "XtRealizeWidget", &arg, 1u,
                                        NULL)) {
            handle->realized = 1;
            host_sync_guest_widget_windows(context,
                                            handle->app_guest_address);
        }
    }
    pthread_mutex_unlock(&g_host_handle_mutex);
}

void XtPopup(Widget widget, XtGrabKind grab_kind)
{
    IrixHostAbiContext *context;
    IrixHostOpaqueHandle *handle;
    uint32_t args[2];
    if (!context_default(&context)) return;
    pthread_mutex_lock(&g_host_handle_mutex);
    handle = host_handle_find(widget, IRIX_HOST_HANDLE_WIDGET);
    if (handle) {
        args[0] = handle->guest_address;
        args[1] = (uint32_t)grab_kind;
        if (!irix_host_abi_context_call(context, "XtPopup", args, 2u, NULL)) {
            handle->realized = 1;
            handle->managed = 1;
        }
    }
    pthread_mutex_unlock(&g_host_handle_mutex);
}

void XtPopdown(Widget widget)
{
    IrixHostAbiContext *context;
    IrixHostOpaqueHandle *handle;
    uint32_t arg;
    if (!context_default(&context)) return;
    pthread_mutex_lock(&g_host_handle_mutex);
    handle = host_handle_find(widget, IRIX_HOST_HANDLE_WIDGET);
    if (handle) {
        arg = handle->guest_address;
        if (!irix_host_abi_context_call(context, "XtPopdown", &arg, 1u, NULL))
            handle->managed = 0;
    }
    pthread_mutex_unlock(&g_host_handle_mutex);
}

void XmAddProtocols(Widget shell, Atom property, Atom *protocols,
                   Cardinal num_protocols)
{
    IrixHostAbiContext *context;
    IrixHostOpaqueHandle *handle;
    IrixGuestAddr mark, guest_protocols;
    uint32_t args[4];
    Cardinal i;
    if (!context_default(&context) || (num_protocols && !protocols)) return;
    pthread_mutex_lock(&g_host_handle_mutex);
    handle = host_handle_find(shell, IRIX_HOST_HANDLE_WIDGET);
    if (!handle) goto done;
    mark = context_arena_mark(context);
    if (irix_host_abi_context_copy_bytes(context, protocols,
            (size_t)num_protocols * sizeof(uint32_t), &guest_protocols)) {
        context_arena_rewind(context, mark);
        goto done;
    }
    for (i = 0; i < num_protocols; ++i)
        irix_guest_write32be(&context->runtime, guest_protocols + i * 4u,
                             (uint32_t)protocols[i]);
    args[0] = handle->guest_address;
    args[1] = (uint32_t)property;
    args[2] = guest_protocols;
    args[3] = num_protocols;
    (void)irix_host_abi_context_call(context, "XmAddProtocols", args, 4, NULL);
    context_arena_rewind(context, mark);
done:
    pthread_mutex_unlock(&g_host_handle_mutex);
}

Widget XmGetFocusWidget(Widget widget)
{
    IrixHostAbiContext *context;
    IrixHostOpaqueHandle *handle, *item;
    uint32_t result = 0;
    Widget host_widget = NULL;
    if (!context_default(&context)) return NULL;
    pthread_mutex_lock(&g_host_handle_mutex);
    handle = host_handle_find(widget, IRIX_HOST_HANDLE_WIDGET);
    if (handle && !irix_host_abi_context_call(context, "XmGetFocusWidget",
            &handle->guest_address, 1u, &result) && result) {
        for (item = g_host_opaque_handles; item; item = item->next)
            if (item->alive && item->kind == IRIX_HOST_HANDLE_WIDGET &&
                item->guest_address == result) {
                host_widget = item->widget_object;
                break;
            }
    }
    pthread_mutex_unlock(&g_host_handle_mutex);
    return host_widget;
}

void XmListSelectPos(Widget widget, int position, Boolean notify)
{
    IrixHostAbiContext *context;
    IrixHostOpaqueHandle *handle;
    uint32_t args[3];
    if (!context_default(&context)) return;
    pthread_mutex_lock(&g_host_handle_mutex);
    handle = host_handle_find(widget, IRIX_HOST_HANDLE_WIDGET);
    if (handle) {
        args[0] = handle->guest_address;
        args[1] = (uint32_t)position;
        args[2] = notify ? 1u : 0u;
        (void)irix_host_abi_context_call(context, "XmListSelectPos", args, 3u,
                                         NULL);
    }
    pthread_mutex_unlock(&g_host_handle_mutex);
}

Cursor XmGetMenuCursor(Display *display)
{
    IrixHostAbiContext *context;
    IrixHostOpaqueHandle *app_handle;
    uint32_t guest_display, result = None;
    if (!display || !context_default(&context)) return None;
    pthread_mutex_lock(&g_host_handle_mutex);
    app_handle = host_app_find_by_display(display);
    if (app_handle) {
        guest_display = app_handle->guest_display_address;
        (void)irix_host_abi_context_call(context, "XmGetMenuCursor",
                                         &guest_display, 1u, &result);
    }
    pthread_mutex_unlock(&g_host_handle_mutex);
    return (Cursor)result;
}

void XmSetMenuCursor(Display *display, Cursor cursor)
{
    IrixHostAbiContext *context;
    IrixHostOpaqueHandle *app_handle;
    uint32_t args[2];
    if (!display || !context_default(&context)) return;
    pthread_mutex_lock(&g_host_handle_mutex);
    app_handle = host_app_find_by_display(display);
    if (app_handle) {
        args[0] = app_handle->guest_display_address;
        args[1] = (uint32_t)cursor;
        (void)irix_host_abi_context_call(context, "XmSetMenuCursor", args, 2u,
                                         NULL);
    }
    pthread_mutex_unlock(&g_host_handle_mutex);
}

void XtDestroyWidget(Widget widget)
{
    IrixHostAbiContext *context;
    IrixHostOpaqueHandle *handle;
    uint32_t arg;
    if (!context_default(&context)) return;
    pthread_mutex_lock(&g_host_handle_mutex);
    handle = host_handle_find(widget, IRIX_HOST_HANDLE_WIDGET);
    if (handle) {
        arg = handle->guest_address;
        if (irix_host_abi_context_call(context, "XtDestroyWidget", &arg, 1u,
                                       NULL) == 0)
            host_handles_invalidate_widget_tree(handle->guest_address,
                                               handle->app_guest_address);
    }
    pthread_mutex_unlock(&g_host_handle_mutex);
}

void XtDestroyApplicationContext(XtAppContext app_context)
{
    IrixHostAbiContext *context;
    IrixHostOpaqueHandle *handle, *item;
    uint32_t arg;
    if (!context_default(&context)) return;
    pthread_mutex_lock(&g_host_handle_mutex);
    handle = host_handle_find(app_context, IRIX_HOST_HANDLE_APP_CONTEXT);
    if (handle) {
        arg = handle->guest_address;
        if (irix_host_abi_context_call(context, "XtDestroyApplicationContext",
                                       &arg, 1u, NULL) == 0) {
            handle->alive = 0;
            for (item = g_host_opaque_handles; item; item = item->next)
                if (item->kind == IRIX_HOST_HANDLE_WIDGET &&
                    item->app_guest_address == handle->guest_address)
                    item->alive = 0;
        }
    }
    pthread_mutex_unlock(&g_host_handle_mutex);
}

XtErrorMsgHandler XtAppSetWarningMsgHandler(XtAppContext app_context,
                                            XtErrorMsgHandler handler)
{
    (void)app_context; (void)handler;
    return NULL;
}

XtErrorHandler XtAppSetWarningHandler(XtAppContext app_context,
                                      XtErrorHandler handler)
{
    (void)app_context; (void)handler;
    return NULL;
}

/*
 * Native Motif text/list operations used by Netscape's XFE.  These wrappers
 * share the opaque Widget/XmString maps and marshal every guest pointer into
 * the context arena; host pointers are never passed to N32 code.
 */
static int host_motif_widget_call(const char *api, Widget widget,
                                  const uint32_t *tail, unsigned tail_count,
                                  uint32_t *result)
{
    IrixHostAbiContext *context;
    IrixHostOpaqueHandle *handle;
    uint32_t args[8];
    unsigned i;
    int status = -1;
    if (tail_count > 7u || !context_default(&context)) return -1;
    pthread_mutex_lock(&g_host_handle_mutex);
    handle = host_handle_find(widget, IRIX_HOST_HANDLE_WIDGET);
    if (handle) {
        args[0] = handle->guest_address;
        for (i = 0; i < tail_count; ++i) args[i + 1] = tail[i];
        status = irix_host_abi_context_call(context, api, args,
                                              tail_count + 1u, result);
    }
    pthread_mutex_unlock(&g_host_handle_mutex);
    return status;
}

static int host_motif_widget_text_call(const char *api, Widget widget,
                                       const uint32_t *prefix,
                                       unsigned prefix_count,
                                       const char *text)
{
    IrixHostAbiContext *context;
    IrixHostOpaqueHandle *handle;
    IrixGuestAddr mark, guest_text;
    uint32_t args[8];
    unsigned i;
    int status = -1;
    if (prefix_count > 6u || !text || !context_default(&context)) return -1;
    pthread_mutex_lock(&g_host_handle_mutex);
    handle = host_handle_find(widget, IRIX_HOST_HANDLE_WIDGET);
    mark = context_arena_mark(context);
    if (handle && !irix_host_abi_context_copy_string(context, text,
                                                      &guest_text)) {
        args[0] = handle->guest_address;
        for (i = 0; i < prefix_count; ++i) args[i + 1] = prefix[i];
        args[prefix_count + 1] = guest_text;
        status = irix_host_abi_context_call(context, api, args,
                                             prefix_count + 2u, NULL);
    }
    context_arena_rewind(context, mark);
    pthread_mutex_unlock(&g_host_handle_mutex);
    return status;
}

static int host_motif_xmstring_guest(XmString string,
                                    IrixHostAbiContext *context,
                                    IrixGuestAddr *guest_string)
{
    IrixHostXmStringHandle *item;
    (void)context;
    for (item = g_host_xmstrings; item; item = item->next)
        if ((XmString)item == string) {
            *guest_string = item->guest_address;
            return 0;
        }
    return -1;
}

typedef struct HostXmStringContext {
    uint32_t guest;
    struct HostXmStringContext *next;
} HostXmStringContext;
static HostXmStringContext *g_host_string_contexts;

static HostXmStringContext *host_xm_string_context(XmStringContext context)
{
    HostXmStringContext *item;
    for (item = g_host_string_contexts; item; item = item->next)
        if ((XmStringContext)item == context) return item;
    return NULL;
}

Boolean XmStringInitContext(XmStringContext *context_out, XmString string)
{
    IrixHostAbiContext *context;
    HostXmStringContext *item;
    IrixGuestAddr mark, guest_slot, guest_string;
    uint8_t zero[4] = {0};
    uint32_t args[2], guest_context = 0, result = 0;
    if (context_out) *context_out = NULL;
    if (!context_out || !context_default(&context)) return False;
    item = calloc(1, sizeof(*item));
    if (!item) return False;
    pthread_mutex_lock(&g_host_handle_mutex);
    mark = context_arena_mark(context);
    if (!host_motif_xmstring_guest(string, context, &guest_string) &&
        !irix_host_abi_context_copy_bytes(context, zero, sizeof(zero),
                                          &guest_slot)) {
        args[0] = guest_slot; args[1] = guest_string;
        if (!irix_host_abi_context_call(context, "XmStringInitContext", args,
                                        2, &result) && result &&
            !irix_host_abi_context_read32be(context, guest_slot,
                                             &guest_context) && guest_context) {
            item->guest = guest_context;
            item->next = g_host_string_contexts;
            g_host_string_contexts = item;
            *context_out = (XmStringContext)item;
            item = NULL;
        }
    }
    context_arena_rewind(context, mark);
    pthread_mutex_unlock(&g_host_handle_mutex);
    free(item);
    return *context_out ? True : False;
}

static char *host_guest_string_copy(IrixHostAbiContext *context,
                                    IrixGuestAddr guest_string)
{
    const char *text;
    char *copy;
    if (!guest_string) return NULL;
    text = irix_guest_cstr(&context->runtime, guest_string, 1u << 20);
    if (!text) return NULL;
    copy = XtMalloc((Cardinal)strlen(text) + 1u);
    if (copy) strcpy(copy, text);
    return copy;
}

Boolean XmStringGetNextSegment(XmStringContext string_context, char **text_out,
                               XmStringCharSet *charset_out,
                               XmStringDirection *direction_out,
                               Boolean *separator_out)
{
    IrixHostAbiContext *context;
    HostXmStringContext *item;
    IrixGuestAddr mark, guest_text = 0, guest_charset = 0;
    IrixGuestAddr guest_text_out, guest_charset_out, guest_direction_out;
    IrixGuestAddr guest_separator_out;
    uint8_t zero[16] = {0};
    uint32_t args[5], result = 0, direction = 0, separator = 0;
    char *text = NULL, *charset = NULL;
    if (text_out) *text_out = NULL;
    if (charset_out) *charset_out = NULL;
    if (direction_out) *direction_out = XmSTRING_DIRECTION_L_TO_R;
    if (separator_out) *separator_out = False;
    if (!text_out || !charset_out || !direction_out || !separator_out ||
        !context_default(&context)) return False;
    pthread_mutex_lock(&g_host_handle_mutex);
    item = host_xm_string_context(string_context);
    mark = context_arena_mark(context);
    if (!item || irix_host_abi_context_copy_bytes(context, zero, sizeof(zero),
            &guest_text_out)) goto done;
    guest_charset_out = guest_text_out + 4u;
    guest_direction_out = guest_text_out + 8u;
    guest_separator_out = guest_text_out + 12u;
    args[0] = item->guest; args[1] = guest_text_out;
    args[2] = guest_charset_out; args[3] = guest_direction_out;
    args[4] = guest_separator_out;
    if (irix_host_abi_context_call(context, "XmStringGetNextSegment", args,
            5, &result) || !result ||
        irix_host_abi_context_read32be(context, guest_text_out, &guest_text) ||
        irix_host_abi_context_read32be(context, guest_charset_out,
                                       &guest_charset) ||
        irix_host_abi_context_read32be(context, guest_direction_out,
                                       &direction) ||
        irix_host_abi_context_read32be(context, guest_separator_out,
                                       &separator)) goto done;
    text = host_guest_string_copy(context, guest_text);
    charset = host_guest_string_copy(context, guest_charset);
    if (!text || !charset) { XtFree(text); XtFree(charset); goto done; }
    *text_out = text; *charset_out = charset;
    *direction_out = (XmStringDirection)direction;
    *separator_out = separator ? True : False;
done:
    context_arena_rewind(context, mark);
    pthread_mutex_unlock(&g_host_handle_mutex);
    return text ? True : False;
}

void XmStringFreeContext(XmStringContext string_context)
{
    IrixHostAbiContext *context;
    HostXmStringContext **link, *item;
    if (!context_default(&context)) return;
    pthread_mutex_lock(&g_host_handle_mutex);
    for (link = &g_host_string_contexts; *link; link = &(*link)->next)
        if ((XmStringContext)*link == string_context) break;
    item = *link;
    if (item) {
        (void)irix_host_abi_context_call(context, "XmStringFreeContext",
                                         &item->guest, 1, NULL);
        *link = item->next; free(item);
    }
    pthread_mutex_unlock(&g_host_handle_mutex);
}

XmString XmStringLtoRCreate(char *text, XmStringCharSet charset)
{ return XmStringCreateLtoR(text, charset); }

static XmString host_xm_string_wrap_local(uint32_t guest_address)
{
    IrixHostXmStringHandle *handle;
    if (!guest_address) return NULL;
    handle = calloc(1, sizeof(*handle));
    if (!handle) return NULL;
    handle->magic = IRIX_HOST_XMSTRING_MAGIC;
    handle->guest_address = guest_address;
    handle->next = g_host_xmstrings;
    g_host_xmstrings = handle;
    return (XmString)handle;
}

XmString XmStringNConcat(XmString first, XmString second, int length)
{
    IrixHostAbiContext *context;
    IrixGuestAddr guest_first = 0, guest_second = 0;
    uint32_t args[3], result = 0;
    XmString result_string = NULL;
    if (!context_default(&context) || length < 0) return NULL;
    pthread_mutex_lock(&g_host_handle_mutex);
    if (!host_motif_xmstring_guest(first, context, &guest_first) &&
        !host_motif_xmstring_guest(second, context, &guest_second)) {
        args[0] = guest_first; args[1] = guest_second;
        args[2] = (uint32_t)length;
        if (!irix_host_abi_context_call(context, "XmStringNConcat", args, 3,
                                        &result))
            result_string = host_xm_string_wrap_local(result);
    }
    pthread_mutex_unlock(&g_host_handle_mutex);
    return result_string;
}

#define HOST_MOTIF_WIDGET_GET(api, name, result_type, fallback) \
    result_type name(Widget widget) \
    { \
        uint32_t result = 0; \
        if (host_motif_widget_call(api, widget, NULL, 0, &result)) \
            return (result_type)(fallback); \
        return (result_type)result; \
    }

#define HOST_MOTIF_WIDGET_SET1(api, name, value_type) \
    void name(Widget widget, value_type value) \
    { \
        uint32_t tail[1] = {(uint32_t)value}; \
        (void)host_motif_widget_call(api, widget, tail, 1, NULL); \
    }

HOST_MOTIF_WIDGET_GET("XmTextGetInsertionPosition", XmTextGetInsertionPosition,
                      XmTextPosition, 0)
HOST_MOTIF_WIDGET_GET("XmTextGetLastPosition", XmTextGetLastPosition,
                      XmTextPosition, 0)
HOST_MOTIF_WIDGET_GET("XmTextGetMaxLength", XmTextGetMaxLength, int, 0)
HOST_MOTIF_WIDGET_GET("XmTextFieldGetCursorPosition",
                      XmTextFieldGetCursorPosition, XmTextPosition, 0)
HOST_MOTIF_WIDGET_GET("XmTextFieldGetEditable", XmTextFieldGetEditable,
                      Boolean, False)
HOST_MOTIF_WIDGET_GET("XmTextFieldGetInsertionPosition",
                      XmTextFieldGetInsertionPosition, XmTextPosition, 0)
HOST_MOTIF_WIDGET_GET("XmTextFieldGetLastPosition",
                      XmTextFieldGetLastPosition, XmTextPosition, 0)
HOST_MOTIF_WIDGET_GET("XmListGetKbdItemPos", XmListGetKbdItemPos, int, 0)
HOST_MOTIF_WIDGET_SET1("XmTextSetCursorPosition", XmTextSetCursorPosition,
                       XmTextPosition)
HOST_MOTIF_WIDGET_SET1("XmTextSetInsertionPosition", XmTextSetInsertionPosition,
                       XmTextPosition)
HOST_MOTIF_WIDGET_SET1("XmTextFieldSetCursorPosition",
                       XmTextFieldSetCursorPosition, XmTextPosition)
HOST_MOTIF_WIDGET_SET1("XmTextFieldSetInsertionPosition",
                       XmTextFieldSetInsertionPosition, XmTextPosition)
HOST_MOTIF_WIDGET_SET1("XmTextFieldShowPosition", XmTextFieldShowPosition,
                       XmTextPosition)
HOST_MOTIF_WIDGET_SET1("XmListDeletePos", XmListDeletePos, int)
HOST_MOTIF_WIDGET_SET1("XmListDeselectPos", XmListDeselectPos, int)
HOST_MOTIF_WIDGET_SET1("XmListSetPos", XmListSetPos, int)

Boolean XmListPosSelected(Widget widget, int position)
{
    uint32_t tail[1] = {(uint32_t)position}, result = 0;
    return host_motif_widget_call("XmListPosSelected", widget, tail, 1, &result)
        ? False : (Boolean)result;
}

void XmListDeleteAllItems(Widget widget)
{
    (void)host_motif_widget_call("XmListDeleteAllItems", widget, NULL, 0, NULL);
}

void XmListDeselectAllItems(Widget widget)
{
    (void)host_motif_widget_call("XmListDeselectAllItems", widget, NULL, 0, NULL);
}

void XmListDeleteItemsPos(Widget widget, int item_count, int position)
{
    uint32_t tail[2] = {(uint32_t)item_count, (uint32_t)position};
    (void)host_motif_widget_call("XmListDeleteItemsPos", widget, tail, 2, NULL);
}

void XmListSelectItem(Widget widget, XmString item, Boolean notify)
{
    IrixHostAbiContext *context;
    IrixHostOpaqueHandle *handle;
    IrixGuestAddr guest_string;
    uint32_t args[3];
    if (!context_default(&context)) return;
    pthread_mutex_lock(&g_host_handle_mutex);
    handle = host_handle_find(widget, IRIX_HOST_HANDLE_WIDGET);
    if (handle && !host_motif_xmstring_guest(item, context, &guest_string)) {
        args[0] = handle->guest_address; args[1] = guest_string;
        args[2] = notify ? 1u : 0u;
        (void)irix_host_abi_context_call(context, "XmListSelectItem", args, 3, NULL);
    }
    pthread_mutex_unlock(&g_host_handle_mutex);
}

void XmListAddItem(Widget widget, XmString item, int position)
{
    IrixHostAbiContext *context;
    IrixHostOpaqueHandle *handle;
    IrixGuestAddr guest_string;
    uint32_t args[3];
    if (!context_default(&context)) return;
    pthread_mutex_lock(&g_host_handle_mutex);
    handle = host_handle_find(widget, IRIX_HOST_HANDLE_WIDGET);
    if (handle && !host_motif_xmstring_guest(item, context, &guest_string)) {
        args[0] = handle->guest_address; args[1] = guest_string;
        args[2] = (uint32_t)position;
        (void)irix_host_abi_context_call(context, "XmListAddItem", args, 3, NULL);
    }
    pthread_mutex_unlock(&g_host_handle_mutex);
}

void XmListSetItem(Widget widget, XmString item)
{
    IrixHostAbiContext *context;
    IrixHostOpaqueHandle *handle;
    IrixGuestAddr guest_string;
    uint32_t args[2];
    if (!context_default(&context)) return;
    pthread_mutex_lock(&g_host_handle_mutex);
    handle = host_handle_find(widget, IRIX_HOST_HANDLE_WIDGET);
    if (handle && !host_motif_xmstring_guest(item, context, &guest_string)) {
        args[0] = handle->guest_address; args[1] = guest_string;
        (void)irix_host_abi_context_call(context, "XmListSetItem", args, 2, NULL);
    }
    pthread_mutex_unlock(&g_host_handle_mutex);
}

static char *host_motif_text_get(const char *api, Widget widget)
{
    IrixHostAbiContext *context;
    IrixHostOpaqueHandle *handle;
    IrixGuestAddr guest_text = 0;
    uint32_t result = 0;
    const char *text;
    char *host_text = NULL;
    if (!context_default(&context)) return NULL;
    pthread_mutex_lock(&g_host_handle_mutex);
    handle = host_handle_find(widget, IRIX_HOST_HANDLE_WIDGET);
    if (handle) {
        result = handle->guest_address;
        if (irix_host_abi_context_call(context, api, &result, 1, &guest_text))
            guest_text = 0;
    }
    if (guest_text) {
        text = irix_guest_cstr(&context->runtime, guest_text, 1u << 20);
        if (text) {
            host_text = XtMalloc((Cardinal)strlen(text) + 1u);
            if (host_text) strcpy(host_text, text);
        }
        result = guest_text;
        (void)irix_host_abi_context_call(context, "XtFree", &result, 1u, NULL);
    }
    pthread_mutex_unlock(&g_host_handle_mutex);
    return host_text;
}

char *XmTextGetString(Widget widget)
{ return host_motif_text_get("XmTextGetString", widget); }
char *XmTextGetSelection(Widget widget)
{ return host_motif_text_get("XmTextGetSelection", widget); }
char *XmTextFieldGetString(Widget widget)
{ return host_motif_text_get("XmTextFieldGetString", widget); }
char *XmTextFieldGetSelection(Widget widget)
{ return host_motif_text_get("XmTextFieldGetSelection", widget); }

void XmTextSetString(Widget widget, char *text)
{ (void)host_motif_widget_text_call("XmTextSetString", widget, NULL, 0, text); }
void XmTextFieldSetString(Widget widget, char *text)
{ (void)host_motif_widget_text_call("XmTextFieldSetString", widget, NULL, 0, text); }

void XmTextInsert(Widget widget, XmTextPosition position, char *text)
{
    uint32_t prefix[1] = {(uint32_t)position};
    (void)host_motif_widget_text_call("XmTextInsert", widget, prefix, 1, text);
}
void XmTextFieldInsert(Widget widget, XmTextPosition position, char *text)
{
    uint32_t prefix[1] = {(uint32_t)position};
    (void)host_motif_widget_text_call("XmTextFieldInsert", widget, prefix, 1, text);
}

Boolean XmTextRemove(Widget widget)
{
    uint32_t result = 0;
    return host_motif_widget_call("XmTextRemove", widget, NULL, 0, &result)
        ? False : (Boolean)result;
}
Boolean XmTextFieldRemove(Widget widget)
{
    uint32_t result = 0;
    return host_motif_widget_call("XmTextFieldRemove", widget, NULL, 0, &result)
        ? False : (Boolean)result;
}

void XmTextClearSelection(Widget widget, Time time)
{
    uint32_t tail[1] = {(uint32_t)time};
    (void)host_motif_widget_call("XmTextClearSelection", widget, tail, 1, NULL);
}
void XmTextFieldClearSelection(Widget widget)
{
    (void)host_motif_widget_call("XmTextFieldClearSelection", widget, NULL, 0, NULL);
}
void XmTextSetSelection(Widget widget, XmTextPosition first,
                        XmTextPosition last, Time time)
{
    uint32_t tail[3] = {(uint32_t)first, (uint32_t)last, (uint32_t)time};
    (void)host_motif_widget_call("XmTextSetSelection", widget, tail, 3, NULL);
}
void XmTextFieldSetSelection(Widget widget, XmTextPosition first,
                              XmTextPosition last, Time time)
{
    uint32_t tail[3] = {(uint32_t)first, (uint32_t)last, (uint32_t)time};
    (void)host_motif_widget_call("XmTextFieldSetSelection", widget, tail, 3, NULL);
}
void XmTextSetHighlight(Widget widget, XmTextPosition first,
                        XmTextPosition last, XmHighlightMode mode)
{
    uint32_t tail[3] = {(uint32_t)first, (uint32_t)last, (uint32_t)mode};
    (void)host_motif_widget_call("XmTextSetHighlight", widget, tail, 3, NULL);
}
void XmTextFieldSetHighlight(Widget widget, XmTextPosition first,
                             XmTextPosition last, XmHighlightMode mode)
{
    uint32_t tail[3] = {(uint32_t)first, (uint32_t)last, (uint32_t)mode};
    (void)host_motif_widget_call("XmTextFieldSetHighlight", widget, tail, 3, NULL);
}

Boolean XmProcessTraversal(Widget widget, XmTraversalDirection direction)
{
    uint32_t tail[1] = {(uint32_t)direction}, result = 0;
    return host_motif_widget_call("XmProcessTraversal", widget, tail, 1, &result)
        ? False : (Boolean)result;
}

void XmUpdateDisplay(Widget widget)
{ (void)host_motif_widget_call("XmUpdateDisplay", widget, NULL, 0, NULL); }

Boolean XmToggleButtonGetState(Widget widget)
{
    uint32_t result = 0;
    return host_motif_widget_call("XmToggleButtonGetState", widget, NULL, 0,
                                  &result) ? False : (Boolean)result;
}
void XmToggleButtonSetState(Widget widget, Boolean state, Boolean notify)
{
    uint32_t tail[2] = {state ? 1u : 0u, notify ? 1u : 0u};
    (void)host_motif_widget_call("XmToggleButtonSetState", widget, tail, 2, NULL);
}
Boolean XmToggleButtonGadgetGetState(Widget widget)
{
    uint32_t result = 0;
    return host_motif_widget_call("XmToggleButtonGadgetGetState", widget,
                                  NULL, 0, &result) ? False : (Boolean)result;
}
void XmToggleButtonGadgetSetState(Widget widget, Boolean state, Boolean notify)
{
    uint32_t tail[2] = {state ? 1u : 0u, notify ? 1u : 0u};
    (void)host_motif_widget_call("XmToggleButtonGadgetSetState", widget,
                                 tail, 2, NULL);
}

void XmScrollBarSetValues(Widget widget, int value, int slider_size,
                          int increment, int page_increment, Boolean notify)
{
    uint32_t tail[5] = {(uint32_t)value, (uint32_t)slider_size,
        (uint32_t)increment, (uint32_t)page_increment, notify ? 1u : 0u};
    (void)host_motif_widget_call("XmScrollBarSetValues", widget, tail, 5, NULL);
}

Boolean XmListSetKbdItemPos(Widget widget, int position)
{
    uint32_t tail[1] = {(uint32_t)position}, result = 0;
    return host_motif_widget_call("XmListSetKbdItemPos", widget, tail, 1,
                                  &result) ? False : (Boolean)result;
}

static int host_motif_xmstring_array(IrixHostAbiContext *context,
                                     XmString *strings, int count,
                                     IrixGuestAddr *guest_array)
{
    IrixGuestAddr *guest_items;
    int i, status = -1;
    if (count < 0 || count > 4096 || (count && !strings)) return -1;
    guest_items = count ? calloc((size_t)count, sizeof(*guest_items)) : NULL;
    if (count && !guest_items) return -1;
    for (i = 0; i < count; ++i)
        if (host_motif_xmstring_guest(strings[i], context, &guest_items[i]))
            goto done;
    if (irix_host_abi_context_copy_bytes(context, guest_items,
            (size_t)count * sizeof(*guest_items), guest_array))
        goto done;
    for (i = 0; i < count; ++i)
        if (irix_host_abi_context_write32be(context,
                *guest_array + (IrixGuestAddr)i * 4u, guest_items[i]))
            goto done;
    status = 0;
done:
    free(guest_items);
    return status;
}

static void host_motif_list_array_call(const char *api, Widget widget,
                                      XmString *items, int count,
                                      int position)
{
    IrixHostAbiContext *context;
    IrixHostOpaqueHandle *handle;
    IrixGuestAddr mark, guest_items = 0;
    uint32_t args[4];
    if (!context_default(&context) || count < 0 || count > 4096) return;
    pthread_mutex_lock(&g_host_handle_mutex);
    handle = host_handle_find(widget, IRIX_HOST_HANDLE_WIDGET);
    mark = context_arena_mark(context);
    if (handle && !host_motif_xmstring_array(context, items, count,
                                             &guest_items)) {
        args[0] = handle->guest_address; args[1] = guest_items;
        args[2] = (uint32_t)count; args[3] = (uint32_t)position;
        (void)irix_host_abi_context_call(context, api, args, 4, NULL);
    }
    context_arena_rewind(context, mark);
    pthread_mutex_unlock(&g_host_handle_mutex);
}

void XmListAddItemsUnselected(Widget widget, XmString *items, int count,
                              int position)
{
    host_motif_list_array_call("XmListAddItemsUnselected", widget, items,
                               count, position);
}
void XmListReplaceItemsPos(Widget widget, XmString *items, int count,
                           int position)
{
    host_motif_list_array_call("XmListReplaceItemsPos", widget, items,
                               count, position);
}

void XmListDeletePositions(Widget widget, int *positions, int count)
{
    IrixHostAbiContext *context;
    IrixHostOpaqueHandle *handle;
    IrixGuestAddr mark, guest_positions = 0;
    uint32_t args[3];
    int i;
    if (!context_default(&context) || count < 0 || count > 4096 ||
        (count && !positions)) return;
    pthread_mutex_lock(&g_host_handle_mutex);
    handle = host_handle_find(widget, IRIX_HOST_HANDLE_WIDGET);
    mark = context_arena_mark(context);
    if (handle && !irix_host_abi_context_copy_bytes(context, positions,
            (size_t)count * 4u, &guest_positions)) {
        for (i = 0; i < count; ++i)
            if (irix_host_abi_context_write32be(context,
                    guest_positions + (IrixGuestAddr)i * 4u,
                    (uint32_t)positions[i])) break;
        if (i == count) {
            args[0] = handle->guest_address; args[1] = guest_positions;
            args[2] = (uint32_t)count;
            (void)irix_host_abi_context_call(context, "XmListDeletePositions",
                                              args, 3, NULL);
        }
    }
    context_arena_rewind(context, mark);
    pthread_mutex_unlock(&g_host_handle_mutex);
}

Boolean XmTextGetSelectionPosition(Widget widget, XmTextPosition *first,
                                   XmTextPosition *last)
{
    IrixHostAbiContext *context;
    IrixHostOpaqueHandle *handle;
    IrixGuestAddr mark, guest_out = 0;
    uint32_t args[3], result = 0, a = 0, b = 0;
    uint8_t zero[8] = {0};
    if (first) *first = 0;
    if (last) *last = 0;
    if (!first || !last || !context_default(&context)) return False;
    pthread_mutex_lock(&g_host_handle_mutex);
    handle = host_handle_find(widget, IRIX_HOST_HANDLE_WIDGET);
    mark = context_arena_mark(context);
    if (handle && !irix_host_abi_context_copy_bytes(context, zero, 8,
                                                    &guest_out)) {
        args[0] = handle->guest_address; args[1] = guest_out;
        args[2] = guest_out + 4u;
        if (!irix_host_abi_context_call(context, "XmTextGetSelectionPosition",
                                        args, 2, &result) && result &&
            !irix_host_abi_context_read32be(context, guest_out, &a) &&
            !irix_host_abi_context_read32be(context, guest_out + 4, &b)) {
            *first = (XmTextPosition)(int32_t)a;
            *last = (XmTextPosition)(int32_t)b;
        }
    }
    context_arena_rewind(context, mark);
    pthread_mutex_unlock(&g_host_handle_mutex);
    return result ? True : False;
}

Boolean XmTextFieldGetSelectionPosition(Widget widget, XmTextPosition *first,
                                        XmTextPosition *last)
{
    IrixHostAbiContext *context;
    IrixHostOpaqueHandle *handle;
    IrixGuestAddr mark, guest_out = 0;
    uint32_t args[3], result = 0, a = 0, b = 0;
    uint8_t zero[8] = {0};
    if (first) *first = 0;
    if (last) *last = 0;
    if (!first || !last || !context_default(&context)) return False;
    pthread_mutex_lock(&g_host_handle_mutex);
    handle = host_handle_find(widget, IRIX_HOST_HANDLE_WIDGET);
    mark = context_arena_mark(context);
    if (handle && !irix_host_abi_context_copy_bytes(context, zero, 8,
                                                    &guest_out)) {
        args[0] = handle->guest_address; args[1] = guest_out;
        args[2] = guest_out + 4u;
        if (!irix_host_abi_context_call(context,
                "XmTextFieldGetSelectionPosition", args, 2, &result) && result &&
            !irix_host_abi_context_read32be(context, guest_out, &a) &&
            !irix_host_abi_context_read32be(context, guest_out + 4, &b)) {
            *first = (XmTextPosition)(int32_t)a;
            *last = (XmTextPosition)(int32_t)b;
        }
    }
    context_arena_rewind(context, mark);
    pthread_mutex_unlock(&g_host_handle_mutex);
    return result ? True : False;
}

XmTextPosition XmTextFieldXYToPos(Widget widget, Position x, Position y)
{
    uint32_t tail[2] = {(uint32_t)(int32_t)x, (uint32_t)(int32_t)y}, result = 0;
    return host_motif_widget_call("XmTextFieldXYToPos", widget, tail, 2,
                                  &result) ? (XmTextPosition)-1
                                           : (XmTextPosition)(int32_t)result;
}

static Widget host_motif_return_widget(IrixHostOpaqueHandle *parent,
                                      uint32_t guest_widget,
                                      const char *class_symbol)
{
    IrixHostOpaqueHandle *handle;
    WidgetClass widget_class = NULL;
    if (!parent || !guest_widget) return NULL;
    handle = host_handle_create(IRIX_HOST_HANDLE_WIDGET, guest_widget,
                                parent->guest_address,
                                parent->app_guest_address);
    if (!handle) return NULL;
    if (class_symbol) {
        widget_class = host_motif_class_from_facade(class_symbol);
        if (!widget_class) {
            WidgetClass *slot = (WidgetClass *)dlsym(RTLD_DEFAULT, class_symbol);
            if (slot) widget_class = *slot;
        }
        if (widget_class) (void)host_widget_promote_class(handle, widget_class);
    }
    return host_widget_value(handle);
}

static Widget host_motif_get_child(const char *api, Widget widget,
                                   unsigned child, const char *class_symbol)
{
    IrixHostAbiContext *context;
    IrixHostOpaqueHandle *parent;
    uint32_t args[2], guest_child = 0;
    Widget result = NULL;
    if (!context_default(&context)) return NULL;
    pthread_mutex_lock(&g_host_handle_mutex);
    parent = host_handle_find(widget, IRIX_HOST_HANDLE_WIDGET);
    if (parent) {
        args[0] = parent->guest_address; args[1] = child;
        if (!irix_host_abi_context_call(context, api, args, 2, &guest_child))
            result = host_motif_return_widget(parent, guest_child, class_symbol);
    }
    pthread_mutex_unlock(&g_host_handle_mutex);
    return result;
}

Widget XmFileSelectionBoxGetChild(Widget widget, unsigned char child)
{
    return host_motif_get_child("XmFileSelectionBoxGetChild", widget, child,
                                "xmListWidgetClass");
}
Widget XmMessageBoxGetChild(Widget widget, unsigned char child)
{
    return host_motif_get_child("XmMessageBoxGetChild", widget, child,
                                "xmPushButtonWidgetClass");
}
Widget XmSelectionBoxGetChild(Widget widget, unsigned char child)
{
    return host_motif_get_child("XmSelectionBoxGetChild", widget, child,
                                "xmTextFieldWidgetClass");
}

void XmFileSelectionDoSearch(Widget widget, XmString directory_mask)
{
    IrixHostAbiContext *context;
    IrixHostOpaqueHandle *handle;
    IrixGuestAddr guest_mask;
    uint32_t args[2];
    if (!context_default(&context)) return;
    pthread_mutex_lock(&g_host_handle_mutex);
    handle = host_handle_find(widget, IRIX_HOST_HANDLE_WIDGET);
    if (handle && !host_motif_xmstring_guest(directory_mask, context,
                                             &guest_mask)) {
        args[0] = handle->guest_address; args[1] = guest_mask;
        (void)irix_host_abi_context_call(context, "XmFileSelectionDoSearch",
                                         args, 2, NULL);
    }
    pthread_mutex_unlock(&g_host_handle_mutex);
}

static Widget host_motif_child_getter(const char *api, Widget widget,
                                      const char *class_symbol)
{
    IrixHostAbiContext *context;
    IrixHostOpaqueHandle *parent;
    uint32_t arg, guest_child = 0;
    Widget result = NULL;
    if (!context_default(&context)) return NULL;
    pthread_mutex_lock(&g_host_handle_mutex);
    parent = host_handle_find(widget, IRIX_HOST_HANDLE_WIDGET);
    if (parent) {
        arg = parent->guest_address;
        if (!irix_host_abi_context_call(context, api, &arg, 1, &guest_child))
            result = host_motif_return_widget(parent, guest_child, class_symbol);
    }
    pthread_mutex_unlock(&g_host_handle_mutex);
    return result;
}
Widget XmOptionButtonGadget(Widget widget)
{ return host_motif_child_getter("XmOptionButtonGadget", widget,
                                 "xmPushButtonWidgetClass"); }
Widget XmOptionLabelGadget(Widget widget)
{ return host_motif_child_getter("XmOptionLabelGadget", widget,
                                 "xmLabelWidgetClass"); }

void XmScrollVisible(Widget scrolled_window, Widget child,
                     Dimension horizontal_margin, Dimension vertical_margin)
{
    IrixHostAbiContext *context;
    IrixHostOpaqueHandle *parent, *child_handle;
    uint32_t args[4];
    if (!context_default(&context)) return;
    pthread_mutex_lock(&g_host_handle_mutex);
    parent = host_handle_find(scrolled_window, IRIX_HOST_HANDLE_WIDGET);
    child_handle = host_handle_find(child, IRIX_HOST_HANDLE_WIDGET);
    if (parent && child_handle) {
        args[0] = parent->guest_address; args[1] = child_handle->guest_address;
        args[2] = horizontal_margin; args[3] = vertical_margin;
        (void)irix_host_abi_context_call(context, "XmScrollVisible", args, 4, NULL);
    }
    pthread_mutex_unlock(&g_host_handle_mutex);
}

Widget XmGetDestination(Display *display)
{
    IrixHostAbiContext *context;
    IrixHostOpaqueHandle *app, *parent;
    uint32_t arg, guest_widget = 0;
    Widget result = NULL;
    if (!context_default(&context) || !display) return NULL;
    pthread_mutex_lock(&g_host_handle_mutex);
    app = host_app_find_by_display(display);
    parent = app;
    if (app) {
        arg = app->guest_display_address;
        if (!irix_host_abi_context_call(context, "XmGetDestination", &arg, 1,
                                       &guest_widget))
            result = host_motif_return_widget(parent, guest_widget, NULL);
    }
    pthread_mutex_unlock(&g_host_handle_mutex);
    return result;
}

/* Motif returns selected list positions through an int ** allocated on the
 * guest heap.  Keep the public ABI native: copy the positions into a host
 * allocation and translate the two output slots explicitly. */
static Boolean host_motif_list_positions(const char *api, Widget widget,
                                         XmString item, int **positions,
                                         int *count)
{
    IrixHostAbiContext *context;
    IrixHostOpaqueHandle *handle;
    IrixGuestAddr mark, guest_item = 0, guest_list_slot = 0,
                  guest_count_slot = 0, guest_list = 0;
    uint32_t args[4], n = 0, i;
    int *host_list = NULL;
    Boolean result = False;
    if (positions) *positions = NULL;
    if (count) *count = 0;
    if (!positions || !count || !context_default(&context)) return False;
    pthread_mutex_lock(&g_host_handle_mutex);
    handle = host_handle_find(widget, IRIX_HOST_HANDLE_WIDGET);
    mark = context_arena_mark(context);
    if (handle && (!item || !host_motif_xmstring_guest(item, context,
                                                        &guest_item)) &&
        !irix_host_abi_context_copy_bytes(context, "\0\0\0\0", 4,
                                           &guest_list_slot) &&
        !irix_host_abi_context_copy_bytes(context, "\0\0\0\0", 4,
                                           &guest_count_slot)) {
        args[0] = handle->guest_address;
        if (item) args[1] = guest_item;
        if (item) { args[2] = guest_list_slot; args[3] = guest_count_slot; }
        else { args[1] = guest_list_slot; args[2] = guest_count_slot; }
        if (!irix_host_abi_context_call(context, api, args,
                                         item ? 4u : 3u, &n) && n) {
            (void)irix_host_abi_context_read32be(context, guest_list_slot,
                                                  &guest_list);
            (void)irix_host_abi_context_read32be(context, guest_count_slot,
                                                  &n);
            if (n <= 65536u) {
                host_list = n ? calloc(n, sizeof(*host_list)) : NULL;
                if (!n || host_list) {
                    for (i = 0; i < n; ++i)
                        host_list[i] = (int)irix_guest_read32be(
                            &context->runtime, guest_list + i * 4u);
                    *positions = host_list; *count = (int)n;
                    result = True;
                }
            }
        }
    }
    context_arena_rewind(context, mark);
    pthread_mutex_unlock(&g_host_handle_mutex);
    return result;
}

Boolean XmListGetSelectedPos(Widget widget, int **positions, int *count)
{ return host_motif_list_positions("XmListGetSelectedPos", widget, NULL,
                                    positions, count); }
Boolean XmListGetMatchPos(Widget widget, XmString item, int **positions,
                          int *count)
{ return host_motif_list_positions("XmListGetMatchPos", widget, item,
                                    positions, count); }

char *_XmStringGetTextConcat(XmString string)
{
    IrixHostAbiContext *context;
    IrixGuestAddr mark, guest_string = 0;
    uint32_t result = 0;
    char *copy = NULL;
    size_t n;
    if (!context_default(&context)) return NULL;
    pthread_mutex_lock(&g_host_handle_mutex);
    mark = context_arena_mark(context);
    if (!host_motif_xmstring_guest(string, context, &guest_string) &&
        !irix_host_abi_context_call(context, "_XmStringGetTextConcat",
                                     &guest_string, 1, &result) && result) {
        for (n = 0; n < 1024u * 1024u &&
             irix_guest_read8(&context->runtime, result + (uint32_t)n); ++n) {}
        if (n < 1024u * 1024u) {
            copy = malloc(n + 1u);
            if (copy) {
                for (size_t i = 0; i < n; ++i)
                    copy[i] = (char)irix_guest_read8(&context->runtime,
                                               result + (uint32_t)i);
                copy[n] = '\0';
            }
        }
    }
    context_arena_rewind(context, mark);
    pthread_mutex_unlock(&g_host_handle_mutex);
    return copy;
}

static void host_motif_im_simple(const char *api, Widget widget,
                                 unsigned int reserved, int has_reserved)
{
    IrixHostAbiContext *context; IrixHostOpaqueHandle *handle;
    uint32_t args[2];
    if (!context_default(&context)) return;
    pthread_mutex_lock(&g_host_handle_mutex);
    handle = host_handle_find(widget, IRIX_HOST_HANDLE_WIDGET);
    if (handle) {
        args[0] = handle->guest_address; args[1] = reserved;
        (void)irix_host_abi_context_call(context, api, args,
                                         has_reserved ? 2u : 1u, NULL);
    }
    pthread_mutex_unlock(&g_host_handle_mutex);
}
void XmImRegister(Widget widget, unsigned int reserved)
{ host_motif_im_simple("XmImRegister", widget, reserved, 1); }
void XmImUnregister(Widget widget)
{ host_motif_im_simple("XmImUnregister", widget, 0, 0); }
void XmImUnsetFocus(Widget widget)
{ host_motif_im_simple("XmImUnsetFocus", widget, 0, 0); }

static void host_motif_im_values(const char *api, Widget widget, ArgList args,
                                 Cardinal count)
{
    IrixHostAbiContext *context; IrixHostOpaqueHandle *handle;
    IrixGuestAddr mark, guest_args = 0;
    uint32_t call_args[3]; Cardinal i;
    typedef struct { uint32_t name, value; } GuestArg;
    GuestArg *converted;
    if (!context_default(&context) || (count && !args) || count > 1024) return;
    converted = count ? calloc(count, sizeof(*converted)) : NULL;
    if (count && !converted) return;
    pthread_mutex_lock(&g_host_handle_mutex);
    handle = host_handle_find(widget, IRIX_HOST_HANDLE_WIDGET);
    mark = context_arena_mark(context);
    if (handle && !irix_host_abi_context_copy_bytes(context, converted,
            count * sizeof(*converted), &guest_args)) {
        for (i = 0; i < count; ++i) {
            IrixGuestAddr name = 0;
            if (irix_host_abi_context_copy_string(context, args[i].name,
                                                   &name)) break;
            irix_guest_write32be(&context->runtime, guest_args + i * 8u, name);
            irix_guest_write32be(&context->runtime, guest_args + i * 8u + 4u,
                                  (uint32_t)args[i].value);
        }
        if (i == count) {
            call_args[0] = handle->guest_address; call_args[1] = guest_args;
            call_args[2] = count;
            (void)irix_host_abi_context_call(context, api, call_args, 3, NULL);
        }
    }
    context_arena_rewind(context, mark);
    pthread_mutex_unlock(&g_host_handle_mutex);
    free(converted);
}
void XmImSetValues(Widget widget, ArgList args, Cardinal count)
{ host_motif_im_values("XmImSetValues", widget, args, count); }
void XmImSetFocusValues(Widget widget, ArgList args, Cardinal count)
{ host_motif_im_values("XmImSetFocusValues", widget, args, count); }

String _XmOSFindPatternPart(String spec)
{
    const char *wild;
    if (!spec) return NULL;
    wild = strpbrk(spec, "*?[");
    if (!wild) return spec + strlen(spec);
    while (wild > spec && wild[-1] != '/') --wild;
    return (String)wild;
}
int _XmOSFileCompare(XmConst void *a, XmConst void *b)
{
    const char *left = a ? *(char * const *)a : "";
    const char *right = b ? *(char * const *)b : "";
    return strcoll(left ? left : "", right ? right : "");
}
void _XmOSQualifyFileSpec(String dir, String filter, String *out_dir,
                          String *out_pattern)
{
    char *slash;
    const char *base = filter ? filter : "*";
    char *directory = NULL;
    if (out_dir) *out_dir = NULL;
    if (out_pattern) *out_pattern = NULL;
    if (!out_dir || !out_pattern) return;
    directory = strdup(dir && *dir ? dir : ".");
    if (!directory) return;
    if (base[0] == '/') {
        free(directory); directory = strdup(base);
    } else if (strchr(base, '/')) {
        size_t n = strlen(directory) + strlen(base) + 2;
        char *joined = malloc(n);
        if (joined) { snprintf(joined, n, "%s/%s", directory, base);
            free(directory); directory = joined; }
    }
    if (!directory) return;
    slash = strrchr(directory, '/');
    if (slash) {
        *slash = '\0';
        *out_pattern = XtNewString(slash + 1);
        *out_dir = XtNewString(*directory ? directory : "/");
    } else {
        *out_pattern = XtNewString(directory);
        *out_dir = XtNewString(dir && *dir ? dir : ".");
    }
    free(directory);
}
void _XmOSBuildFileList(String dir, String pattern, unsigned char type_mask,
                        String **entries, unsigned int *entry_count,
                        unsigned int *allocated)
{
    glob_t matches; char *query; size_t i; unsigned int n = 0;
    String *list = NULL;
    if (entries) *entries = NULL;
    if (entry_count) *entry_count = 0;
    if (allocated) *allocated = 0;
    if (!entries || !entry_count || !allocated) return;
    if (!dir || !*dir) dir = ".";
    if (!pattern || !*pattern) pattern = "*";
    query = malloc(strlen(dir) + strlen(pattern) + 2);
    if (!query) return;
    snprintf(query, strlen(dir) + strlen(pattern) + 2, "%s/%s", dir, pattern);
    memset(&matches, 0, sizeof(matches));
    if (glob(query, 0, NULL, &matches) == 0 && matches.gl_pathc <= 65536) {
        list = matches.gl_pathc ? (String *)XtMalloc(
            matches.gl_pathc * sizeof(*list)) : NULL;
        if (!matches.gl_pathc || list) {
            for (i = 0; i < matches.gl_pathc; ++i) {
                struct stat st;
                if (stat(matches.gl_pathv[i], &st)) continue;
                if ((S_ISDIR(st.st_mode) && !(type_mask & XmFILE_DIRECTORY)) ||
                    (!S_ISDIR(st.st_mode) && !(type_mask & XmFILE_REGULAR)))
                    continue;
                list[n] = XtNewString(matches.gl_pathv[i]);
                if (list[n]) ++n;
            }
            qsort(list, n, sizeof(*list), _XmOSFileCompare);
            *entries = list; *entry_count = n; *allocated = matches.gl_pathc;
        }
    }
    globfree(&matches); free(query);
}

void _XmMoveObject(Widget widget, Position x, Position y)
{
    IrixHostAbiContext *context; IrixHostOpaqueHandle *handle;
    uint32_t args[3];
    if (!context_default(&context)) return;
    pthread_mutex_lock(&g_host_handle_mutex);
    handle = host_handle_find(widget, IRIX_HOST_HANDLE_WIDGET);
    if (handle) { args[0] = handle->guest_address; args[1] = (uint16_t)x;
        args[2] = (uint16_t)y;
        (void)irix_host_abi_context_call(context, "_XmMoveObject", args, 3,
                                         NULL); }
    pthread_mutex_unlock(&g_host_handle_mutex);
}
Boolean _XmShellIsExclusive(Widget widget)
{
    IrixHostAbiContext *context; IrixHostOpaqueHandle *handle;
    uint32_t arg, result = 0;
    if (!context_default(&context)) return False;
    pthread_mutex_lock(&g_host_handle_mutex);
    handle = host_handle_find(widget, IRIX_HOST_HANDLE_WIDGET);
    if (handle) { arg = handle->guest_address;
        (void)irix_host_abi_context_call(context, "_XmShellIsExclusive", &arg,
                                         1, &result); }
    pthread_mutex_unlock(&g_host_handle_mutex);
    return result ? True : False;
}
void _XmSetDestination(Display *display, Widget widget)
{
    IrixHostAbiContext *context; IrixHostOpaqueHandle *app, *handle;
    uint32_t args[2];
    if (!context_default(&context)) return;
    pthread_mutex_lock(&g_host_handle_mutex);
    app = host_app_find_by_display(display);
    handle = host_handle_find(widget, IRIX_HOST_HANDLE_WIDGET);
    if (app && handle) { args[0] = app->guest_display_address;
        args[1] = handle->guest_address;
        (void)irix_host_abi_context_call(context, "_XmSetDestination", args, 2,
                                         NULL); }
    pthread_mutex_unlock(&g_host_handle_mutex);
}

static void host_xm_string_draw_native(Display *display, Drawable drawable,
                                       GC gc, XmString string, Position x,
                                       Position y, XmString underline)
{
    char *text = _XmStringGetTextConcat(string);
    if (display && drawable && gc && text) {
        XDrawString(display, drawable, gc, x, y, text, (int)strlen(text));
        if (underline) {
            XGCValues values;
            XFontStruct *font = NULL;
            int width = 0;
            if (XGetGCValues(display, gc, GCFont, &values))
                font = XQueryFont(display, values.font);
            if (font) width = XTextWidth(font, text, (int)strlen(text));
            XDrawLine(display, drawable, gc, x, y + 1, x + width, y + 1);
            if (font) XFreeFont(display, font);
        }
    }
    free(text);
}

void XmStringDraw(Display *display, Window window, XmFontList fontlist,
                  XmString string, GC gc, Position x, Position y,
                  Dimension width, unsigned char align,
                  unsigned char direction, XRectangle *clip)
{
    (void)fontlist; (void)width; (void)align; (void)direction; (void)clip;
    host_xm_string_draw_native(display, window, gc, string, x, y, NULL);
}
void XmStringDrawUnderline(Display *display, Window window, XmFontList fontlist,
                           XmString string, GC gc, Position x, Position y,
                           Dimension width, unsigned char align,
                           unsigned char direction, XRectangle *clip,
                           XmString underline)
{
    (void)fontlist; (void)width; (void)align; (void)direction; (void)clip;
    host_xm_string_draw_native(display, window, gc, string, x, y, underline);
}
void _XmStringDraw(Display *display, Window window, XmFontList fontlist,
                   _XmString string, GC gc, Position x, Position y,
                   Dimension width, unsigned char align,
                   unsigned char direction, XRectangle *clip)
{
    XmStringDraw(display, window, fontlist, (XmString)string, gc, x, y, width,
                 align, direction, clip);
}

static void host_xm_event_action(const char *api, Widget widget, XEvent *event,
                                 String *params, Cardinal *num_params,
                                 int has_count_out)
{
    IrixHostAbiContext *context; IrixHostOpaqueHandle *handle;
    IrixGuestAddr mark, guest_event = 0, guest_params = 0, guest_count = 0;
    uint32_t args[4], count = num_params ? *num_params : 0, i;
    if (!context_default(&context) || count > 256) return;
    pthread_mutex_lock(&g_host_handle_mutex);
    handle = host_handle_find(widget, IRIX_HOST_HANDLE_WIDGET);
    mark = context_arena_mark(context);
    if (handle && event && !irix_host_abi_context_copy_bytes(context, event,
            sizeof(*event), &guest_event) &&
        !irix_x11_image_write_n32_event(&context->runtime, 0, guest_event,
                                         event)) {
        args[0] = handle->guest_address; args[1] = guest_event;
        if (params && count) {
            if (!irix_host_abi_context_copy_bytes(context, params,
                    count * sizeof(uint32_t), &guest_params)) {
                for (i = 0; i < count; ++i) {
                    IrixGuestAddr guest_text = 0;
                    if (irix_host_abi_context_copy_string(context, params[i],
                                                           &guest_text)) break;
                    irix_guest_write32be(&context->runtime,
                        guest_params + i * 4u, guest_text);
                }
                if (i != count) guest_params = 0;
            }
        }
        args[2] = guest_params;
        if (has_count_out || num_params) {
            (void)irix_host_abi_context_copy_bytes(context, "\0\0\0\0", 4,
                                                    &guest_count);
            (void)irix_host_abi_context_write32be(context, guest_count, count);
            args[3] = guest_count;
        } else args[3] = count;
        (void)irix_host_abi_context_call(context, api, args, 4, NULL);
        if (num_params)
            (void)irix_host_abi_context_read32be(context, guest_count,
                                                  &count), *num_params = count;
    }
    context_arena_rewind(context, mark);
    pthread_mutex_unlock(&g_host_handle_mutex);
}
void _XmPrimitiveEnter(Widget widget, XEvent *event, String *params,
                      Cardinal *num_params)
{ host_xm_event_action("_XmPrimitiveEnter", widget, event, params,
                       num_params, 0); }
void _XmPrimitiveLeave(Widget widget, XEvent *event, String *params,
                      Cardinal *num_params)
{ host_xm_event_action("_XmPrimitiveLeave", widget, event, params,
                       num_params, 0); }
void _XmPostPopupMenu(Widget widget, XEvent *event)
{ host_xm_event_action("_XmPostPopupMenu", widget, event, NULL, NULL, 0); }
void _XmMenuPopDown(Widget widget, XEvent *event, Boolean *popped_up)
{
    IrixHostAbiContext *context; IrixHostOpaqueHandle *handle;
    IrixGuestAddr mark, guest_event = 0, guest_bool = 0;
    uint32_t args[3], result = 0;
    if (popped_up) *popped_up = False;
    if (!event || !context_default(&context)) return;
    pthread_mutex_lock(&g_host_handle_mutex);
    handle = host_handle_find(widget, IRIX_HOST_HANDLE_WIDGET);
    mark = context_arena_mark(context);
    if (handle && !irix_host_abi_context_copy_bytes(context, event,
            sizeof(*event), &guest_event) &&
        !irix_x11_image_write_n32_event(&context->runtime, 0, guest_event,
                                         event) &&
        !irix_host_abi_context_copy_bytes(context, "\0\0\0\0", 4,
                                           &guest_bool)) {
        args[0] = handle->guest_address; args[1] = guest_event;
        args[2] = guest_bool;
        (void)irix_host_abi_context_call(context, "_XmMenuPopDown", args, 3,
                                         &result);
        if (popped_up) *popped_up = irix_guest_read8(&context->runtime,
                                                     guest_bool) != 0;
    }
    context_arena_rewind(context, mark);
    pthread_mutex_unlock(&g_host_handle_mutex);
}

int _XmGrabKeyboard(Widget widget, int owner_events, int pointer_mode,
                    int keyboard_mode, Time time)
{
    IrixHostAbiContext *context; IrixHostOpaqueHandle *handle;
    uint32_t args[5], result = 0;
    if (!context_default(&context)) return GrabNotViewable;
    pthread_mutex_lock(&g_host_handle_mutex);
    handle = host_handle_find(widget, IRIX_HOST_HANDLE_WIDGET);
    if (handle) {
        args[0] = handle->guest_address; args[1] = owner_events;
        args[2] = pointer_mode; args[3] = keyboard_mode; args[4] = time;
        (void)irix_host_abi_context_call(context, "_XmGrabKeyboard", args, 5,
                                         &result);
    }
    pthread_mutex_unlock(&g_host_handle_mutex);
    return (int)result;
}
int _XmGrabPointer(Widget widget, int owner_events, unsigned int event_mask,
                   int pointer_mode, int keyboard_mode, Window confine_to,
                   Cursor cursor, Time time)
{
    IrixHostAbiContext *context; IrixHostOpaqueHandle *handle;
    uint32_t args[8], result = 0;
    if (!context_default(&context)) return GrabNotViewable;
    pthread_mutex_lock(&g_host_handle_mutex);
    handle = host_handle_find(widget, IRIX_HOST_HANDLE_WIDGET);
    if (handle) {
        args[0] = handle->guest_address; args[1] = owner_events;
        args[2] = event_mask; args[3] = pointer_mode; args[4] = keyboard_mode;
        args[5] = confine_to; args[6] = cursor; args[7] = time;
        (void)irix_host_abi_context_call(context, "_XmGrabPointer", args, 8,
                                         &result);
    }
    pthread_mutex_unlock(&g_host_handle_mutex);
    return (int)result;
}

static void host_xm_shadow_default(const char *api, Widget widget, int offset,
                                   XrmValue *value)
{
    IrixHostAbiContext *context; IrixHostOpaqueHandle *handle;
    IrixGuestAddr mark, guest_value = 0, guest_pixel = 0;
    uint32_t args[3], pixel = 0;
    static __thread Pixel result_pixel;
    if (!value || !context_default(&context)) return;
    pthread_mutex_lock(&g_host_handle_mutex);
    handle = host_handle_find(widget, IRIX_HOST_HANDLE_WIDGET);
    mark = context_arena_mark(context);
    if (handle && !irix_host_abi_context_copy_bytes(context, "\0\0\0\0", 4,
                                                      &guest_pixel) &&
        !irix_host_abi_context_copy_bytes(context, "\0\0\0\0\0\0\0\0", 8,
                                           &guest_value)) {
        irix_guest_write32be(&context->runtime, guest_value, 4);
        irix_guest_write32be(&context->runtime, guest_value + 4, guest_pixel);
        args[0] = handle->guest_address; args[1] = (uint32_t)offset;
        args[2] = guest_value;
        (void)irix_host_abi_context_call(context, api, args, 3, NULL);
        (void)irix_host_abi_context_read32be(context, guest_pixel, &pixel);
        result_pixel = pixel; value->size = sizeof(result_pixel);
        value->addr = (XPointer)&result_pixel;
    }
    context_arena_rewind(context, mark);
    pthread_mutex_unlock(&g_host_handle_mutex);
}
void _XmTopShadowColorDefault(Widget w, int o, XrmValue *v)
{ host_xm_shadow_default("_XmTopShadowColorDefault", w, o, v); }
void _XmBottomShadowColorDefault(Widget w, int o, XrmValue *v)
{ host_xm_shadow_default("_XmBottomShadowColorDefault", w, o, v); }

void _XmDestroyParentCallback(Widget widget, XtPointer client_data,
                              XtPointer call_data)
{
    IrixHostAbiContext *context; IrixHostOpaqueHandle *handle;
    uint32_t args[3];
    (void)client_data; (void)call_data;
    if (!context_default(&context)) return;
    pthread_mutex_lock(&g_host_handle_mutex);
    handle = host_handle_find(widget, IRIX_HOST_HANDLE_WIDGET);
    if (handle) {
        args[0] = handle->guest_address; args[1] = 0; args[2] = 0;
        (void)irix_host_abi_context_call(context, "_XmDestroyParentCallback",
                                         args, 3, NULL);
    }
    pthread_mutex_unlock(&g_host_handle_mutex);
}

static IrixHostOpaqueHandle *host_clipboard_app(Display *display,
                                                 IrixHostAbiContext **context)
{
    IrixHostOpaqueHandle *app;
    if (!context_default(context)) return NULL;
    app = host_app_find_by_display(display);
    return app;
}
static int host_clipboard_scalar(const char *api, Display *display,
                                 Window window, uint32_t tail)
{
    IrixHostAbiContext *context; IrixHostOpaqueHandle *app;
    uint32_t args[3], result = XmClipboardFail;
    pthread_mutex_lock(&g_host_handle_mutex);
    app = host_clipboard_app(display, &context);
    if (app) {
        args[0] = app->guest_display_address; args[1] = window; args[2] = tail;
        (void)irix_host_abi_context_call(context, api, args, 3, &result);
    }
    pthread_mutex_unlock(&g_host_handle_mutex);
    return (int)result;
}
int XmClipboardLock(Display *d, Window w)
{ return host_clipboard_scalar("XmClipboardLock", d, w, 0); }
int XmClipboardUnlock(Display *d, Window w)
{ return host_clipboard_scalar("XmClipboardUnlock", d, w, 0); }
int XmClipboardEndCopy(Display *d, Window w, long id)
{ return host_clipboard_scalar("XmClipboardEndCopy", d, w, (uint32_t)id); }

int XmClipboardStartCopy(Display *display, Window window, XmString label,
                         Time timestamp, Widget widget,
                         XmCutPasteProc callback, long *itemid)
{
    IrixHostAbiContext *context; IrixHostOpaqueHandle *app, *wh;
    IrixGuestAddr mark, guest_label = 0, guest_item = 0;
    uint32_t args[6], result = XmClipboardFail, id = 0;
    (void)callback; /* Native callback trampolines are implemented separately. */
    if (itemid) *itemid = 0;
    if (!itemid) return XmClipboardFail;
    pthread_mutex_lock(&g_host_handle_mutex);
    app = host_clipboard_app(display, &context);
    wh = host_handle_find(widget, IRIX_HOST_HANDLE_WIDGET);
    mark = app ? context_arena_mark(context) : 0;
    if (app && wh && !host_motif_xmstring_guest(label, context, &guest_label) &&
        !irix_host_abi_context_copy_bytes(context, "\0\0\0\0", 4,
                                           &guest_item)) {
        args[0] = app->guest_display_address; args[1] = window;
        args[2] = guest_label; args[3] = timestamp;
        args[4] = wh->guest_address; args[5] = 0;
        /* N32's seventh argument is the item-id output pointer. */
        args[6] = guest_item;
        (void)irix_host_abi_context_call(context, "XmClipboardStartCopy", args,
                                         7, &result);
        (void)irix_host_abi_context_read32be(context, guest_item, &id);
        *itemid = (long)(int32_t)id;
    }
    if (app) context_arena_rewind(context, mark);
    pthread_mutex_unlock(&g_host_handle_mutex);
    return (int)result;
}

int XmClipboardCopy(Display *display, Window window, long itemid, char *format,
                    XtPointer buffer, unsigned long length, long private_id,
                    long *dataid)
{
    IrixHostAbiContext *context; IrixHostOpaqueHandle *app;
    IrixGuestAddr mark, guest_format = 0, guest_buffer = 0, guest_out = 0;
    uint32_t args[8], result = XmClipboardFail, id = 0;
    if (dataid) *dataid = 0;
    if (!buffer || length > 16u * 1024u * 1024u || !dataid) return XmClipboardFail;
    pthread_mutex_lock(&g_host_handle_mutex);
    app = host_clipboard_app(display, &context);
    mark = app ? context_arena_mark(context) : 0;
    if (app && !irix_host_abi_context_copy_string(context, format,
            &guest_format) && !irix_host_abi_context_copy_bytes(context, buffer,
            length, &guest_buffer) && !irix_host_abi_context_copy_bytes(context,
            "\0\0\0\0", 4, &guest_out)) {
        args[0]=app->guest_display_address; args[1]=window; args[2]=(uint32_t)itemid;
        args[3]=guest_format; args[4]=guest_buffer; args[5]=(uint32_t)length;
        args[6]=(uint32_t)private_id; args[7]=guest_out;
        (void)irix_host_abi_context_call(context, "XmClipboardCopy", args, 8,
                                         &result);
        (void)irix_host_abi_context_read32be(context, guest_out, &id);
        *dataid = (long)(int32_t)id;
    }
    if (app) context_arena_rewind(context, mark);
    pthread_mutex_unlock(&g_host_handle_mutex);
    return (int)result;
}

int XmClipboardInquireLength(Display *display, Window window, char *format,
                             unsigned long *length)
{
    IrixHostAbiContext *context; IrixHostOpaqueHandle *app;
    IrixGuestAddr mark, guest_format=0, guest_length=0;
    uint32_t args[4], result=XmClipboardFail, n=0;
    if (length) *length = 0;
    if (!length) return XmClipboardFail;
    pthread_mutex_lock(&g_host_handle_mutex);
    app = host_clipboard_app(display, &context);
    mark = app ? context_arena_mark(context) : 0;
    if (app && !irix_host_abi_context_copy_string(context, format, &guest_format) &&
        !irix_host_abi_context_copy_bytes(context,"\0\0\0\0",4,&guest_length)) {
        args[0]=app->guest_display_address; args[1]=window;
        args[2]=guest_format; args[3]=guest_length;
        (void)irix_host_abi_context_call(context,"XmClipboardInquireLength",args,4,&result);
        (void)irix_host_abi_context_read32be(context,guest_length,&n); *length=n;
    }
    if (app) context_arena_rewind(context,mark);
    pthread_mutex_unlock(&g_host_handle_mutex);
    return (int)result;
}

int XmClipboardRetrieve(Display *display, Window window, char *format,
                        XtPointer buffer, unsigned long length,
                        unsigned long *outlength, long *private_id)
{
    IrixHostAbiContext *context; IrixHostOpaqueHandle *app;
    IrixGuestAddr mark, guest_format=0, guest_buffer=0, guest_length=0,
                  guest_private=0;
    uint32_t args[7], result=XmClipboardFail, n=0, id=0; unsigned long i;
    if (outlength) *outlength=0;
    if (private_id) *private_id=0;
    if (!buffer || length > 16u*1024u*1024u || !outlength || !private_id)
        return XmClipboardFail;
    pthread_mutex_lock(&g_host_handle_mutex);
    app=host_clipboard_app(display,&context); mark=app?context_arena_mark(context):0;
    if (app && !irix_host_abi_context_copy_string(context,format,&guest_format) &&
        !irix_host_abi_context_copy_bytes(context,buffer,length,&guest_buffer) &&
        !irix_host_abi_context_copy_bytes(context,"\0\0\0\0",4,&guest_length) &&
        !irix_host_abi_context_copy_bytes(context,"\0\0\0\0",4,&guest_private)) {
        args[0]=app->guest_display_address; args[1]=window; args[2]=guest_format;
        args[3]=guest_buffer; args[4]=(uint32_t)length; args[5]=guest_length;
        args[6]=guest_private;
        (void)irix_host_abi_context_call(context,"XmClipboardRetrieve",args,7,&result);
        (void)irix_host_abi_context_read32be(context,guest_length,&n);
        (void)irix_host_abi_context_read32be(context,guest_private,&id);
        *outlength=n; *private_id=(long)(int32_t)id;
        if (n > length) n=(uint32_t)length;
        for (i=0;i<n;++i) ((uint8_t*)buffer)[i]=irix_guest_read8(&context->runtime,guest_buffer+(uint32_t)i);
    }
    if (app) context_arena_rewind(context,mark);
    pthread_mutex_unlock(&g_host_handle_mutex);
    return (int)result;
}

void XmMenuPosition(Widget menu, XButtonPressedEvent *event)
{
    IrixHostAbiContext *context;
    IrixHostOpaqueHandle *handle;
    IrixGuestAddr mark, guest_event = 0;
    uint32_t args[2];
    if (!event || !context_default(&context)) return;
    pthread_mutex_lock(&g_host_handle_mutex);
    handle = host_handle_find(menu, IRIX_HOST_HANDLE_WIDGET);
    mark = context_arena_mark(context);
    if (handle && !irix_host_abi_context_copy_bytes(context, event,
            sizeof(XEvent), &guest_event) &&
        !irix_x11_image_write_n32_event(&context->runtime, 0, guest_event,
                                        (const XEvent *)event)) {
        args[0] = handle->guest_address; args[1] = guest_event;
        (void)irix_host_abi_context_call(context, "XmMenuPosition", args, 2, NULL);
    }
    context_arena_rewind(context, mark);
    pthread_mutex_unlock(&g_host_handle_mutex);
}

#undef HOST_MOTIF_WIDGET_GET
#undef HOST_MOTIF_WIDGET_SET1

