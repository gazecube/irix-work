/* Decompiled reconstruction from IRIX libimdUtil.so. Batch 01 subset. */
#include <cstdio>
#include <cstdlib>
#include <cstring>

struct ImdStringLayout { void *rep; };
struct DictLayout { void *opaque; };
struct InitFileImpl { FILE *file; DictLayout *values; DictLayout *rawValues; };
struct ImdInitFileLayout { InitFileImpl *impl; void *vtable; };

extern "C" void *__ct__20ImdDictionaryOfTemplGUi(DictLayout *, unsigned int);
extern "C" void __dt__20ImdDictionaryOfTemplGv(DictLayout *, int);
extern "C" const char *__opPCc__9ImdStringCGv(const ImdStringLayout *);
extern "C" void *__ct__9ImdStringGv(ImdStringLayout *);
extern "C" void *__ct__9ImdStringGPCc(ImdStringLayout *, const char *);
extern "C" void __dt__9ImdStringGv(ImdStringLayout *, int);
extern "C" ImdStringLayout *imdStringFromBool__Gb(ImdStringLayout *, int);
extern "C" ImdStringLayout *imdStringFromLong__Gl(ImdStringLayout *, long);
extern "C" ImdStringLayout *imdStringFromDouble__Gd(ImdStringLayout *, double);
extern "C" ImdStringLayout *imdStringFromLongPair__GlT1(ImdStringLayout *, long, long);
extern "C" int writeLine__11ImdInitFileGRC9ImdStringT1(ImdInitFileLayout *, const ImdStringLayout *, const ImdStringLayout *);
extern "C" int writeBuffer__11ImdInitFileGPcUl(ImdInitFileLayout *, char *, unsigned long);
extern "C" int findKey__11ImdInitFileGRC9ImdStringR9ImdString(ImdInitFileLayout *, const ImdStringLayout *, ImdStringLayout *);
extern "C" int imdStringToBool__GRC9ImdStringRb(const ImdStringLayout *, bool *);
extern "C" long asLong__9ImdStringCGi(const ImdStringLayout *, int);
extern "C" void readFile__11ImdInitFileGv(ImdInitFileLayout *);

static InitFileImpl *allocImpl()
{
    InitFileImpl *p = new InitFileImpl;
    p->file = 0;
    p->values = new DictLayout;
    __ct__20ImdDictionaryOfTemplGUi(p->values, 251);
    p->rawValues = new DictLayout;
    __ct__20ImdDictionaryOfTemplGUi(p->rawValues, 251);
    return p;
}

