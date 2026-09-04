# displayIcon / showPicture reconstruction

Targets:

- `PandoraComponent::displayIcon()` at `0x10011b8c`
- `PandoraComponent::showPicture()` at `0x10011d10`

## displayIcon()

The function operates on the component's Imd icon panel/model rather than directly painting Motif labels.

Recovered flow:

1. Begin a blocked Imd panel update with `ImdViewPanelBeginBlockChanges`.
2. Iterate the component's user/model collection at `PandoraComponent +0x50`.
3. For each model object, construct a `UserView` (`UserView::UserView(const ImdString &, long, long)`).
4. Add/configure that view through the panel/view interfaces at `PandoraComponent +0x84`.
5. Continue until the model iterator is exhausted.
6. End the blocked update with `ImdViewPanelEndBlockChanges`.

This proves the original icon screen is an Imd view/model pipeline, not a row of Motif push buttons.

## Current live Linux probe boundary

The ViewKit probe now calls a reconstructed `PandoraComponent::displayIcon()` after the binary-proven `ImdIconPanel("panel", ...)` is created.

Account enumeration follows the recovered IRIX gates already documented for `pandora::createIconList()` where they map cleanly to Linux today:

- enumerate with `getpwent()` rather than a UID threshold,
- reject empty login names,
- reject empty home paths,
- reject a literal `"*"` passwd field,
- honor `noshow` and `noicon` from `/var/sysadm/config/clogin.conf`,
- honor configured ignored home prefixes,
- require the home path to exist and be a directory.

When `/var/sysadm/config/clogin.conf` is absent on a Linux development machine, the probe additionally suppresses accounts whose shell basename is `nologin` or `false`. This is explicitly a reconstructed Linux probe fallback for the missing SGI account metadata, not original IRIX policy. It must disappear as an eligibility rule once the installed Clogin configuration/passwd metadata path is available.

The actual `UserView`/renderer/layout classes are still being reconstructed. Until those interfaces are linkable, each accepted account is represented by a Motif push-button child inside a temporary RowColumn under the reconstructed `imdViewPanelWidgetClass`. Activating one copies the account name into the existing `userText` field. This temporary rendering backend preserves the original `displayIcon()` boundary and user-selection direction without claiming the push buttons are original UI machinery.

No generic Linux `UID >= 1000` filter is used.

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

## Porting consequence

For the first Linux implementation we do not need to reproduce the entire libimd renderer before a working login screen is possible. The behavioral boundary can be preserved as:

- `displayIcon()` -> populate a user-icon view from the reconstructed user model
- `showPicture()` -> switch to a selected-user picture view

The initial portable renderer may use Motif/Xpm while retaining these original function boundaries. Replacing it with reconstructed Imd UI machinery can happen incrementally.
