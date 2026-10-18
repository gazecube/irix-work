# Host ABI bridge for translated IRIX libraries

## Current state

`build_core_stack.py` translates the N32 `libXt.a`, `libXm.a`, `libSgm.a`,
`libvk.a`, `libMrm.a`, and `libUil.a` archives into native ELF shared objects.
The translated C entry points still have the internal signature
`void (IrixGuestRuntime *, IrixGuestCpu *)`. The legacy generated-core DSOs
retain original-name link aliases, but those are not host-callable APIs. The
native32 build omits such aliases. It now adds host-C-ABI Xt wrappers and
their shared private context to `libXt.so`; `libXm.so` exports Motif entry
points as i386 GOT-based tail thunks into that context owner. A native 32-bit
shell/label smoke client links to these DSO symbols and creates, realizes, and
destroys its widgets.

The translated code expects guest addresses and big-endian guest memory even
when the generated ELF container is x86-64. It must not be called by casting a
host function pointer or by treating host `Widget`, `XmString`, `Arg`, resource,
or callback storage as guest structures. Some guest-runtime bridge functions
are currently resolved by runner-exported bridge symbols; a standalone host
link also needs those bridge dependencies arranged explicitly.

The wrapper implementation lives in `runtime/IrixHostAbiContext.c`.
It maps the generated core image, the N32 libc/X11/Xt/Motif library images, a
guest stack and heap, initializes the host bridges, registers the synthetic
import slots, and installs the six split-library translated registries. It is
deliberately separate from `AppRunner.c`. Host-callable wrappers currently
cover `XmStringCreateLtoR`/`XmStringFree` plus a resource-free Widget lifecycle:
`XtAppInitialize`, `XmCreateLabel`, `XtManageChild`, `XtRealizeWidget`,
`XtDestroyWidget`, and `XtDestroyApplicationContext`. XmString and Widget/app
context results use stable opaque host handles; strings and argv are copied
into N32 memory. The native32 build compiles the context into `libXt.so`, while
`IrixNative32XmExports.c` provides public Motif symbols in `libXm.so`; it does
not add a standalone ABI context DSO. Common Motif factory wrappers now cover
arrow, cascade, drawn, file-selection, form, frame, label, message, option,
prompt, push, question, radio, row-column, scale, scrolled, separator, and
toggle constructors. Xt/Motif resource, callback, and native class lifecycle
coverage remains incomplete. Native32 still depends on `libirixre`
for guest-memory marshalling and dispatch, and the Xt translation-table path
retains a MIPS compatibility fallback.

## Required shape of the conversion

Keep the generated `irix_recomp_*` functions as private implementation entry
points. Export original API names from the per-library shared objects using
real host-C-ABI wrappers. A wrapper must:

1. Obtain a process-shared guest context with the translated core image mapped
   at the addresses recorded in `generated/image/link.json` and with the
   translated library registries installed.
2. Marshal each host argument according to that API's declared type and the
   IRIX N32 layout. Copy strings and scalar arrays where appropriate; convert
   structured objects field by field; never cast host structures into guest
   memory.
3. Place converted arguments in the N32 call state, call the guest entry by
   its address, and detect guest traps before converting the return value.
4. Preserve object identity across API calls with explicit host-to-guest and
   guest-to-host handle maps. Callback-taking APIs additionally need stable
   callback trampolines that convert callback arguments in both directions.
5. Define ownership and lifetime rules for every returned object and temporary
   guest allocation. Repeated calls, callbacks, and calls from multiple host
   threads must not share a mutable CPU state unsafely.

The wrappers belong in the corresponding library DSO, or in a small source
unit linked into that DSO. Do not add a separate public ABI-context DSO. Keep
registration and translated entry symbols
available internally to the shared context. Generate aliases only for global
function symbols selected by the synthetic linker's `globals` map; local
functions often repeat names such as `Initialize` and must not become public
exports.

## Incremental implementation and checks

Start with a narrow API whose arguments and result ownership are understood,
then add the APIs needed by a real native client. For each API, record the
IRIX header declaration, N32 argument/result representation, host wrapper
declaration, conversion rules, and ownership behavior. Test both link-time
visibility and runtime semantics: a program that merely links against an
alias does not validate ABI conversion.

The current alias layer can be checked with `nm -D --defined-only` and a
link-only test program. A converted wrapper is not complete until a native
client can call it, inspect/use its result through the matching API, release
that result, and run under memory/error instrumentation without a guest trap
or ownership leak. The standalone `XmString` probe must use a literal charset
string for now: the IRIX header's `XmSTRING_DEFAULT_CHARSET` macro references
`_XmStrings`, which is not yet exported by the generated DSO.

