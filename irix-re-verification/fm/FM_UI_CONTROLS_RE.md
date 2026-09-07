# FM control-board UI reverse-engineering notes

Target specimen:

```text
irix-re/evidence/fm
SHA-256 65430bf4937027314b1a940b1e7f109d01f3dc48275223216360e3b50a809818
ELF 32-bit MSB MIPS N32 MIPS-III
```

This note records only UI facts corroborated by the preserved `fm` executable
and the recovered `FileManager` app-defaults.  Screenshots are reference only.

## FmWindow::_createPanels

The preserved executable exports:

```text
_createPanels__8FmWindowGv
0x100187e0
size 3596
```

The function imports and calls the following UI constructors/helpers:

```text
XmCreateForm
XtVaCreateManagedWidget
ImdViewerFinder::ImdViewerFinder(ImdString const &, Widget, int)
ImdModelBasedIconPanel::ImdModelBasedIconPanel(char const *, Widget,
                                               ImdModel *, Arg *, int)
ImdInvertedTeeRenderer::ImdInvertedTeeRenderer()
ImdIconLayout::ImdIconLayout(Widget)
ImdIControlBoard::ImdIControlBoard(Widget, ImdIconPanel &)
ImdIControlBoard::addButton(char const *, void (*)(Widget, void *, void *),
                            void *)
ImdIControlBoard::getWidget()
VkPopupMenu::VkPopupMenu(VkMenuDesc *, void *)
FmMenuHandler::FmMenuHandler(FmWindow *, VkMenu *, int, char **)
```

The main directory panel is stored at `FmWindow +0xa4` and is immediately
registered through:

```text
FmMenuActions::setDirPanel(ImdModelBasedIconPanel *)
```

The binary also imports these control-board layout identifiers:

```text
IconicLayout__16ImdIControlBoard
ListLayout__16ImdIControlBoard
ColumnLayout__16ImdIControlBoard
NoLayout__16ImdIControlBoard
```

This confirms that layout switching is part of the control-board layer rather
than an independently invented toolbar in the reimplementation.

## Original widget/resource names

The recovered `FileManager` app-defaults gives the control widget names used by
the original program:

```text
rc1
rc2
pathfinder
iconicLayout
listLayout
treeLayout
columnLayout
galleryMode
shelf
scaleZoom
finderForm
iconFinder
scrollW
panel
```

The same binary contains `finderForm`, `iconFinder`, `scrollW`, and `panel`
strings, independently corroborating the resource hierarchy.

## Control geometry/resources

Recovered original resources:

```text
*FileManager*rc1.orientation: HORIZONTAL
*FileManager*rc2.orientation: VERTICAL
*FileManager*rc2.packing: PACK_TIGHT

*FileManager*pathfinder.labelType: PIXMAP
*FileManager*pathfinder.labelPixmap: /usr/include/X11/bitmaps/sgidesktop/pathfinder.bm
*FileManager*iconicLayout.labelType: PIXMAP
*FileManager*iconicLayout.labelPixmap: /usr/include/X11/bitmaps/sgidesktop/iconicDirView.bm
*FileManager*listLayout.labelType: PIXMAP
*FileManager*listLayout.labelPixmap: /usr/include/X11/bitmaps/sgidesktop/listDirView.bm
*FileManager*treeLayout.labelType: PIXMAP
*FileManager*treeLayout.labelPixmap: /usr/include/X11/bitmaps/sgidesktop/treeDirView.bm
*FileManager*columnLayout.labelType: PIXMAP
*FileManager*columnLayout.labelPixmap: /usr/include/X11/bitmaps/sgidesktop/columnDirView.bm
*FileManager*galleryMode.labelType: PIXMAP
*FileManager*galleryMode.labelPixmap: /usr/include/X11/bitmaps/sgidesktop/galleryDirView.bm
*FileManager*shelf.labelType: PIXMAP
*FileManager*shelf.labelPixmap: /usr/include/X11/bitmaps/sgidesktop/shelf.bm
```

The icon scaling control is also directly specified:

```text
*FileManager*scaleZoom.orientation: VERTICAL
*FileManager*scaleZoom.processingDirection: MAX_ON_TOP
*FileManager*scaleZoom.maximum: 500
*FileManager*scaleZoom.minimum: 60
*FileManager*scaleZoom.value: 100
*FileManager*scaleZoom.homePosition: 100
*FileManager*scaleZoom.showValue: false
*FileManager*scaleZoom.traversalOn: false
```

The finder height and attachments are original resources:

```text
*finderForm.iconFinder.height: 48
*FileManager*iconFinder.leftAttachment: XmATTACH_FORM
*FileManager*iconFinder.rightAttachment: XmATTACH_FORM
*FileManager*iconFinder.topAttachment: XmATTACH_FORM
*FileManager*iconFinder.bottomAttachment: XmATTACH_NONE
*FileManager*iconFinderPanel.traversalOn: False
```

## Main panel resources

The original directory-view panel has always-visible scroll policies and
binary/resource-backed icon-layout parameters:

```text
FileManager*panel.horzScrollPolicy: ALWAYS
FileManager*panel.vertScrollPolicy: ALWAYS

*FileManager*scrollW.panel.gridWidth: 30
*FileManager*scrollW.panel.staggerOffset: 120
*FileManager*scrollW.panel.minimumRightIconPadding: 10
*FileManager*scrollW.panel.baseOfIconPadding: 10
*FileManager*scrollW.panel.topMargin: 20
*FileManager*scrollW.panel.bottomMargin: 20
*FileManager*scrollW.panel.leftMargin: 5
*FileManager*scrollW.panel.rightMargin: 3
*FileManager*scrollW.panel.overlapLimit: 20
*FileManager*scrollW.panel.minimumIconWidth: 70
*FileManager*scrollW.panel.maximumIconWidth: 100
```

## Implementation boundary

The next runnable UI slice should therefore create the original-named control
hierarchy and callbacks around the already-live `ImdModelBasedIconPanel`, while
keeping the temporary directory list as a child until the reconstructed model,
view, layout and FTR icon paths can replace it.  Do not derive geometry or
control identity from screenshots when the executable/resources disagree.