extern "C" ImdInitFileLayout *__ct__11ImdInitFileGRC9ImdString(ImdInitFileLayout *self, const ImdStringLayout *name)
{
    self->impl = allocImpl();
    self->impl->file = fopen(__opPCc__9ImdStringCGv(name), "r");
    if (self->impl->file) readFile__11ImdInitFileGv(self);
    return self;
}
extern "C" ImdInitFileLayout *__ct__11ImdInitFileGRC9ImdStringb(ImdInitFileLayout *self, const ImdStringLayout *name, bool append)
{
    self->impl = allocImpl();
    self->impl->file = fopen(__opPCc__9ImdStringCGv(name), append ? "wa" : "w");
    return self;
}
extern "C" void __dt__11ImdInitFileGv(ImdInitFileLayout *self, int deleting)
{
    if (!self) return;
    if (self->impl) {
        if (self->impl->file) fclose(self->impl->file);
        /*
         * PARTIAL: the original MIPS destructor iterates both dictionaries.
         * One loop invokes a virtual deleting destructor on each value and
         * the other deletes raw value pointers. The erased element types and
         * SGI vtable slot types have not yet been recovered, so those loops
         * are intentionally not fabricated here.
         */
        if (self->impl->values) { __dt__20ImdDictionaryOfTemplGv(self->impl->values, 3); delete self->impl->values; }
        if (self->impl->rawValues) { __dt__20ImdDictionaryOfTemplGv(self->impl->rawValues, 3); delete self->impl->rawValues; }
        delete self->impl;
        self->impl = 0;
    }
    if (deleting & 1) ::operator delete(self);
}
static bool writable(const ImdInitFileLayout *self) { return self && self->impl && self->impl->file; }
extern "C" int writeValue__11ImdInitFileGRC9ImdStringb(ImdInitFileLayout *self,const ImdStringLayout *key,bool value)
{
    if (!writable(self)) return 0; ImdStringLayout s; imdStringFromBool__Gb(&s,value); int r=writeLine__11ImdInitFileGRC9ImdStringT1(self,key,&s); __dt__9ImdStringGv(&s,2); return !!r;
}
extern "C" int writeValue__11ImdInitFileGRC9ImdStringl(ImdInitFileLayout *self,const ImdStringLayout *key,long value)
{
    if (!writable(self)) return 0; ImdStringLayout s; imdStringFromLong__Gl(&s,value); int r=writeLine__11ImdInitFileGRC9ImdStringT1(self,key,&s); __dt__9ImdStringGv(&s,2); return !!r;
}
extern "C" int writeValue__11ImdInitFileGRC9ImdStringf(ImdInitFileLayout *self,const ImdStringLayout *key,float value)
{
    if (!writable(self)) return 0; ImdStringLayout s; imdStringFromDouble__Gd(&s,(double)value); int r=writeLine__11ImdInitFileGRC9ImdStringT1(self,key,&s); __dt__9ImdStringGv(&s,2); return !!r;
}
extern "C" int writeValue__11ImdInitFileGRC9ImdStringT1(ImdInitFileLayout *self,const ImdStringLayout *key,const ImdStringLayout *value)
{ return writable(self) ? writeLine__11ImdInitFileGRC9ImdStringT1(self,key,value) : 0; }
extern "C" int writeValue__11ImdInitFileGRC9ImdStringlT2(ImdInitFileLayout *self,const ImdStringLayout *key,long a,long b)
{
    if (!writable(self)) return 0; ImdStringLayout s; imdStringFromLongPair__GlT1(&s,a,b); int r=writeLine__11ImdInitFileGRC9ImdStringT1(self,key,&s); __dt__9ImdStringGv(&s,2); return !!r;
}
extern "C" int writeValue__11ImdInitFileGRC9ImdStringPcUl(ImdInitFileLayout *self,const ImdStringLayout *key,char *data,unsigned long size)
{
    if (!writable(self) || !data || !size) return 0;
    char text[128]; std::sprintf(text,"Raw %lu",size); ImdStringLayout s; __ct__9ImdStringGPCc(&s,text);
    int r=writeLine__11ImdInitFileGRC9ImdStringT1(self,key,&s); __dt__9ImdStringGv(&s,2);
    if (r) r=writeBuffer__11ImdInitFileGPcUl(self,data,size); return !!r;
}
extern "C" int findValue__11ImdInitFileGRC9ImdStringRbb(ImdInitFileLayout *self,const ImdStringLayout *key,bool *out,bool def)
{
    *out=def; if (!self || !self->impl || !self->impl->file) return 0;
    ImdStringLayout s; __ct__9ImdStringGv(&s); int ok=0;
    if (findKey__11ImdInitFileGRC9ImdStringR9ImdString(self,key,&s) && imdStringToBool__GRC9ImdStringRb(&s,out)) ok=1;
    __dt__9ImdStringGv(&s,2); return ok;
}
extern "C" int findValue__11ImdInitFileGRC9ImdStringRll(ImdInitFileLayout *self,const ImdStringLayout *key,long *out,long def)
{
    *out=def; if (!self || !self->impl || !self->impl->file) return 0;
    ImdStringLayout s; __ct__9ImdStringGv(&s); int ok=0;
    if (findKey__11ImdInitFileGRC9ImdStringR9ImdString(self,key,&s)) { *out=asLong__9ImdStringCGi(&s,10); ok=1; }
    __dt__9ImdStringGv(&s,2); return ok;
}
