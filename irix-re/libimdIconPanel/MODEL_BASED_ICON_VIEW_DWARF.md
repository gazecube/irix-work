# ImdModelBasedIconView binary evidence checkpoint

This note records only evidence recovered from the preserved original IRIX shared-object corpus. It deliberately does not assign semantics to unresolved members or call edges.

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

## ImdModelBasedIconView vtable slots - RESOLVED

The original vtable is at `0x096c9e38`, size 168. The relevant SGI virtual function slots resolve to:

```
+0x20  setChildCapabilities
+0x2c  ImdIconView::setFlag
+0x38  ImdIconView::getFlag
+0x44  createContents
+0x50  getExtraLabels
+0x5c  releaseExtraLabels
+0x68  getLabel
+0x74  ImdIconView::setLabel
+0x80  getDNA
+0x8c  update
+0x98  specifyDNA
+0xa4  getType
```

This resolves the three opening virtual calls in `createContents(bool)` exactly as:

```
specifyDNA();
ImdDNA dna = getDNA();
...
ImdDNA typeDNA = getType();
```

The last point is important: despite the method name, the original `getType()` returns an `ImdDNA` value, not a plain string or enum.

## Base ImdIconView state - RESOLVED

Original `ImdIconView::ImdIconView()` at `0x0968412c` initializes:

```
+0x20  0
+0x24  0
+0x28  0
+0x2c  ImdString()
```

The two pointers at `+0x20` and `+0x24` are the renderer-facing content pointers populated by `ImdModelBasedIconView::createContents()` as described below.

Original `ImdIconView::setFlag(ImdFtrIcon::EIconState, bool)` at `0x0967bd40` performs the exact bit operations:

```
value == true  -> flags |= mask
value == false -> flags &= ~mask
```

Original `ImdIconView::getFlag(ImdFtrIcon::EIconState)` at `0x0967c88c` returns true only when all bits in the requested mask are set:

```
(flags & mask) == mask
```

The bitmask is stored at N32 offset `+0x28`.

Original `ImdIconView::getLabel()` at `0x09679d08` copy-constructs an `ImdString` from original object offset `+0x2c`.

Original `ImdIconView::setLabel(const ImdString &)` at `0x096840f8` assigns directly into the same `ImdString` at `+0x2c`.

Therefore the original label member begins at N32 offset:

```
ImdIconView + 0x2c : ImdString label
```

No parallel desktop label object is involved.

## Constructor classification and initial label - RESOLVED

Original constructor:

```
0x0968729c  ImdModelBasedIconView::ImdModelBasedIconView(ImdDNA &, bool)
```

The constructor first calls `ImdIconView::ImdIconView()`, installs the derived vtable, and clears derived state at original offsets `+0x34`, `+0x38`, `+0x40`, `+0x48`, `+0x4c`, and `+0x50`.

It then classifies the supplied DNA type into a three-bit field in the base flags word. The field occupies bits 18..20 (`0x001c0000`). The observed values are:

```
0 : ImdFileModel::kUnknownType
1 : ImdFileModel::kTypeName
2 : ImdDirModel::kTypeName
3 : ImdRefModel::kTypeName
4 : any other type, or the constructor bool forces this mode
```

When the constructor bool is true, the field is forced to value 4. If the supplied DNA initially has `ImdFileModel::kUnknownType`, the constructor performs the proven path:

```
ImdFileModel::fileNameFromDNA(dna)
ImdFileModel::dnaFromFullPath(path, false)
dna = resulting DNA
```

When the constructor bool is false, the field is selected directly from `dna.type()` using the four original type constants above.

Initial label construction is also resolved. After classification:

```
if (mode == 4) {
    ImdString serialized = dna.serialize();
    ImdIconView::setLabel(serialized);
} else {
    ImdBroker *broker = ImdBroker::contextBroker();
    ImdString altName = broker->altNameFromDNA(dna);
    ImdIconView::setLabel(altName);
}
```

This is the original model-view label path.

## Broker alternate-name dispatch - RESOLVED

