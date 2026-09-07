# ViewKit architecture reverse-engineering notes

Target: IRIX 6.5.0 ViewKit behavior as consumed by Clogin and the desktop libraries.

## Evidence policy

Primary evidence:

1. IRIX 6.5.0 binaries, symbols, disassembly, DWARF, and installed resources.
2. IRIX desktop libraries which import or subclass ViewKit classes.

Secondary reference only:

- MaXX Interactive Desktop ViewKit headers and libVk implementation.

MaXX is useful for public class names, declarations, and modern behavior, but it is not treated as proof of IRIX 6.5.0 internals.

## Confirmed consumers in the recovered IRIX library set

The following recovered IRIX desktop libraries link directly against libvk.so.1:

- libimdIconPanel.so
- libimdXutil.so
- libimdApp.so
- libImdURLModel.so
- libdesktopIcon.so
- libdesktopUtil.so

This makes libimdXutil.so and libimdIconPanel.so especially useful cross-reference targets because both retain rich C++ symbol/debug information and sit directly underneath Clogin.

### libimdXutil.so as the primary ViewKit specimen

Recovered batch metadata gives libimdXutil.so:

- ELF32 MSB MIPS N32 MIPS-III
- debug_info present, not stripped
- 565 dynamic exports/imports on the export side and 314 imports
- 689 DWARF pubnames
- direct dependencies on libvk.so.1, libXm.so.1, libXt.so, libX11.so.1, and the Imd utility libraries
- recoverable class VkImdApp with 11 published/debug names

This is currently the strongest IRIX-side specimen for recovering the VkApp application-lifecycle contract. Unlike Clogin, it exposes a ViewKit-derived application class while retaining extensive debug metadata.

### libimdIconPanel.so as the ViewKit/Motif boundary specimen

libimdIconPanel.so directly depends on:

- libvk.so.1
- libSgm.so.1
- libXm.so.1
- libXt.so
- libX11.so.1
- libimdFtrIcon.so
- libimdXutil.so

That dependency shape is important for Clogin archaeology. It gives us a concrete place to distinguish behavior supplied by ViewKit from behavior supplied by the custom Imd widget/Sgm/Motif stack. In particular, the user-panel background/resource path should be traced here rather than attributed to ViewKit by assumption.

## High-value recovered ViewKit-facing classes

From the recovered IRIX desktop binaries:

- VkImdApp in libimdXutil.so
- URLPasswdDialog in libImdURLModel.so
- QuickFindDialog in libimdApp.so
- GetInfoDialog in libimdApp.so
- HostFindDialog in libimdApp.so

These provide real IRIX call sites for application lifecycle, component/window ownership, dialog management, callback registration, and resource handling.

## MaXX public header surface available for comparison

The MaXX installation includes a broad Vk header set, including:

- VkApp.h
- VkComponent.h
- VkSimpleWindow.h
- VkCallbackObject.h
- VkCallbackList.h
- VkComponentList.h
- VkDialogManager.h
- VkGenericDialog.h
- VkMenu.h and menu subclasses
- VkModel.h
- VkPeriodic.h
- VkPipe.h
- VkWindow.h

The headers are useful for naming and public API hypotheses, but each method or ownership rule must be checked against the IRIX binaries before being considered reconstructed original behavior.

## Clogin-specific ViewKit boundary already proven

The current Clogin reconstruction uses:

- PandoraComponent : VkComponent
- PandoraWindow : VkSimpleWindow

Binary work on PandoraComponentUI has already shown that the VkComponent base widget is an XmFrame containing a managed XmForm. This is an example of the preferred workflow: use ViewKit declarations as context, but recover actual widget ownership and construction order from the Clogin binary.

## Direct IRIX libvk.so.1 receipts

### VkComponent object shape

IRIX libvk.so.1 exports:

- __ct__11VkComponentGv at 0x0d636558
- __ct__11VkComponentGPCc at 0x0d63660c
- __dt__11VkComponentGv at 0x0d62f590
- installDestroyHandler__11VkComponentGv at 0x0d6369a0
- setDefaultResources__11VkComponentGP10_WidgetRecPCPc at 0x0d6366c8
- baseWidget__11VkComponentCGv at 0x0d62ff64

Constructor disassembly proves the following member locations for this IRIX build:

- +0x0c: duplicated component name string
- +0x10: base Widget, initialized to NULL
- +0x14: pointer to the +0x10 base-widget slot
- +0x18: separately allocated private state block of 0x70 bytes
- +0x1c: self pointer

The private block is initialized with two byte flags at private +0x04 and +0x05 set to 1, and private +0x08 set to 0. Their semantic names are not yet proven.

