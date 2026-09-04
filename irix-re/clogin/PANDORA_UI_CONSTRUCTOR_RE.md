# PandoraComponentUI constructor reconstruction

Target binary: `/usr/Cadmin/bin/clogin`

Target symbol:

```text
0x10012bbc PandoraComponentUI::PandoraComponentUI(const char *, Widget)
size 1700 bytes
```

This file is the canonical evidence map for the original Pandora Motif widget
construction. The attachment graph below is decoded from the uploaded IRIX
`clogin` binary, not inferred from the portable prototype.

## Constructor calls

The constructor calls:

```text
VkComponent::VkComponent(const char *)
XtVaCreateWidget
installDestroyHandler()
XtVaCreateManagedWidget
XtAddCallback
XtManageChildren
```

`XmCreateScrolledWindow` and `Xdraw` are not created by this constructor. They
belong to later Pandora component/display setup.

## Base hierarchy

Binary-proven widget hierarchy at constructor entry:

```text
VkComponent::_baseWidget
  -> XmFrame, instance name supplied by constructor caller
     width 846
     height 744
     shadowType 8
       -> XmForm "form"
          width 844
          height 742
```

The frame is created with `XtVaCreateWidget` and is left unmanaged for the
ViewKit container. The child `form` is created with
`XtVaCreateManagedWidget`.

## Member widget offsets

The constructor stores widgets at these object offsets:

```text
+0x28 helpButton
+0x2c hostLabel
+0x30 loginButton
+0x34 panelWindow
+0x38 passwordLabel
+0x3c passwordText
+0x40 userFullNameLabel
+0x44 userLabel
+0x48 userText
+0x4c warningLabel
```

## Exact Form relationships

`passwordLabel`:

```text
class               xmLabelWidgetClass
mappedWhenManaged   False
alignment           XmALIGNMENT_END
recomputeSize       True
topAttachment       XmATTACH_NONE
bottomAttachment    XmATTACH_FORM
leftAttachment      XmATTACH_FORM
rightAttachment     XmATTACH_NONE
```

`userLabel`:

```text
class               xmLabelWidgetClass
alignment           XmALIGNMENT_END
recomputeSize       True
topAttachment       XmATTACH_NONE
bottomAttachment    XmATTACH_WIDGET
bottomWidget        passwordLabel
leftAttachment      XmATTACH_FORM
rightAttachment     XmATTACH_NONE
```

`userText`:

```text
class               xmTextWidgetClass
editMode            XmSINGLE_LINE_EDIT
topAttachment       XmATTACH_NONE
bottomAttachment    XmATTACH_OPPOSITE_WIDGET
bottomWidget        userLabel
leftAttachment      XmATTACH_WIDGET
leftWidget          userLabel
rightAttachment     XmATTACH_NONE
```

`warningLabel`:

```text
class               xmLabelWidgetClass
alignment           XmALIGNMENT_BEGINNING
topAttachment       XmATTACH_OPPOSITE_WIDGET
topWidget           passwordLabel
bottomAttachment    XmATTACH_NONE
leftAttachment      XmATTACH_FORM
rightAttachment     XmATTACH_NONE
```

`passwordText`:

```text
class               xmTextWidgetClass
userData            this
editMode            XmSINGLE_LINE_EDIT
topAttachment       XmATTACH_NONE
bottomAttachment    XmATTACH_OPPOSITE_WIDGET
bottomWidget        passwordLabel
leftAttachment      XmATTACH_OPPOSITE_WIDGET
leftWidget          userText
rightAttachment     XmATTACH_NONE
```

`userFullNameLabel`:

```text
class               xmLabelWidgetClass
alignment           XmALIGNMENT_BEGINNING
recomputeSize       True
topAttachment       XmATTACH_NONE
bottomAttachment    XmATTACH_OPPOSITE_WIDGET
bottomWidget        userLabel
leftAttachment      XmATTACH_WIDGET
leftWidget          userText
rightAttachment     XmATTACH_NONE
```

