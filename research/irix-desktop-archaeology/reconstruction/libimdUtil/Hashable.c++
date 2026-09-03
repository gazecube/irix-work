/* Decompiled reconstruction from IRIX libimdUtil.so. */
struct ImdHashableNative { virtual ~ImdHashableNative() {} virtual void doNothing() {} };
struct ImdPointerKeyNative : ImdHashableNative { void *key; };
extern "C" void doNothing__11ImdHashableGv(void *) {}
extern "C" int __eq__13ImdPointerKeyCGRC11ImdHashable(const ImdPointerKeyNative *self, const ImdHashableNative *other)
{
    if (!other) return 0;
    const ImdPointerKeyNative *rhs = dynamic_cast<const ImdPointerKeyNative *>(other);
    return rhs && rhs->key == self->key;
}
extern "C" int __ne__13ImdPointerKeyCGRC11ImdHashable(const ImdPointerKeyNative *self, const ImdHashableNative *other)
{ return !__eq__13ImdPointerKeyCGRC11ImdHashable(self, other); }