The constructor first invokes its base-class constructor, then installs the VkComponent vtable and initializes the fields above. This is binary evidence for the compact component object model rather than an inferred MaXX layout.

### VkComponent destroy handler

installDestroyHandler() at 0x0d6369a0 loads the widget at component +0x10 and calls an Xt callback-registration routine using the component pointer as client data. The resource/callback symbol itself still needs exact GOT resolution, but the behavior proves that destruction coupling is established directly on _baseWidget rather than through a separate ViewKit container object.

### VkSimpleWindow

IRIX libvk.so.1 exports:

- __ct__14VkSimpleWindowGPCcP3ArgUi at 0x0d639618
- __dt__14VkSimpleWindowGv at 0x0d62e7c4
- addView__14VkSimpleWindowGP10_WidgetRec at 0x0d63931c
- addView__14VkSimpleWindowGP11VkComponent at 0x0d63e210
- removeView__14VkSimpleWindowGv at 0x0d68236c
- mainWindowWidget__14VkSimpleWindowCGv at 0x0d62f160
- viewWidget__14VkSimpleWindowCGv at 0x0d682078
- build__14VkSimpleWindowGv at 0x0d6825c4
- show__14VkSimpleWindowGv at 0x0d63900c
- open__14VkSimpleWindowGv at 0x0d682404

The VkSimpleWindow constructor calls the VkComponent constructor first, installs its own vtable, initializes its window state, creates/configures the Motif main-window hierarchy, and registers window-manager/event callbacks directly against the resulting widgets.

The Widget overload of addView() stores the supplied view widget at object +0x34 before performing parent/hierarchy checks and attachment/setup work.

The VkComponent overload of addView() first obtains a Widget from the component, delegates to the Widget overload, and then invokes a virtual method on the component. The exact virtual slot name is still pending vtable resolution. This proves that VkSimpleWindow does not require a separate per-view ViewKit container; the component's own widget is the actual view inserted into the window.

### VkApp main-window relationship

IRIX libvk.so.1 exports setMainWindow__5VkAppGP14VkSimpleWindow at 0x0d62fcc0. Its complete implementation is two instructions:

    jr   $ra
    sw   $5, 0x64($4)

Therefore VkApp::_mainWindow is at +0x64 in this build, and setMainWindow() is literally a pointer assignment.

mainWindow__5VkAppCGv at 0x0d631520 first returns the pointer at +0x64 when non-NULL. If it is NULL, it falls back to the application's registered window list. This is important for Clogin: setting PandoraWindow as the main window is shallow ViewKit state, not a hidden ownership transfer.

## Initial archaeology targets

### VkComponent

Recover or cross-check:

- destructor sequence and exact private-state cleanup
- exact destroy callback symbol and callback target
- setDefaultResources implementation details
- className virtual dispatch
- semantic names for the private-state flags

### VkSimpleWindow

Recover or cross-check:

- exact shell/main-window widget classes
- addView virtual callback slot
- show/hide/realize ordering
- destruction of contained VkComponent objects
- object-member names for +0x2c/+0x34 and related window fields

### VkApp / VkImdApp

Use libimdXutil.so as the first IRIX consumer target. Recover:

- application initialization order
- XtAppContext ownership
- display connection setup
- window registration
- event-loop entry
- fallback resource setup
- session-management hooks

### Callback layer

Cross-check VkCallbackObject / VkCallbackList against IRIX consumers to recover:

- callback record representation
- registration/removal semantics
- dispatch ordering
- whether callbacks are copied or referenced during notification

## Current reconstruction rule

Do not add ViewKit-like convenience behavior to the portable Clogin code solely because the MaXX headers expose a similarly named API. Before moving behavior into a reconstructed ViewKit layer, require one of:

1. a matching IRIX libvk import/call site,
2. a derived-class call pattern in an IRIX consumer,
3. binary/debug evidence for the member or lifecycle rule.

This is especially important for ownership and destruction semantics, where a modern ViewKlass implementation may differ subtly from IRIX 6.5.0.

## Why this matters to Clogin

A better ViewKit reconstruction will let us distinguish three layers cleanly:

1. ViewKit lifecycle/component behavior.
2. Motif/Xt widget behavior.
3. Clogin-specific presentation and login logic.

That separation is important for avoiding portable convenience code in places where ViewKit already supplied the original behavior.

## Next binary work

The next pass should resolve the relevant VkComponent/VkSimpleWindow GOT entries and vtable slots, then trace VkImdApp in libimdXutil.so against the now-known VkApp object model.

For the panel-color issue, trace libimdIconPanel.so resource initialization separately. The theme-switch test showed that the current panel shade does not follow the selected MaXX desktop theme, so the custom Imd/Sgm resource path is now a higher-confidence suspect than global ViewKit theming.
