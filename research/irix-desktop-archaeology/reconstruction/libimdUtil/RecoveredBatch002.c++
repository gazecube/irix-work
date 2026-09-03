/*
 * libimdUtil.so decompilation batch 002.
 *
 * Functions 51-75 in the size-ordered leaf-function pass used by batch 001.
 * Compiler destruction-list helpers are intentionally excluded from this
 * application-function pass and will be tracked separately as runtime glue.
 */
#include <stdint.h>
#include <stddef.h>

namespace {
typedef uint32_t word32;
static inline word32 load32(const void *p, size_t off) { return *(const word32 *)((const unsigned char *)p + off); }
static inline void store32(void *p, size_t off, word32 v) { *(word32 *)((unsigned char *)p + off) = v; }
static inline void *p32(word32 v) { return (void *)(uintptr_t)v; }
static inline const void *cp32(word32 v) { return (const void *)(uintptr_t)v; }
static inline word32 w32(const void *p) { return (word32)(uintptr_t)p; }
}

extern "C" {
void put__Q2_8ImdDLink8RecyclerGP8ImdDLink(void *, void *);
extern word32 _list__Q2_8ImdDLink8Recycler;
void *lookup__19ImdDictionaryOfImplCGRC11ImdHashablePPC11ImdHashable(const void *, const void *, const void **);
int __gt__9ImdStringCGPCc(const void *, const char *);
int __lt__9ImdStringCGPCc(const void *, const char *);
int isSame__9ImdStringCGPCc(const void *, const char *);
int __ge__9ImdStringCGPCc(const void *, const char *);
int __le__9ImdStringCGPCc(const void *, const char *);
void *__ct__23ImdRealCollectionOfImplGv(void *);
void *__ct__23ImdRealCollectionOfImplGRC23ImdRealCollectionOfImpl(void *, const void *);
extern unsigned char __vtbl__12ImdAttribute[];
extern unsigned char __vtbl__11ImdAttrLong[];
extern unsigned char __vtbl__15ImdAttrLongLong[];
extern unsigned char __vtbl__13ImdAttrDouble[];
extern unsigned char __vtbl__11ImdAttrBool[];
extern unsigned char __vtbl__19ImdCollectionOfImpl[];
extern unsigned char __vtbl__19ImdIteratorOverImpl[];
extern unsigned char __vtbl__24ImdIteratorOverDLinkImpl[];
extern unsigned char __vtbl__26ImdOrderedCollectionOfImpl[];
}

/* 51 0x0983bc70, 56 */
extern "C" void __dl__8ImdDLinkSGPv(void *value)
{ put__Q2_8ImdDLink8RecyclerGP8ImdDLink(&_list__Q2_8ImdDLink8Recycler, value); }

/* 52 0x0983c018, 56 */
extern "C" void *__vc__20ImdDictionaryOfTemplGRC11ImdHashable(void *self, const void *key)
{ return lookup__19ImdDictionaryOfImplCGRC11ImdHashablePPC11ImdHashable(cp32(load32(self,0)), key, 0); }
/* 53 0x0983c050, 56 */
extern "C" const void *__vc__20ImdDictionaryOfTemplCGRC11ImdHashable(const void *self, const void *key)
{ return lookup__19ImdDictionaryOfImplCGRC11ImdHashablePPC11ImdHashable(cp32(load32(self,0)), key, 0); }

static inline const char *imdStringChars(const void *s)
{ const void *rep=cp32(load32(s,4)); return (const char *)cp32(load32(rep,0)); }
/* 54 0x0984049c, 56 */
extern "C" int __gt__9ImdStringCGRC9ImdString(const void *self,const void *other)
{ return __gt__9ImdStringCGPCc(self,imdStringChars(other)); }
/* 55 0x098404d4, 56 */
extern "C" int __lt__9ImdStringCGRC9ImdString(const void *self,const void *other)
{ return __lt__9ImdStringCGPCc(self,imdStringChars(other)); }
/* 56 0x0984050c, 56 */
extern "C" int isSame__9ImdStringCGRC9ImdString(const void *self,const void *other)
{ return isSame__9ImdStringCGPCc(self,imdStringChars(other)); }
/* 57 0x09840998, 56 */
extern "C" int __ge__9ImdStringCGRC9ImdString(const void *self,const void *other)
{ return __ge__9ImdStringCGPCc(self,imdStringChars(other)); }
/* 58 0x098409d0, 56 */
extern "C" int __le__9ImdStringCGRC9ImdString(const void *self,const void *other)
{ return __le__9ImdStringCGPCc(self,imdStringChars(other)); }

typedef int (*SgiBoolMethod)(void *, const void *);
static int callSgiBoolVirtual(const void *self, size_t adjOff, size_t fnOff, const void *arg)
{
    const unsigned char *vt=(const unsigned char *)p32(load32(self,0));
    int32_t adj=(int32_t)load32(vt,adjOff);
    SgiBoolMethod fn=(SgiBoolMethod)(uintptr_t)load32(vt,fnOff);
    return fn((unsigned char *)const_cast<void *>(self)+adj,arg);
}