The generated DSOs depend on `libirixre.so.1`, whose runner callback symbols
are normally supplied by the libc and X11 bridge archives. A standalone
link-only client therefore also needs those bridge objects and their host
dependencies; linking only `-lXm -lXt -lirixre` currently fails on unresolved
`irix_libc_guest_malloc` and `irix_x11_primary_create_gc`. A local link probe
passed when those archives were linked whole-archive with X11, Xt, Xext, Xpm,
crypt, and math. This proves name resolution only, not native-call safety.

## Netscape 32-bit link torture test

The existing XFE object tree is 32-bit i386, so the translated libraries
must also be built as i386 host ELF. Configure a separate CMake build with
`-m32`, disable Unicorn if its 32-bit library is unavailable, and make sure
the host X11, Xt, and Xpm libraries resolve from `/usr/lib32`. The generated
core builder receives a compiler executable rather than CMake's compile
flags, so use a compiler wrapper that adds `-m32` there too. The CMake GUI
library search uses `CMAKE_C_SIZEOF_DATA_PTR`; the C++ compiler may otherwise
make `CMAKE_SIZEOF_VOID_P` report the wrong host width in this mixed toolchain.

With the generated Xt/Motif DSOs and `libirixhostabi` first in the link order,
an isolated `netscape-recomp-export` target linked successfully from the
existing Netscape XFE objects and static application archives. `readelf` and
`ldd -r` confirmed an i386 executable, the six generated IRIX-shaped DSOs,
`libirixhostabi.so.1`, `libirixre.so.1`, and no unresolved dynamic symbols.
The normal `netscape-export` and `mozilla-export` outputs were left untouched.

This was strictly a link-surface experiment. Netscape's Linux build expects
Motif 2.x private `Xme*` entry points, and the generated SGI Motif 1.2 stack
does not provide them. The test therefore used temporary link-only shims for
26 missing functions and placeholder storage for 50 guest global-data
symbols; it also needed the existing static archives grouped to satisfy their
cross-library references. Those placeholders are not valid widget classes,
resources, or callback bridges, so the test executable must not be run. The
result shows that the overall ELF/link integration can be made to work, while
also providing a concrete list of compatibility and data-marshalling work
that remains before Netscape can use the stack at runtime.

## Netscape's native IRIX Motif target

The Linux XFE objects are not a reliable guide to the Motif API surface used by
Netscape on IRIX. The source's `config/IRIX.mk` selects the native `-lXm` and
`-lXt` libraries and, for the SGI compiler, `-32` (the file documents O32 as
the default and leaves its N32 define commented out). In `cmd/xfe/xfe.c`,
`XmObjectAtPoint` is selected only when `XmVersion >= 2000`; with the SGI
Motif 1.2 headers the source instead selects `_XmInputInGadget`.

A host-GCC compile probe of the Xfe and Microline sources using `-m32`,
`-D__sgi`, and the SGI Motif 1.2 headers successfully rebuilt the checked
widget objects. Their Linux Motif 2.x imports map to names supplied by SGI
Motif 1.2: for example `XmeDrawArrow` to `_XmDrawArrow`,
`XmeDrawShadows` to `_XmDrawShadows`, the `Xme*Pixels` helpers to
`_Xm*Pixels`, `XmeConfigureObject` to `_XmConfigureObject`, and the traversal,
gadget, clear-border, drag-icon, and font-list helpers to their 1.2 `_Xm*`
forms. The generated i386 `libXm.so` exports the checked legacy entry points.
`CallProcs.c` and `GcUtil.c` also compile against 1.2 headers and use
`_XmGetDefaultFontList` and `_XmFontListGetDefaultFont` instead of the Linux
objects' Motif 2.x render-table helper names.

This is evidence that the Linux link failure is largely caused by compiling
against Motif 2.x headers, not evidence that the complete browser has been
rebuilt or linked for IRIX. The probe used host GCC and cannot validate SGI
MIPS code generation or IRIX system headers; a full native target build still
needs an IRIX compiler/toolchain and a complete rebuild of all platform
objects. Keep the Linux link-only stubs out of that build and check any
remaining imports against the actual IRIX target's symbol table.

## Native Netscape widget API map