Original `ImdBroker::altNameFromDNA()` is at `0x09741610`, size 228 in `libimdModel.so`.

GOT resolution proves the opening calls are:

```
ImdString type(dna.type());
FactoryEntry *entry = _getFactoryEntry(type);
```

If no factory entry exists, the function returns a copy of `dna.selector()`.

If an entry exists, it dispatches through that factory's virtual `altNameFromDNA(dna)` method and returns the resulting `ImdString`.

This proves that model-view label selection is factory-backed, with selector fallback only when no registered factory exists.

## getLabel() override - RESOLVED

Original `ImdModelBasedIconView::getLabel() const` at `0x09679d44` first delegates to `ImdIconView::getLabel()`.

For modes other than 4, that base label is returned unchanged.

For mode 4, the base label is interpreted as a serialized DNA string:

```
ImdDNA dna(baseLabel);
ImdBroker *broker = ImdBroker::contextBroker();
result = broker->altNameFromDNA(dna);
```

So mode 4 intentionally stores serialized DNA in the base label slot while exposing the broker-derived alternate name to callers.

## setChildCapabilities() - RESOLVED

Original `setChildCapabilities()` at `0x0967f314` performs:

```
ImdDNA dna = getDNA();
ImdBroker *broker = ImdBroker::contextBroker();
ImdModel *model = broker->getModel(dna);
if (!model)
    return;
```

It then reads model attribute ID:

```
0xEFFF0042
```

through the model attribute/deleter path and converts it to a boolean. This is the same attribute whose semantics were previously recovered as the model's open capability; the original symbolic spelling is still not proven.

The boolean is written into view flag bit 27 (`0x08000000`). Independently, bit 26 (`0x04000000`) is always set. The function then releases the model through `ImdBroker::releaseModel()`.

Do not rename these two flag bits until their original symbolic names are recovered.

## createContents(bool) FTR path - RESOLVED through object construction

Original `createContents(bool)` is at `0x0967f478`, size 2456.

The opening sequence is now exact:

```
specifyDNA();
ImdDNA dna = getDNA();
ImdBroker *broker = ImdBroker::contextBroker();
ImdModel *model = broker->getModel(dna);
if (!model)
    return;

ImdDNA typeDNA = getType();
if (typeDNA != ImdDNA::kNull) {
    ... FTR content selection ...
}
```

From `typeDNA`, the function copies:

```
typeDNA.selector()
typeDNA.type()
typeDNA.getAttr(ImdTypedModel::dnaSecondaryDBKey)
typeDNA.getAttr(ImdTypedModel::dnaSecondaryTypeKey)
```

The two attribute-key symbols are direct imports from the original model library; these are not reconstructed names.

### Primary FTR content branch

If the copied type string is non-empty, the function performs:

```
ImdFtrDB db = imdFtrLoadDB(typeDNA.selector());
ImdFtrType *type = imdFtrIconTypeFromTypeName(db, typeDNA.type());
```

When an FTR type is returned, the original allocates 16 bytes and constructs:

```
ImdFtrGalleryX(const ImdFtrType &)
```

The resulting object is stored at original `ImdModelBasedIconView + 0x20`.

### Primary gallery configuration virtuals - RESOLVED

Fresh local vtable resolution against original `libimdFtrIcon.so` identifies both virtual calls made immediately after `ImdFtrGalleryX` construction.

The gallery object's SGI vtable is `__vtbl__14ImdFtrGalleryX` at `0x09824570`.

The first call in `createContents()` loads the virtual entry with adjustment at table `+0x6c` and function at `+0x74`. The function pointer is:

```
0x0980a130  ImdFtrIcon::setGallery(bool)
```

The argument is the incoming `force` boolean from `createContents(bool)`.

The second call loads the virtual entry with adjustment at table `+0xc0` and function at `+0xc8`. The function pointer is:

```
0x09809f48  ImdFtrGalleryX::setGalleryFileName(const char *)
```

The filename passed to it is produced by:

```
ImdFileModel::fileNameFromDNA(dna)
```

Therefore the proven primary configuration sequence is:

