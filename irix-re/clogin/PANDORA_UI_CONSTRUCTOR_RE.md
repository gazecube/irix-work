# PandoraComponentUI constructor reconstruction

Target binary: `/usr/Cadmin/bin/clogin`

Target symbol:

```text
0x10012bbc PandoraComponentUI::PandoraComponentUI(const char *, Widget)
```

This file is the evidence boundary for reconstructing the original Pandora
Motif widget tree. Do not copy attachment relationships from the portable
`pandora_ui.h` prototype into the ViewKit implementation unless they are
confirmed here from the original constructor.

## Binary-proven facts

The original constructor directly calls the Xt/Motif creation APIs, including:

```text
XtVaCreateWidget
XtVaCreateManagedWidget
XmCreateScrolledWindow
XtAddCallback
```

The original widget instance names recovered from the binary/app-defaults
namespace are:

```text
form
warningLabel
userFullNameLabel
userLabel
userText
passwordLabel
passwordText
panelWindow
scrolledWindow
hostLabel
helpButton
loginButton
Xdraw
```

The preserved SGI app-defaults file proves these resource values:

```text
userLabel.leftOffset      = 45
userLabel.width           = 120
userText.leftOffset       = 6
userText.width            = 225
passwordLabel.leftOffset  = 45
passwordLabel.width       = 120
passwordText.leftOffset   = 0
passwordText.width        = 225

warningLabel.topOffset    = 0
warningLabel.leftOffset   = 35
warningLabel.width        = 700
warningLabel.height       = 30
userFullNameLabel.bottomOffset = 0
userFullNameLabel.leftOffset   = 10

panelWindow.topOffset     = 35
panelWindow.bottomOffset  = 25
panelWindow.leftOffset    = 35
panelWindow.rightOffset   = 30

helpButton.bottomOffset   = 25
helpButton.rightOffset    = 30
helpButton.width          = 72
helpButton.marginHeight   = 3
loginButton.bottomOffset  = 25
loginButton.rightOffset   = 8
loginButton.width         = 72
loginButton.marginHeight  = 3
hostLabel.bottomOffset    = 22
hostLabel.leftOffset      = 33
```

The app-defaults file also proves the labels:

```text
userLabel     = "Login name:"
passwordLabel = "Password:"
loginButton   = "Log In"
helpButton    = "Help"
```

These offsets and sizes do not by themselves prove any `XmForm` attachment
relationships. Those relationships must be recovered from the constructor's
`XtVaCreate*` argument lists.

## ViewKit ownership facts

The ViewKit-backed reconstruction should preserve the original ownership model:

```text
VkApp
  -> PandoraWindow : VkSimpleWindow
       -> PandoraComponent : VkComponent
            -> PandoraComponentUI widget subtree
```

The derived component owns a single-rooted widget subtree through
`VkComponent::_baseWidget`; the base widget is left unmanaged for the
container/window to manage through `addView()`/`show()`.

## Current unproven reconstruction

The attachment graph currently present in `PandoraViewKit.c++` is reconstructed
from the earlier portable prototype and is not binary-proven. In particular,
relationships involving:

```text
userLabel/userText
passwordLabel/passwordText
panelWindow
hostLabel
helpButton/loginButton
```

must be replaced only after decoding the constructor argument sequence.

## Next reverse-engineering pass

For the function range beginning at `0x10012bbc`, record each call in order and
resolve all argument values/rodata references:

```text
1. called function
2. widget instance name
3. widget class / Motif creation routine
4. parent widget
5. every resource/value pair passed in the varargs list
6. destination object/member offset receiving the returned Widget
7. callback name, callback resource, and client data for XtAddCallback calls
```

Special attention is required for these resources:

```text
XmNtopAttachment
XmNbottomAttachment
XmNleftAttachment
XmNrightAttachment
XmNtopWidget
XmNbottomWidget
XmNleftWidget
XmNrightWidget
XmNtopOffset
XmNbottomOffset
XmNleftOffset
XmNrightOffset
XmNwidth
XmNheight
XmNmappedWhenManaged
XmNtraversalOn
```

When the call sequence is decoded, this document should become the canonical
source for the exact constructor tree and `PandoraViewKit.c++` should be
updated from it.