The map below was collected from the Netscape XFE and XP C/C++ sources, which
are shared by the IRIX target. Counts are source call-site occurrences, not
dynamic symbol-import counts, and can include conditional or helper code. The
current generated Xt/Xm DSOs export these function names, but those exports
still have guest-entry signatures; none of the following are thereby safe
native calls.

| Native API family | Frequent source calls | Host-to-guest conversion required |
| --- | --- | --- |
| Application and shell setup | `XtAppInitialize` (21), `XtVaAppInitialize` (1), `XtVaAppCreateShell` (2), `XtCreateApplicationContext` (1) | Convert `argc`/`argv` in/out, application name/class, option records, fallback-resource strings, `Display *`, and app-context handles. The initialize calls are variadic and need a resource-list policy. |
| Widget construction | `XtVaCreateManagedWidget` (190), `XtCreateWidget` (37), `XtCreateManagedWidget` (22), `XtVaCreateWidget` (19), `XmCreateLabelGadget` (102), `XmCreatePushButtonGadget` (72), `XmCreateForm` (57), `XmCreateToggleButtonGadget` (56), `XmCreateRowColumn` (27) | Parent and return `Widget` values need stable opaque handle maps. Class pointers need class handles. `Arg` names and values require per-resource conversion; `XtVa*` lists require a host-side varargs parser or explicit supported-resource schema. Motif factory calls also need their `name` and `ArgList` copied. |
| Widget state and geometry | `XtVaSetValues` (714), `XtVaGetValues` (322), `XtSetValues` (105), `XtGetValues` (18) | Convert resource names and values by resource type in both directions, including pointer-valued resources and output storage. Variadic forms need careful parsing and must not assume every value is an integer or pointer. |
| Hierarchy and visibility | `XtManageChild` (367), `XtManageChildren` (55), `XtUnmanageChild` (79), `XtRealizeWidget` (37), `XtPopup` (37), `XtPopdown` (10), `XtDestroyWidget` (62) | Translate each widget handle and child arrays; preserve parent/child identity and destroy semantics. Shell popup arguments are scalar but the shell handle remains opaque. |
| Callbacks and event loop | `XtAddCallback` (481), `XtRemoveCallback` (37), `XtAppAddTimeOut` (24), `XtAppAddInput` (5), `XtAppMainLoop` (22), `XtAppProcessEvent` (8), `XtAppNextEvent` (5), `XtDispatchEvent` (6) | Callback functions and client data need stable trampolines and reverse conversion of callback widget/call-data. Timer/input IDs need host handles; event structures need field-by-field conversion. Reentrancy, lifetime, and thread rules must be explicit. |
| Common Motif factories | `XmCreateFrame` (12), `XmCreateScrolledList` (11), `XmCreateDialogShell` (9), `XmCreatePulldownMenu` (9), `XmCreateScrolledText` (8), `XmCreatePromptDialog` (7), `XmCreateMessageBox` (5), plus dialogs, menus, text, lists, and controls | Same Widget/Arg conversion as above. These factories create Motif-specific children and resources, so wrapper coverage must include the resource types and callbacks those widgets use. |

The smallest runtime milestone is a native client that creates an application
shell, makes one `XmCreateLabel` (or label gadget), manages it, realizes the
shell, and destroys it. That first vertical slice still needs a real solution
for application startup, Widget handles, resource-list marshaling, and shell
lifecycle. It can omit callback trampolines only if the probe registers no
callbacks. The browser itself is a much larger milestone: its callback and
varargs setter/getter traffic dominates the observed API surface.

The SGI target's `Arg` layout comes from its IRIX Xt headers: `Arg` contains a
resource-name pointer and `XtArgVal`; the shipped IRIX header defines
`XtArgVal` as `long`. In N32, pointers and `long` are 32-bit. A 32-bit native
client therefore has a layout close to the guest ABI, but still needs pointer
and ownership conversion. A 64-bit host client has wider pointers and `long`,
so it must use a separate host-facing declaration/layout and convert each
argument into N32 storage. Never copy a host `Arg[]` into guest memory as a
block.

For the inventory, a source scan of `cmd/xfe` and `lib/xp` found 2,728 call
sites across 77 selected Xt/Motif API names. The generated i386 `libXt`
and `libXm` export the representative lifecycle symbols (`XtAppInitialize`,
`XtCreateWidget`, `XtManageChild`, `XtRealizeWidget`, `XtAddCallback`,
`XtAppMainLoop`, `XmCreateLabel`, and the common factory functions). That is
an export-surface check only. The default `irix_host_abi_context_test` exercises
the XmString wrapper, while its opt-in widget probe exercises translated guest
calls rather than native Widget conversion.

