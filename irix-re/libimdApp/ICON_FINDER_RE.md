# ImdIconFinder application-side reconstruction

Primary artifact:

`/usr/lib32/internal/libimdApp.so`, IRIX MIPS N32, unstripped, with debug info.

## Recovered ownership seam

- Original source directory: `userenv/desktop/lib/libimdApp/Finders/`
- Original compilation unit: `IconFinder.c++`
- `libimdApp.so` has `DT_NEEDED: libSgm.so.1`.
- `ImdIconFinder::ImdIconFinder(const ImdString &, Widget, int)` is at
  `0x0963ec5c`, size 1780.
- The constructor calls `VkComponent(const char *)`, installs the
  `ImdIconFinder` vtable, and creates its base widget with
  `XtCreateWidget(name, sgFinderWidgetClass, parent, args, count)`.
- `ImdIconFinder::getWidget() const` is at `0x0963f350`, size 8, and returns
  the widget stored at object offset `+0x10`.
- The original library imports `SgFinderGetChild`,
  `SgFinderGetTextString`, and `SgFinderSetTextString`.
- The constructor creates widget instances named `iconFinderFrame` and
  `iconFinderPanel` below the SgFinder base widget.

The original fallback resources recovered from `_fallbackResources` are:

- `*finderSelectionMenu.labelType: PIXMAP`
- `*finderSelectionMenu.labelPixmap: /usr/include/X11/bitmaps/sgidesktop/iconFinderPrevious`
- `*findButton.labelString: Launch`
- `*iconBookButton.labelString: IconBook`
- `*searchBookButton.labelString: SearchBook`
- `*infoscapeButton.labelString: InfoScape`

## Current reconstruction boundary

The host `ImdIconFinder` now makes `libimdApp.so` a real consumer of the
reconstructed `libSgm.so.1`. It creates SgFinder as its base widget, retains
the binary-confirmed text child, routes content get/set through the public
SgFinder API, and restores the `iconFinderFrame` / `iconFinderPanel` ownership
shape.

The original constructor's `ImdArgList`, `VkComponent` base, `ImdString` ABI,
`ImdCompletionField`,
`ImdModelBasedIconPanel`, renderer/layout setup, drag-and-drop targets, and
ViewKit menus remain future reconstruction work. Portable substitutions are
marked in the source and are not claimed as original SGI ABI.
