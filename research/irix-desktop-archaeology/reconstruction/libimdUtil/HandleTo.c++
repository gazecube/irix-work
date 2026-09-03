/* Decompiled reconstruction from IRIX libimdUtil.so. */
#include <cstdlib>

struct ImdHandleRep {
    void *value;
    long references;
};
struct ImdHandleLayout { ImdHandleRep *rep; };
static ImdHandleRep emptyRep = { 0, 0 };

extern "C" long addReference__9ImdHandleGv(ImdHandleLayout *self)
{
    if (!self->rep) return 0;
    ++self->rep->references;
    return self->rep->references;
}
extern "C" ImdHandleLayout *__ct__9ImdHandleGv(ImdHandleLayout *self)
{
    self->rep = &emptyRep;
    addReference__9ImdHandleGv(self);
    return self;
}
extern "C" void __dt__9ImdHandleGv(ImdHandleLayout *self, int deleting)
{
    if (!self) return;
    self->rep = 0;
    if (deleting & 1) ::operator delete(self);
}
extern "C" ImdHandleLayout *__ct__9ImdHandleGPv(ImdHandleLayout *self, void *value)
{
    ImdHandleRep *rep = new ImdHandleRep;
    rep->references = 0;
    rep->value = value;
    self->rep = rep;
    if (!value) {
        delete rep;
        self->rep = &emptyRep;
    }
    addReference__9ImdHandleGv(self);
    return self;
}
extern "C" ImdHandleLayout *__ct__9ImdHandleGRC9ImdHandle(ImdHandleLayout *self, const ImdHandleLayout *other)
{
    self->rep = other->rep;
    addReference__9ImdHandleGv(self);
    return self;
}
extern "C" ImdHandleLayout *__as__9ImdHandleGRC9ImdHandle(ImdHandleLayout *self, const ImdHandleLayout *other)
{
    self->rep = other->rep;
    return self;
}
extern "C" void *getValue__9ImdHandleGv(ImdHandleLayout *self)
{ return self->rep ? self->rep->value : 0; }
extern "C" void *getValue__9ImdHandleCGv(const ImdHandleLayout *self)
{ return self->rep ? self->rep->value : 0; }
extern "C" void setValue__9ImdHandleGPv(ImdHandleLayout *self, void *value)
{ if (self->rep) self->rep->value = value; }
extern "C" long removeReference__9ImdHandleGv(ImdHandleLayout *self)
{
    if (!self->rep) return 0;
    --self->rep->references;
    return self->rep->references;
}
extern "C" void destroy__9ImdHandleGv(ImdHandleLayout *self)
{
    if (self->rep == &emptyRep) self->rep = 0;
    else { delete self->rep; self->rep = 0; }
}