```
ImdFtrGalleryX *gallery = new ImdFtrGalleryX(*type);
gallery->setGallery(force);
ImdString path = ImdFileModel::fileNameFromDNA(dna);
gallery->setGalleryFileName(path);
```

The temporary FTR type is released with `imdFtrFreeIconType()` and the temporary DB with `imdFtrFreeDB()`.

### Secondary FTR content branch

If both secondary strings are non-empty, the function performs the parallel sequence:

```
ImdFtrDB db = imdFtrLoadDB(secondaryDB);
ImdFtrType *type = imdFtrIconTypeFromTypeName(db, secondaryType);
```

When a type is returned, the original allocates 12 bytes and constructs:

```
ImdFtrIconX(const ImdFtrType &)
```

The resulting object is stored at original `ImdModelBasedIconView + 0x24`.

Again the temporary FTR type and DB are freed after construction.

This proves that the model-backed view owns two distinct renderer-facing FTR objects: a primary `ImdFtrGalleryX` and an optional secondary `ImdFtrIconX`. They are not synthetic desktop-host objects.

## createContents() model attributes and view flags

After FTR construction, the original reads several model attributes through the normal model/deleter conversion path. The exact IDs observed in this function are:

```
0xEFFF0047
0xEFFF00C9
0xEFFF00C5
0xEFFF00C4
0xEFFF00C1
0xEFFF00CD
```

`0xEFFF0047` is the previously recovered open-state attribute. When the attribute exists and is true, `createContents()` invokes the inherited flag setter with `(1, true)`.

The remaining five attribute values feed view members/flag bits at original offsets `+0x34`, `+0x38`, `+0x40`, and `+0x48`, plus additional bits in the base flags word. Their exact original symbolic attribute names and member names remain unresolved and must not be invented.

The function releases the broker model before returning.

## update(Widget) high-level edges - RESOLVED

Original `update(Widget)` at `0x0967c8a0` starts by obtaining `getDNA()` and resolving the corresponding model through the context broker. It reads model attributes, obtains `getType()`, and compares the current FTR content against the newly resolved DNA/type state.

The imported/direct calls prove that update participates in the real view-panel invalidation path:

```
ImdViewPanelGetRenderer(...)
ImdViewPanelBeginBlockChanges(...)
ImdViewPanelCreateIterator(...)
ImdViewPanelGetNext(...)
ImdViewPanelDestroyIterator(...)
ImdViewPanelEndBlockChanges(...)
ImdViewPanelGetLayout(...)
ImdViewPanelClearView(...)
```

It also compares the current FTR object's:

```
ImdFtrIcon::getDBName()
ImdFtrIcon::getTypeName()
```

against the newly resolved DNA selector/type. This proves that update can invalidate/rebuild renderer-visible FTR content when the model's effective icon type changes rather than merely changing a text label.

The remaining virtual calls inside the view-panel iteration still need their exact class/method names resolved before source reconstruction of `update()`.

## Renderer-facing classes

The original library exposes the real renderer surface used by the adopted views:

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

The current desktop host already adopts `ImdInvertedTeeRenderer`, so reconstruction must feed this original view/content representation into that renderer rather than create a parallel icon painter.

## Next hard gates

The original disassembly has now crossed the primary FTR configuration gate. Before implementing full `createContents()` or `update()`, resolve:

1. the original symbolic names for model attributes `0xEFFF00C9`, `0xEFFF00C5`, `0xEFFF00C4`, `0xEFFF00C1`, and `0xEFFF00CD`;
2. the exact member/flag semantics at original view offsets `+0x34`, `+0x38`, `+0x40`, `+0x48`, and `+0x50`;
3. the view-panel iterator virtual calls inside `update()`;
4. the exact `ImdRefModel::kTypeName` string before reproducing constructor type classification with host string literals.

The base icon-view flag/label semantics, constructor classification algorithm, `getLabel()` behavior, FTR object classes, FTR database/type selection, gallery configuration, and `setChildCapabilities()` flag writes are now binary-proven and can be implemented source-level once the corresponding portable storage is introduced without claiming SGI N32 layout compatibility.
