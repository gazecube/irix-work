# ImdModelBasedIconView binary evidence checkpoint

This note records only evidence recovered from the preserved original IRIX shared-object corpus and the generated DWARF inventory. It deliberately does not assign semantics to unresolved members or call edges.

## Proven source unit and public methods

The original `libimdIconPanel.so` contains the source unit `ModelBasedIconView.c++` and DWARF public names for:

```
ImdModelBasedIconView::ImdModelBasedIconView
ImdModelBasedIconView::createContents
ImdModelBasedIconView::getDNA
ImdModelBasedIconView::getExtraLabels
ImdModelBasedIconView::getLabel
ImdModelBasedIconView::getType
ImdModelBasedIconView::releaseExtraLabels
ImdModelBasedIconView::setChildCapabilities
ImdModelBasedIconView::specifyDNA
ImdModelBasedIconView::update
ImdModelBasedIconView::~ImdModelBasedIconView
```

The base `ImdIconView` public-name surface is independently present:

```
ImdIconView::ImdIconView
ImdIconView::createContents
ImdIconView::getExtraLabels
ImdIconView::getFlag
ImdIconView::getLabel
ImdIconView::releaseExtraLabels
ImdIconView::setFlag
ImdIconView::setLabel
ImdIconView::~ImdIconView
```

This proves that label/content ownership is part of the original icon-view class hierarchy and is not a desktop-host invention.

## Proven renderer-facing classes

The original library also exposes the real renderer surface used by the adopted views:

```
ImdIconRenderer::drawIcons
ImdIconRenderer::formatLabel
ImdIconRenderer::getArrowRect
ImdIconRenderer::getIconRect
ImdIconRenderer::getLabelRect
ImdIconRenderer::getTextXRect
ImdIconRenderer::realize
ImdIconRenderer::setGallery
ImdIconRenderer::setScale
ImdIconRenderer::shape

ImdInvertedTeeRenderer::beginRender
ImdInvertedTeeRenderer::draw
ImdInvertedTeeRenderer::endRender
ImdInvertedTeeRenderer::getIconX
ImdInvertedTeeRenderer::getIconXRect
ImdInvertedTeeRenderer::getLabelXRect
ImdInvertedTeeRenderer::getRect
ImdInvertedTeeRenderer::getTextX
ImdInvertedTeeRenderer::getTextY
ImdInvertedTeeRenderer::realize
ImdInvertedTeeRenderer::setIconScale
ImdInvertedTeeRenderer::setScale
ImdInvertedTeeRenderer::setSize
```

The current desktop host already adopts `ImdInvertedTeeRenderer`, so the next implementation must feed the original view/content representation into this renderer rather than create a parallel icon painter.

## FTR dependency proof

`libimdIconPanel.so` directly depends on both:

```
libimdFtrDB.so
libimdFtrIcon.so
```

The preserved `libimdFtrIcon.so` DWARF surface includes:

```
ImdFtrIcon::getType
ImdFtrIcon::getTypeName
ImdFtrIcon::getScale
ImdFtrIcon::setScale
ImdFtrIcon::setIconState
ImdFtrIcon::setOffset
ImdFtrIcon::useIconCache
ImdFtrIcon::useIconMask

ImdFtrIconImpl::getDBName
ImdFtrIconImpl::getParams
ImdFtrIconImpl::getRenderInfo
ImdFtrIconImpl::setupIconType

ImdFtrIconX::drawIcon
ImdFtrIconX::getMask
ImdFtrIconX::getPixmap
```

`libimdFtrDB.so` exposes the FTR database/type layer, and `libimdApp.so` independently uses `ImdFtrDB::createTypeFromTypeName` and `ImdFtrType::getTypeName`.

This establishes the expected architectural bridge:

```
ImdModelBasedIconView
    -> FTR type/icon state
    -> ImdIconRenderer / ImdInvertedTeeRenderer
```

but does not yet prove the exact call sequence inside `createContents()`.

## Hard gate before implementation

Do not implement `createContents()`, `getType()`, `getLabel()`, `setChildCapabilities()`, or `update()` from these names alone.

The next required binary pass is the original MIPS disassembly for the five methods, resolving at minimum:

1. imported/direct callees and vtable slots;
2. exact model attribute IDs read by `getLabel()` and `getType()`;
3. which FTR constructor/factory is called by `createContents()`;
4. which base `ImdIconView` members or setters receive the generated contents;
5. which branches in `setChildCapabilities()` affect renderer-visible flags;
6. whether `update(Widget)` calls `createContents()` directly or invalidates/rebuilds through another path.

Until those edges are proven, the currently adopted model-backed views remain intentionally content-incomplete.
