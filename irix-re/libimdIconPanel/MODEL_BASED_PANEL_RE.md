# ImdModelBasedIconPanel / ImdModelBasedIconView archaeology

Local binary archaeology against the original IRIX shared object:

```
/usr/lib32/internal/libimdIconPanel.so
```

The specimen is ELF32 MIPS N32, unstripped, with SGI debug information and
exported symbols.

## Exact exported entry points

```
0x09684610  6492  ImdModelBasedIconPanel::ImdModelBasedIconPanel(...)
0x09685f6c   840  ImdModelBasedIconPanel::changeModel(ImdModel *)
0x0967d500   360  ImdModelBasedIconPanel::setModel(ImdModel *)
0x09686408   176  ImdModelBasedIconPanel::show()
0x096a0fa4   576  ImdModelBasedIconPanel::showChildren(ImdIconView &)
0x096a11e4     8  ImdModelBasedIconPanel::hideChildren(ImdIconView &)

0x0968729c   764  ImdModelBasedIconView::ImdModelBasedIconView(ImdDNA &, bool)
0x096871e0   188  ImdModelBasedIconView::~ImdModelBasedIconView()
0x0967a0e8   680  ImdModelBasedIconView::getDNA() const
0x0967c8a0  1752  ImdModelBasedIconView::update(Widget)
0x09679d44   196  ImdModelBasedIconView::getLabel() const
0x0967fe10   380  ImdModelBasedIconView::specifyDNA()
0x0967f478  2456  ImdModelBasedIconView::createContents(bool)
0x0967f314   356  ImdModelBasedIconView::setChildCapabilities()
0x0967d8f0  1568  ImdModelBasedIconView::getType()

0x096b111c  1236  ImdModelBasedIconPanel::NotifyReceiver::NotifyReceiver(...)
0x0968cd74   912  ImdModelBasedIconPanel::NotifyReceiver::~NotifyReceiver()
0x0967aaa0  4116  ImdModelBasedIconPanel::NotifyReceiver::receiveNotification(...)
```

## Critical correction: showChildren is receiver setup

The original exported method is:

```
ImdModelBasedIconPanel::showChildren(ImdIconView &)
```

It is not a zero-argument child-model enumerator.

Recovered control flow at `0x096a0fa4`:

1. Obtain the supplied `ImdIconView` DNA through its virtual interface.
2. Scan the panel's existing receiver collection and compare receiver/view DNA.
3. If a matching receiver already exists, return without installing another.
4. Call `ImdBroker::contextBroker()`.
5. Call `ImdBroker::getModel(dna)`.
6. Allocate 24 bytes for `NotifyReceiver`.
7. Construct:

   ```
   NotifyReceiver(panel, &view, model, false)
   ```

8. Insert the receiver into the panel receiver collection.
9. Call `ImdBroker::releaseModel(model)`.

This invalidates the earlier reconstruction where `showChildren()` directly
iterated `ImdModel` children.

## NotifyReceiver layout

The constructor at `0x096b111c` writes the following original N32 layout:

```
+0x00  pointer used for receiver-owned dictionary/cache state
+0x04  byte flag, initialized false
+0x08  ImdModelBasedIconPanel *
+0x0c  ImdModelBasedIconView *
+0x10  ImdModel * (stored from a model virtual call, likely getSelf())
+0x14  bool constructor parameter
```

Object allocation size is exactly 24 bytes.

The constructor registers `receiveNotification` twice through the model's
callback virtual interface, once with the child-callback selector false and once
with it true. This matches the recovered `ImdModel::addClientCallback(callback,
clientData, childCallback)` surface.

## View creation site

`NotifyReceiver::receiveNotification()` contains the actual model-backed view
creation path.

The original code allocates exactly 88 bytes, then calls:

```
ImdModelBasedIconView::ImdModelBasedIconView(ImdDNA &, bool)
```

The bool is not globally constant. One branch forces it true; another derives it
from the receiver/model relationship. After construction the view is passed to
a panel virtual insertion/show path. That virtual target still needs to be
resolved before assigning an original method name to it.

## changeModel lifecycle

Recovered control flow at `0x09685f6c`:

1. `baseWidget()`
2. `ImdViewPanelBeginBlockChanges(widget)`
3. Tear down old receiver/model relationships.
4. Iterate existing panel views through the real ImdViewPanel iterator API.
5. Call a panel virtual on those views while transitioning models.
6. `setModel(newModel)`
7. If the stored model exists, allocate a root 24-byte `NotifyReceiver` with no
   child view and insert it into the receiver collection.
8. `baseWidget()`
9. `ImdViewPanelEndBlockChanges(widget)`

The original implementation does not simply delete every view and enumerate the
new model's children synchronously.

## setModel state

`setModel()` stores the model pointer at original object offset `+0x64`.

It also maintains an embedded `ImdString` beginning at `+0x68`. For reference
models it appears to derive/cache selector information from model DNA. The exact
semantic name for this string is not yet proven and must not be invented.

## show lifecycle

`ImdModelBasedIconPanel::show()` checks the stored model at original offset
`+0x64`. If present it allocates a 24-byte root `NotifyReceiver`:

```
NotifyReceiver(panel, 0, model, false)
```

and inserts it into the receiver collection, then delegates to
`VkComponent::show()`.

## Next implementation seam

Reconstruct, in this order:

1. portable `ImdDNA` value type sufficient for `ImdModelBasedIconView`;
2. `ImdBroker::contextBroker()`, `getModel()`, and `releaseModel()`;
3. `NotifyReceiver` registration/removal against `ImdModel` callbacks;
4. `ImdModelBasedIconView` constructor and DNA ownership;
5. resolve the panel virtual used after view construction;
6. restore real ImdViewPanel view adoption/deletion state.

Do not synthesize a parallel desktop icon type.
