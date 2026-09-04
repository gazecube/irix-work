# ViewKit callback reverse-engineering notes

Target: IRIX 6.5.0 `libvk.so.1`.

Primary evidence is the original IRIX binary. Addresses below refer to the recovered N32 `usr/lib32/libvk.so.1`.

## Exported callback symbols

Important recovered entry points include:

```text
0x0d636388 VkCallbackObject::VkCallbackObject()
0x0d62f264 VkCallbackObject::~VkCallbackObject()
0x0d62fba8 VkCallbackObject::callCallbacks(const char *, void *)
0x0d634c00 VkCallbackObject::addCallback(...member function...)
0x0d63f768 VkCallbackObject::addCallback(...free function...)
0x0d62ee00 VkCallbackObject::removeCallback(...free function...)
0x0d643ae0 VkCallbackObject::removeCallback(...member function...)
0x0d62f2cc VkCallbackObject::removeAllCallbacks()
0x0d634cec VkCallbackObject::registerSender(VkCallbackObject *)
0x0d645120 VkCallbackObject::unregisterSender(VkCallbackObject *)
0x0d645164 VkCallbackObject::unregisterSenders(VkCallbackObject *)

0x0d636300 VkCallbackList::VkCallbackList()
0x0d62f168 VkCallbackList::~VkCallbackList()
0x0d62f720 VkCallbackList::invoke(VkCallbackObject *, const char *, void *)
0x0d636198 VkCallbackList::add(VkCallbackItem *)
0x0d634a64 VkCallbackList::add(...member function...)
0x0d63f64c VkCallbackList::add(...free function...)

0x0d6349e4 VkCallbackItem::VkCallbackItem(...member function...)
0x0d63f6e4 VkCallbackItem::VkCallbackItem(...free function...)
0x0d62ec58 VkCallbackItem::~VkCallbackItem()
0x0d62f82c VkCallbackItem::invoke(...)
```

## VkCallbackObject layout

The default constructor at `0x0d636388` proves this compact layout:

```text
VkCallbackObject
+0x00 VkCallbackList *callbacks
+0x04 VkCallbackObjectList *senders
+0x08 vtable pointer
```

Constructor behavior is exactly:

```text
callbacks = NULL
senders   = NULL
vptr      = __vtbl__VkCallbackObject
```

The semantic names `callbacks` and `senders` are proven by later member functions:

- `addCallback()` lazily allocates an 8-byte `VkCallbackList` and stores it at `+0x00`.
- `registerSender()` lazily allocates a 12-byte `VkCallbackObjectList` and stores it at `+0x04`.

The `VkCallbackObject` vtable is only three 12-byte SGI ABI records:

```text
+0x000 RTTI/type record
+0x00c destructor
+0x018 className
```

No additional callback-specific virtual dispatch is hidden in this base class.

## VkCallbackList layout

The constructor at `0x0d636300` proves:

```text
VkCallbackList
+0x00 VkCallbackItem **items
+0x04 int count
```

Both fields initialize to zero.

`invoke()` reads `+0x04` as the callback count and `+0x00` as an array of item pointers. Empty/null item entries are skipped.

## VkCallbackItem layout

The member-function constructor at `0x0d6349e4` proves the following storage pattern:

```text
VkCallbackItem
+0x00 VkCallbackObject *receiver
+0x04 char *callbackName
+0x08 int/flag, initialized to 0
+0x0c member-function descriptor word 0
+0x10 member-function descriptor word 1
+0x14 member-function descriptor word 2
+0x18 void *clientData
```

The callback name is duplicated with `strdup()`.

The three-word member-function descriptor is the SGI C++ N32 member-function representation used by this build. Exact semantic names for its component words are intentionally not invented here.

## callCallbacks() is a thin wrapper

`VkCallbackObject::callCallbacks()` at `0x0d62fba8`:

1. loads `this->callbacks` from `+0x00`,
2. returns immediately if it is NULL,
3. otherwise calls `VkCallbackList::invoke(callbacks, this, callbackName, callData)`.

There is no extra callback copying or filtering in `VkCallbackObject::callCallbacks()` itself.

## Callback registration relationship

The member-function `addCallback()` path at `0x0d634c00` proves:

1. `this->callbacks` is allocated lazily as an 8-byte `VkCallbackList`.
2. The callback is added to that list.
3. The receiver object is then told to `registerSender(this)`.

This establishes a bidirectional relationship:

```text
sender VkCallbackObject
  -> owns callback list entries that point at receiver

receiver VkCallbackObject
  -> tracks sender objects in its sender list
```

This relationship is what permits safe cleanup when either side is destroyed.

## Destruction-safety tracker used during invocation

`VkCallbackList::invoke()` at `0x0d62f720` contains a particularly important safety mechanism.

A process-global object is lazily allocated by the `VkCallbackList` constructor. The allocation size is `0x28` bytes and the constructor called for it is:

```text
VkComponentList::VkComponentList()
```

During callback dispatch, `invoke()` uses this global list as an active-list tracker:

```text
tracker->add((VkComponent *)this)

for each callback item:
    item->invoke(sender, callbackName, callData)

    if (!tracker->contains((VkComponent *)this))
        stop dispatch immediately

tracker->removeFirst((VkComponent *)this)
```

The casts above describe the binary call shape only. `VkCallbackList` is not claimed to inherit `VkComponent`; the original implementation reuses `VkComponentList` storage/operations as a generic pointer tracker.

The relevant `VkComponentList` vtable slots are binary-proven:

```text
+0x09c add
+0x0a8 remove
+0x0b4 removeFirst
+0x0c0 removeLast
```

`VkCallbackList::invoke()` calls `add` before iteration, `contains()` after each callback, and `removeFirst` at the end.

### Consequence

Callbacks are allowed to destroy objects or callback-list state during notification. Dispatch explicitly detects that case and stops before dereferencing invalid state.

A portable reconstruction should preserve this behavior. A naive range-for over a `std::vector` is not behaviorally equivalent if callbacks may remove/destroy the sender or its callback list.

## VkCallbackObject destruction

`destroyVkCallbackObject()` at `0x0d62f3b0` walks the sender list at `+0x04`, unregistering/removing this object from tracked senders before clearing sender state, then removes all callbacks at `+0x00`.

This confirms the sender list is cleanup infrastructure, not a second callback namespace.

## Reconstruction guidance

Portable code may use safer C++ containers internally, but it should preserve these original semantics:

1. named callback channels,
2. receiver/client-data callback records,
3. receiver tracks sender relationships,
4. destroying either participant cleans the relationship,
5. callback dispatch tolerates destruction/removal during notification,
6. dispatch stops when the active callback list is destroyed.

Do not reproduce SGI pointer-to-member binary encoding unless implementing an ABI-compatibility layer. For the Linux reconstruction, preserve behavior rather than N32 representation.