`panelWindow`:

```text
class               xmFrameWidgetClass
topAttachment       XmATTACH_FORM
bottomAttachment    XmATTACH_WIDGET
bottomWidget        userLabel
leftAttachment      XmATTACH_FORM
rightAttachment     XmATTACH_FORM
```

`helpButton`:

```text
class               xmPushButtonWidgetClass
recomputeSize       False
topAttachment       XmATTACH_NONE
bottomAttachment    XmATTACH_FORM
leftAttachment      XmATTACH_NONE
rightAttachment     XmATTACH_FORM
```

`loginButton`:

```text
class               xmPushButtonWidgetClass
recomputeSize       False
topAttachment       XmATTACH_NONE
bottomAttachment    XmATTACH_FORM
leftAttachment      XmATTACH_NONE
rightAttachment     XmATTACH_WIDGET
rightWidget         helpButton
```

Thus the original right-side button order is:

```text
[ Log In ] [ Help ]
```

`hostLabel`:

```text
class               xmLabelWidgetClass
alignment           XmALIGNMENT_BEGINNING
recomputeSize       True
topAttachment       XmATTACH_NONE
bottomAttachment    XmATTACH_FORM
leftAttachment      XmATTACH_FORM
rightAttachment     XmATTACH_NONE
```

## Initial managed set

At constructor end, the binary builds a six-element Widget array and calls:

```text
XtManageChildren(..., 6)
```

The exact managed set is:

```text
loginButton
helpButton
panelWindow
userText
userLabel
passwordLabel
```

These are NOT managed by this constructor initially:

```text
warningLabel
passwordText
userFullNameLabel
hostLabel
```

This distinction is important. The earlier reconstruction incorrectly created
nearly every child managed, which preserved the wrong layout even when the
app-defaults database was loaded correctly.

## Callback wiring

The constructor wires `userText` callbacks with `this` as client data:

```text
XmNactivateCallback -> userTextActivateCBCallback
XmNfocusCallback    -> userTextFocusCBCallback
```

The constructor wires `passwordText` callbacks with `this` as client data:

```text
XmNactivateCallback     -> passwordTextActivateCBCallback
XmNfocusCallback        -> passwordTextFocusCBCallback
XmNmodifyVerifyCallback -> passwordTextModifyVerCBCallback
XmNmotionVerifyCallback -> passwordTextMotionVerCBCallback
```

The constructor also wires:

```text
helpButton  XmNactivateCallback -> helpCBCallback
loginButton XmNactivateCallback -> loginCBCallback
```

The standalone ViewKit probe currently reconstructs the widget hierarchy and
geometry but does not yet reproduce these behavioral callbacks.

## App-defaults-owned values

The preserved SGI app-defaults file supplies offsets, widths, fonts, labels,
and other presentation resources. Relevant values include:

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
loginButton.bottomOffset  = 25
loginButton.rightOffset   = 8
loginButton.width         = 72
hostLabel.bottomOffset    = 22
hostLabel.leftOffset      = 33
```

Labels:

```text
userLabel     = "Login name:"
passwordLabel = "Password:"
loginButton   = "Log In"
helpButton    = "Help"
```

## Source cross-check

The resource-name offsets in the MIPS constructor were resolved against the
IRIX Motif 1.2.4 `XmStrDefs.h` and X11 `StringDefs.h` from the preserved IRIX
source tree. This confirms values such as:

```text
_XmStrings[5017]  -> XmNbottomAttachment
_XmStrings[7523]  -> XmNleftAttachment
_XmStrings[7562]  -> XmNleftWidget
_XmStrings[9077]  -> XmNrightAttachment
_XmStrings[9119]  -> XmNrightWidget
_XmStrings[10165] -> XmNtopAttachment
_XmStrings[10305] -> XmNtopWidget
_XmStrings[10526] -> XmNuserData
XtStrings[234]    -> XtNheight
XtStrings[453]    -> XtNmappedWhenManaged
XtStrings[872]    -> XtNwidth
```