/* 59 0x09839bd8, 64 */
extern "C" int __ne__12ImdAttributeCGRC12ImdAttribute(const void *self,const void *other)
{
    const unsigned char *vt=(const unsigned char *)p32(load32(self,4));
    int32_t adj=(int32_t)load32(vt,0x18);
    SgiBoolMethod fn=(SgiBoolMethod)(uintptr_t)load32(vt,0x20);
    return !fn((unsigned char *)const_cast<void *>(self)+adj,other);
}

static inline void deletingDtor(void *self,int deleting)
{ if (self && (deleting & 1)) ::operator delete(self); }
/* 60 0x0983bcbc, 64 */
extern "C" void __dt__14ImdDeleterBaseGv(void *self,int deleting) { deletingDtor(self,deleting); }
/* 61 0x0983cd6c, 64 */
extern "C" void __dt__20ImdDictionaryOfTImplGv(void *self,int deleting) { deletingDtor(self,deleting); }
/* 62 0x0983d6f0, 64 */
extern "C" int __ne__13ImdPointerKeyCGRC11ImdHashable(const void *self,const void *other)
{ return !callSgiBoolVirtual(self,0x18,0x20,other); }
/* 63 0x09840544, 64 */
extern "C" int __ne__9ImdStringCGRC11ImdHashable(const void *self,const void *other)
{ return !callSgiBoolVirtual(self,0x18,0x20,other); }

/* 64 0x098397f4, 68 */
typedef void *(*SgiOutputMethod)(void *, void *);
extern "C" void *__ls__GR7ostreamRC12ImdAttribute(void *out,const void *attr)
{
    const unsigned char *vt=(const unsigned char *)p32(load32(attr,4));
    int32_t adj=(int32_t)load32(vt,0x48);
    SgiOutputMethod fn=(SgiOutputMethod)(uintptr_t)load32(vt,0x50);
    return fn((unsigned char *)const_cast<void *>(attr)+adj,out);
}

/* 65 0x0983b21c, 68 */
extern "C" void *__ct__26ImdOrderedCollectionOfImplGv(void *self)
{ __ct__23ImdRealCollectionOfImplGv(self); store32(self,0,w32(__vtbl__26ImdOrderedCollectionOfImpl)); return self; }
/* 66 0x0983b260, 68 */
extern "C" void *__ct__26ImdOrderedCollectionOfImplGRC26ImdOrderedCollectionOfImpl(void *self,const void *other)
{ __ct__23ImdRealCollectionOfImplGRC23ImdRealCollectionOfImpl(self,other); store32(self,0,w32(__vtbl__26ImdOrderedCollectionOfImpl)); return self; }

/* 67 0x0983d38c, 68 */
extern "C" void __dt__9ImdHandleGv(void *self,int deleting)
{ if (!self) return; store32(self,0,0); deletingDtor(self,deleting); }

static inline void attrDtor(void *self,int deleting,void *vt)
{ if (!self) return; store32(self,4,w32(vt)); deletingDtor(self,deleting); }
/* 68 0x09839b24, 72 */
extern "C" void __dt__12ImdAttributeGv(void *self,int deleting) { attrDtor(self,deleting,__vtbl__12ImdAttribute); }
/* 69 0x09839d14, 72 */
extern "C" void __dt__11ImdAttrLongGv(void *self,int deleting) { attrDtor(self,deleting,__vtbl__11ImdAttrLong); }
/* 70 0x09839f70, 72 */
extern "C" void __dt__15ImdAttrLongLongGv(void *self,int deleting) { attrDtor(self,deleting,__vtbl__15ImdAttrLongLong); }
/* 71 0x0983a1cc, 72 */
extern "C" void __dt__13ImdAttrDoubleGv(void *self,int deleting) { attrDtor(self,deleting,__vtbl__13ImdAttrDouble); }
/* 72 0x0983a434, 72 */
extern "C" void __dt__11ImdAttrBoolGv(void *self,int deleting) { attrDtor(self,deleting,__vtbl__11ImdAttrBool); }
/* 73 0x0983aaa0, 72 */
extern "C" void __dt__19ImdCollectionOfImplGv(void *self,int deleting)
{ if(!self) return; store32(self,0,w32(__vtbl__19ImdCollectionOfImpl)); deletingDtor(self,deleting); }
/* 74 0x0983b440, 72 */
extern "C" void __dt__19ImdIteratorOverImplGv(void *self,int deleting)
{ if(!self) return; store32(self,4,w32(__vtbl__19ImdIteratorOverImpl)); deletingDtor(self,deleting); }
/* 75 0x0983b488, 72 */
extern "C" void __dt__24ImdIteratorOverDLinkImplGv(void *self,int deleting)
{ if(!self) return; store32(self,4,w32(__vtbl__24ImdIteratorOverDLinkImpl)); deletingDtor(self,deleting); }
