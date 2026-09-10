# displayIcon / showPicture reconstruction

Targets:

- `PandoraComponent::displayIcon()` at `0x10011b8c`
- `PandoraComponent::showPicture()` at `0x10011d10`

## displayIcon()

The function operates on the component's Imd icon panel/model rather than directly painting Motif labels or enumerating passwd entries.

Recovered flow:

1. Begin a blocked Imd panel update with `ImdViewPanelBeginBlockChanges`.
2. Iterate the component's already-built user/model collection at `PandoraComponent +0x50`.
3. For each model object, construct a `UserView` using the binary-proven constructor payload.
4. Add/adopt that view through the Imd panel/view interfaces at `PandoraComponent +0x84`.
5. Continue until the model iterator is exhausted.
6. End the blocked update with `ImdViewPanelEndBlockChanges`.

This proves the original icon screen is an Imd model/view pipeline, not a row of Motif push buttons and not a passwd-enumeration routine.

## Responsibility split now used by the reconstruction

The cleanroom C++ path now follows the recovered architecture:

```text
pandora::createIconList()
    -> passwd / NIS enumeration
    -> policy and directory filtering
    -> PandoraComponent::addUser()
    -> component user-model collection

PandoraComponent::displayIcon()
    -> iterate component user-model collection
    -> construct UserView instances
    -> PandoraLayout::addView()
    -> ImdIconPanel::adoptView()
```

The portable `PandoraUserModel` type is explicitly reconstructed storage for this boundary. Its name and C++ layout are not claims about the original 0x1c-byte SGI model object's concrete class or ABI; the original class name remains unresolved.

## UserView constructor

The exported/mangled constructor evidence identifies the original constructor shape as:

```text
UserView(const ImdString &login, long uid, const ImdString &home)
```

The portable reconstruction currently carries equivalent logical values as C++ strings plus uid storage while preserving the original constructor boundary at the presentation layer.

## Account filtering belongs before displayIcon()

Binary evidence for `pandora::createIconList()` shows that account-source and filtering work happens before `displayIcon()`:

- passwd enumeration uses `setpwent()` / `getpwent()`;
- local-home policy affects IRIX passwd/YP enumeration through `_getpwent_no_yp`;
- leading `+` / `-` records use the recovered passwd/NIS compatibility path;
- `PwdNameList` `noshow` / `noicon` flags participate in filtering;
- ignore-list paths participate in filtering;
- `checkDirectory()` state handling participates in filtering;
- accepted records become component model objects through `addUser()`.

`displayIcon()` must not duplicate these gates.

## showPicture()

`showPicture()` switches to the large single-picture presentation used after a user is selected.

High-confidence operations:

1. Unmanage the current child widget/presentation.
2. Build visual arguments with `imdFtrChooseVisualArgs`.
3. Create an SGI VisualDrawingArea with `SgCreateVisualDrawingArea`.
4. Create/configure image rendering state and query resulting widget dimensions.
5. Call the local `CreateXPM()` helper, storing the returned pixmap/state in `PandoraComponent +0x88` and dimensions in `+0x90/+0x94`.
6. Manage the new picture widget and attach X callbacks.

The image file descriptor/path has already been selected by `findUserPicture()`; `showPicture()` is the rendering/presentation half.

## Current reconstruction boundary

The next fidelity target is no longer passwd enumeration inside `displayIcon()`. It is panel selection and callback behavior:

```text
ImdIconPanel::kSelectionChangedCallback
    -> PandoraComponent::selectCB
    -> selected-view list
    -> selected UserView/model login
    -> userText / selected-user presentation

ImdIconPanel::kCommandCallback
    -> PandoraComponent::commandCB
    -> command/double-activation path
```

Until the complete selected-view list ABI is reconstructed, any direct layout activation callback must remain explicitly marked as a portable temporary seam rather than original Clogin behavior.
