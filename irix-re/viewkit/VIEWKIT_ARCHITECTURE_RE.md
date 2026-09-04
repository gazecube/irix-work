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

## Initial archaeology targets

### VkComponent

Recover or cross-check:

- constructor/destructor call sequence
- _baseWidget ownership
- installDestroyHandler behavior
- setDefaultResources behavior
- widget destruction and component lifetime coupling
- className virtual dispatch

### VkSimpleWindow

Recover or cross-check:

- shell/main-window creation
- addView ownership semantics
- show/hide/realize ordering
- destruction of contained VkComponent objects

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

The first disassembly pass should focus on ViewKit imports visible from Clogin, libimdXutil.so, and libimdApp.so, then map those imports back to the public Vk declarations. Priority symbols/classes are VkComponent, VkSimpleWindow, VkApp, VkCallbackObject, and VkDialogManager.

For the panel-color issue, trace libimdIconPanel.so resource initialization separately. The theme-switch test showed that the current panel shade does not follow the selected MaXX desktop theme, so the custom Imd/Sgm resource path is now a higher-confidence suspect than global ViewKit theming.
