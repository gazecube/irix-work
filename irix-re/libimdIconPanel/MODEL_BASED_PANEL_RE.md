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
+0x10  ImdModel *
+0x14  bool constructor parameter
```

Object allocation size is exactly 24 bytes.

Fresh local disassembly resolves the `+0x10` model assignment. The constructor
calls the virtual slot corresponding to `ImdModel::getSelf()` through the
supplied model, then stores that returned pointer at `+0x10`.

The constructor registers `receiveNotification` twice through the stored model's
`ImdModel::addClientCallback()` virtual slot. The exact calls at the end of the
constructor are:

```
model->addClientCallback(receiveNotification, this, false);
model->addClientCallback(receiveNotification, this, true);
```

This is not inferred from ordering: the callback address loaded at both call
sites resolves through the original GOT to `0x0967aaa0`, the exact address of
`NotifyReceiver::receiveNotification`, and the final boolean argument is first
zero and then one.

## ImdModel callback registration behavior

Fresh local disassembly of original `libimdModel.so` resolves:

```
0x09753470  ImdModel::addClientCallback(callback, clientData, childCallback)
```

The implementation:

1. invokes the model activation virtual before installing the callback;
2. allocates a 12-byte `CallbackInfo`;
3. stores callback at `+0x00`, clientData at `+0x04`, and active byte `1` at
   `+0x08`;
4. selects `_clientCallbacks` when `childCallback == false` and
   `_clientChildCallbacks` when `childCallback == true`;
5. lazily creates the selected callback collection when absent;
6. inserts the new `CallbackInfo` into that collection;
7. returns true.

There is no direct invocation of the newly registered callback in
`addClientCallback()` itself. Any initial model notification therefore comes
through activation/update/notification behavior, not from an explicit immediate
callback call at the registration site.

## View creation site

`NotifyReceiver::receiveNotification()` contains the actual model-backed view
creation path.

The original code allocates exactly 88 bytes, then calls:

```
ImdModelBasedIconView::ImdModelBasedIconView(ImdDNA &, bool)
```

The bool is not globally constant. One branch forces it true; another derives it
from the receiver/model relationship.

### Post-construction insertion virtual - RESOLVED

The previously unresolved panel virtual immediately following view construction
is now identified exactly from the original `ImdModelBasedIconPanel` vtable.

At the call site in `receiveNotification()`:

```
panel vptr = *(panel + 0x08)
this-adjust = *(vptr + 0x9c)
function    = *(vptr + 0xa4)
```

The `ImdModelBasedIconPanel` vtable entry at that exact slot contains:

```
0x09682c90  ImdIconPanel::adoptView(ImdIconView *, ImdIconView *)
```

The call arguments are:

```
panel->adoptView(newView, receiverChildView);
```

where the second argument comes from `NotifyReceiver + 0x0c`. For a root
receiver that value is null. For a receiver installed by `showChildren(view)`,
it is the existing parent/anchor view supplied to the receiver constructor.

### ImdIconPanel::adoptView() overload selection - RESOLVED

Fresh local disassembly of `ImdIconPanel::adoptView()` at `0x09682c90` proves
that the second view argument controls which original ImdViewPanel overload is
used:

```
if (secondView != 0)
    ImdViewPanelAdoptView(baseWidget, newView, secondView);
else
    ImdViewPanelAdoptView(baseWidget, newView, this);
```

The two GOT targets resolve exactly to:

```
0x096ab1cc  ImdViewPanelAdoptView(Widget, ImdView *, ImdView *)
0x096ab374  ImdViewPanelAdoptView(Widget, ImdView *, ImdIconPanel *)
```

After the ImdViewPanel adoption call, `ImdIconPanel::adoptView()` performs one
additional panel virtual call on the new view. That follow-up virtual remains to
be named from the original vtable before reconstruction.

This resolves the critical receive-notification insertion path as:

```
NotifyReceiver::receiveNotification(...)
    -> new ImdModelBasedIconView(dna, bool)
    -> ImdIconPanel::adoptView(newView, receiverChildView)
       -> ImdViewPanelAdoptView(...)
```

No parallel desktop-specific insertion path exists in this flow.

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

Fresh GOT resolution confirms the iterator calls used here are the original:

```
ImdViewPanelCreateIterator(...)
ImdViewPanelGetNext(...)
ImdViewPanelDestroyIterator(...)
```

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

The desktop DNA, broker, and live Directory model are now reconstructed enough
to produce a real root model. The next code must remain gated on the remaining
binary proof in this order:

1. identify the final virtual invoked by `ImdIconPanel::adoptView()` after the
   ImdViewPanel adoption call;
2. trace `ImdModel::removeClientCallback()` and the NotifyReceiver destructor
   so callback lifetime/removal is exact;
3. trace the root receiver's first activation/notification path far enough to
   reproduce initial child view creation without synchronous enumeration;
4. only then implement portable callback collections and NotifyReceiver;
5. restore real ImdViewPanel view ownership/deletion state.

Do not synthesize a parallel desktop icon type.