The context's legacy `uint32_t` API now delegates argument placement to the
typed N32 scalar packer, preserving its previous 32-bit token behavior. New
wrappers can use `irix_host_abi_context_call_typed` to declare integer,
pointer, 64-bit, and floating-point inputs and scalar result types. The host
ABI context initializes and resolves the X11 bulk bridge and XImage bridge. The opt-in
`IRIX_HOST_ABI_RUN_WIDGET_PROBE=1` run passes both a guest-format probe and a
native host-C-ABI probe through nine-argument `XtAppInitialize`,
`XmCreateLabel`, `XtManageChild`, `XtRealizeWidget`, `XtDestroyWidget`, and
`XtDestroyApplicationContext`. The host probe exercises the opaque handle maps
and resolves those original API names from `libirixhostabi.so`. AppRunner-only
Motif redirects are skipped in this runtime. This is still a narrow lifecycle
smoke test: resource conversion and callback trampolines remain unimplemented.

## Isolated native Netscape startup investigation, 2026-10-17

The current experimental executable is
`/home/nekron/Projects/netscape-src/netscape/cmd/xfe/Linux7.2.9_x86_DBG.OBJ/netscape-recomp-export-hostxt`.
It is a separate relink of the existing `netscape-recomp-export` objects with
`--export-dynamic`, so the context can find its link-only Motif class slots
with `dlsym`. The normal `netscape-export`, installed browser, and normal
profile are separate. Use only `DISPLAY=:0.1`,
`HOME=/tmp/netscape-hostabi-visible-20261017`, and
`MOZILLA_HOME=/home/nekron/Projects/netscape-src/dist/netscape` for this test.
Do not pass `-irix-session-management`. The runtime search path is
`build-irix-re32b/recomp:build-irix-re32b/recomp/generated-core:/usr/lib32`
(expand the first two paths relative to the project root).

The context currently supplies minimal host class facades for Motif class
identity checks. Menu convenience constructors mark their returned host
records as RowColumn; the label constructor marks its record as Label.
These records are not a conversion of the complete Motif private ABI.
Native XFE/Microline class initialization, resources, private instance data,
callbacks, timers, and input registrations are still incomplete. Generic
`XtCreateWidget`/`XtVaCreateWidget` still substitute a guest BulletinBoard,
which causes real guest menu warnings when used for menu-entry classes.
Reaching a later assertion is startup progress, not a usable browser.

Confirmed startup fixes include cached host Screen lookup and inheritance,
clean failure for a null `XmFontListInitFontContext`, and a host
`XtManageChildren` wrapper that copies host widget handles into a big-endian
N32 pointer array. Without that wrapper the native browser called the
guest-entry alias with host arguments and crashed in translated Xt.

The docked taskbar failure was a guest trap in `XmManager.Initialize` at
`0x5018a5b0`: its SGI vendor-extension pointer at instance offset `0xe8` was
null. The host context now calls its translated `_XmInitializeExtensions`
after registering and binding the translated libraries. The original-DSO
bootstrap/root-wrapper repairs in `IrixGuest.c` exclude
`IRIX_GUEST_RUNTIME_HOST_ABI`; their desktop-runner behavior is retained.
Both existing widget probes pass after this change, and Netscape passes the
docked taskbar initialization and reaches bookmark menubar construction.
The next observed assertion was `XfeVisual(visual_widget) != NULL` in
`XfeMenuCreatePulldownPane` (`MenuUtil.c:537`). XFE reads `ShellRec.shell.visual`
directly, so a host record containing only `WidgetRec` is insufficient even
when `XtVaGetValues(..., visual, ...)` can return a visual.

Diagnostic output for that last validated startup is
`/tmp/netscape-hostabi-translated-init.log`. The opt-in context probe now covers
three nested Manager widgets and direct reads of native application/popup
shell visuals, in addition to the original shell/label lifecycle. These checks
pass, as do `irix_mips_call_boundary_test` and `irix_xt_gc_lifetime_test` in
the i386 build.

The shell facade now allocates at least `ApplicationShellRec`, populates the
host visual/depth/colormap, and updates parent/child references when a
constructor promotes its initial Core record to a larger concrete record.
Netscape still exposed a separate link problem: its temporary data stub
defined `topLevelShellClassRec` and `vendorShellClassRec` as four-byte arrays.
Host Xt's class pointers were interposed onto those arrays, causing invalid
widget-size reads and failed shell-record allocation. The isolated
`netscape-recomp-export-hostxt` relink removes all twelve stub definitions
provided by `/usr/lib32/libXt.so.6` and adds `-l:libXt.so.6` explicitly. Its
Motif placeholders remain experimental; removing Xt placeholders does not
complete the Motif private-data conversion.

The `hostxt` executable links successfully. Its dynamic symbol table now
imports `topLevelShellClassRec`, `vendorShellClassRec`, and `XtStrings` from
host Xt instead of defining four-byte executable placeholders. Startup takes
an additional taskbar-button path and fails earlier than the menubar visual
assertion: `XFE_DropBase` (`DragDrop.cpp:1330`) directly calls
`irix_recomp_o0097_s0134_XmDropSiteRegister` with native arguments. Breaking
at that entry shows an intact native stack; continuing corrupts it. The next
required host wrapper is `XmDropSiteRegister`, with related Update/Unregister
coverage. Its Arg resources include native `dragProc`/`dropProc` callbacks,
activity/type scalars, and later Atom target arrays. Those need typed
conversion and callback trampolines; do not pass the native function pointers
or Atom arrays into N32 memory and do not replace registration with a no-op
to claim browser support. The menubar visual assertion has not yet been
revisited end to end in this corrected link. Latest startup output is
`/tmp/netscape-hostabi-hostxt-run.log`. Copies of the compatibility sources,
startup/probe logs, dynamic-dependency check, and a relink recipe are retained
under `build-irix-re32b/recomp/netscape-hostabi-probe/` so the next session
does not depend on temporary-file survival.


### Native DnD callbacks and XFE class identity (2026-10-17 follow-up)

The previous DropSite frontier is now crossed. `IrixHostAbiDropSite.inc`
provides typed Register/Update/Unregister wrappers, guest Atom arrays and
native drag/drop callback trampolines. Callback tests query the callback
address back through translated `XmDropSiteRetrieve`, enter through the
guest dispatcher, check event and mutable result conversion, and reenter
XmString creation/free from the native callback. Separate stack slots and
recursive context/handle locks support this reentry. Full drag transfers
have not been tested. Native clients now use host headers rather than the
IRIX include directories propagated by the native interface target.

XtSetValues/XtVaSetValues convert a bounded set of scalar resources,
userData and tracked XmString labelString handles; unknown resources are
rejected. XmStringCreate, Copy, Compare and ByteCompare now have native
wrappers. The native probe covers these paths. Resource coverage remains
incomplete; rejection messages during startup are real limitations.

The isolated relink recipe now declares the five Motif ClassRec symbols
using host private-header types instead of four-byte arrays, and declares
class pointer symbols as WidgetClass. The context copies the CoreClassPart
facade into exported records and uses those same record addresses for the
WidgetClass pointers. This repairs the XFE superclass stopping condition:
XFE traverses to `&xmPrimitiveClassRec` and compares against
`xmPrimitiveWidgetClass`; they must identify the same record. This supplies
storage and identity, not completed native Motif lifecycle methods.

Latest isolated browser test clears `_XfePrimitivePrepareComponents`' class
walk and enters real XFE Label.PrepareComponents. It then crashes in the
unwrapped translated `XmStringExtent`, called with native font-list/string
arguments. Native subclass initialize methods and resources are still not
run by the generic facade creation path; its zero-initialized private fields
include the label font list. Implement font-list ownership/conversion and
native subclass lifecycle rather than inventing metrics or ignoring methods.
The browser has NOT reached fe_EventLoop or a usable navigator window.

Latest logs: `netscape-hostabi-probe/class-gdb.log`, `class-run.log`, and
`class-widget-test.log` in the build recomp directory. The three focused
CTest cases (host ABI context, MIPS call boundary, Xt GC lifetime) and the
opt-in DISPLAY=:0.1 widget/callback probe pass. Tests use the isolated HOME
and executable above, never the installed browser. Existing runner coverage
is limited to these checks; do not claim a full desktop regression pass.

### Netscape display/list prefix and event-loop frontier (2026-10-17)

The original IRIX Motif 1.2 private headers are present at
`../../holyshit/usr/Motif-1.2/include/Xm/DisplayP.h` and `ListP.h`. They confirm
that `XmDisplayPart.userGrabbed` is the third byte of the part and that
`XmListPart.itemCount` follows its `items` pointer. The host facade keeps
translated widgets opaque and allocates only the native prefixes Netscape
directly reads. Do not put the complete SGI Motif include tree ahead of host
headers for the bridge: `XmP.h` defines `XtName` and `XtIsManaged` as macros,
colliding with exported wrapper functions, and SGI-only extension types are
not declared in this hybrid translation unit's include order.

`irix_host_abi_context_call_typed` now clears a trapped runtime state only at
the next outermost call boundary, so one failed guest call does not poison
later independent calls. Trap diagnostics are capped to prevent an event-loop
retry from filling the log. The focused N32 argument and host ABI context
tests pass after this change.

The isolated `netscape-recomp-export-hostxt` now maps its Navigator 4.05 shell
and remains alive through the short DISPLAY=:0.1 probe, but the client area is
still blank. This is not a successful page load. Startup still reports
unconverted native resources, skipped callback registrations, an XfeDashBoard
child rejection, and translated Xt/Motif calls that trap (including
`XmGetXmDisplay`); X errors reference Window 0. The next work must repair the
guest/host widget class and lifecycle bridge, plus callback/resource
conversion, before Netscape can render or navigate. Latest visual capture:
`/tmp/netscape-hostabi-clean-boundaries.png`; startup log:
`/tmp/netscape-hostabi-clean-boundaries.log`.

### Updated Netscape startup frontier after relinking (2026-10-17)

The isolated executable was relinked against the current `libirixhostabi.so`;
the prior `class-gdb.log` and `class-run.log` predate that library build and
are not the latest runtime evidence. The fresh run on `DISPLAY=:0.1` uses
`HOME=/tmp/netscape-hostabi-visible-20261017b` and aborts during bookmark
outliner construction. Netscape's native `XmLCreateGrid` calls the intercepted
`XtCreateWidget`, which currently returns an opaque facade backed by a guest
BulletinBoard class. Microline's native `WidgetToGrid` then rejects that object
as not an `XmLGrid`; `XFE_Outliner::applyDelta` subsequently asserts because
the resulting column widths are zero. This is a class-ownership/lifecycle
boundary, not an instruction-lowering failure. The browser does not reach a
usable Navigator page.

Adding Microline to the translated N32 closure remains a possible route. The
complete Motif development headers are under
`holyshit/devl/usr/Motif-1.2/include`, not only the installed subset under
`holyshit/usr/Motif-1.2/include`. With `-D__sgi`, those headers provide
`XmRenderShadowsProc` and `visual_state_array` from `Xm/XmP.h`, plus
`_SgClassExtensionRec` from `Xm/SgiP.h`. Clang supports the N32 MIPS target;
all five Microline C units pass syntax checking with these include paths and
the IRIX preprocessor defines. A test `Grid.c` object was also emitted as
big-endian N32 MIPS III with `-march=mips3`.

The IRIX installed tree supplies `standards.h` and `sgidefs.h` at
`IRIX-TREE/usr/include`; use those as the first IRIX system include directory.
With that include path, the full Motif 1.2 development headers from
`holyshit/devl/usr/Motif-1.2/include`, the development include tree, and
`-D__sgi`, all five Microline C units (`Grid.c`, `GridUtil.c`, `Tree.c`,
`XmL.c`, and `Folder.c`) pass Clang N32 MIPS III syntax checking without the
temporary compatibility headers. This confirms the headers needed for a real
Microline cross-compile are available in the extracted installset; it does
not yet establish that the resulting objects link or run in Netscape.

The relinked executable log is `/tmp/netscape-hostabi-current.log`; its
captured desktop is `/tmp/netscape-hostabi-current.png`. The full 12-test CTest
suite passed before this isolated relink/run. The run terminated with
SIGABRT at `assert: line 1441, file Outliner.cpp`; no browser process remains.

### Direct native32 DSO relink checkpoint (2026-10-17)

The existing link recipe was relinked to `generated-core-native32/libXm.so`
and `libXt.so`, with `-lirixhostabi` removed. The first attempt reported 136
unresolved Motif imports. Adding the factory wrappers listed above rebuilt the
native32 DSOs and reduced that list to 110. The remaining imports include
list/text APIs, clipboard, input-method, drawing, drag-and-drop, and private
Motif helpers. They are genuine API coverage gaps; no no-op or external ABI
shim was used to conceal them. The direct browser link therefore remains
incomplete. The native32 DSO build and six focused ABI/lowering/lifetime CTests
pass after these additions. A separate GUI process on `DISPLAY=:0.1` is using
the older `generated-core` plus `libirixhostabi.so` path and was left
untouched. The failed direct-link output and command are saved as
`netscape-hostabi-probe/direct32-link.log` and
`netscape-hostabi-probe/direct32-link-command.json` under the build tree.
