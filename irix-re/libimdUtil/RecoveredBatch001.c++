/*
 * libimdUtil.so decompilation batch 001.
 *
 * These routines are reconstructed directly from the original IRIX/MIPS N32
 * machine code. For class methods whose full C++ type declarations are not yet
 * recovered, the source intentionally models the observed 32-bit object slots
 * instead of inventing modern class layouts.
 */

#include <stdint.h>
#include <stddef.h>

namespace {
typedef uint32_t word32;
static inline word32 load32(const void *p, size_t off) { return *(const word32 *)((const unsigned char *)p + off); }
static inline void store32(void *p, size_t off, word32 v) { *(word32 *)((unsigned char *)p + off) = v; }
static inline void store8(void *p, size_t off, unsigned char v) { *((unsigned char *)p + off) = v; }
static inline void *p32(word32 v) { return (void *)(uintptr_t)v; }
static inline const void *cp32(word32 v) { return (const void *)(uintptr_t)v; }
static inline word32 w32(const void *p) { return (word32)(uintptr_t)p; }
}

extern "C" {
void *fetch__Q2_8ImdDLink8RecyclerGv(void *);
int add__19ImdDictionaryOfImplGPvP11ImdHashable(void *, void *, void *);
int remove__19ImdDictionaryOfImplGRC11ImdHashable(void *, const void *);
void *lookup__19ImdDictionaryOfImplCGRC11ImdHashablePPC11ImdHashable(const void *, const void *, const void **);
void reset__20ImdDictionaryOfTImplGv(void *);
void *__cl__20ImdDictionaryOfTImplGv(void *);
void *remove__20ImdDictionaryOfTImplGv(void *);
int __eq__9ImdStringCGPCc(const void *, const char *);
int __ne__9ImdStringCGPCc(const void *, const char *);
void *insertBefore__23ImdRealCollectionOfImplGP8ImdDLinkPv(void *, void *, void *);
void *insertAfter__23ImdRealCollectionOfImplGP8ImdDLinkPv(void *, void *, void *);
extern unsigned char __vtbl__12ImdAttribute[];
extern word32 _list__Q2_8ImdDLink8Recycler;
}

/* 01 0x09839c88, 8 */
extern "C" word32 getName__12ImdAttributeCGv(const void *self) { return load32(self, 0); }
/* 02 0x0983a954, 8 */
extern "C" void *value__13ImdAttrStringCGv(void *self) { return (unsigned char *)self + 8; }
/* 03 0x0983ad28, 8 */
extern "C" word32 getSize__23ImdRealCollectionOfImplCGv(const void *self) { return load32(self, 8); }
/* 04 0x0983bda0, 8 */
extern "C" void *get__14ImdDeleterBaseCGv(const void *self) { return p32(load32(self, 0)); }
/* 05 0x0983d620, 8 */
extern "C" void doNothing__11ImdHashableGv(void *) {}
/* 06 0x0983f028, 8 */
extern "C" void *__opPv__12ImdMemoryMapGv(void *self) { return p32(load32(self, 0)); }
/* 07 0x0983f030, 8 */
extern "C" char *__opPc__12ImdMemoryMapGv(void *self) { return (char *)p32(load32(self, 0)); }
/* 08 0x0983f038, 8 */
extern "C" word32 getLength__12ImdMemoryMapCGv(const void *self) { return load32(self, 4); }
/* 09 0x0983b65c, 12 */
extern "C" void reset__24ImdIteratorOverDLinkImplGv(void *self) { store8(self, 12, 0); store32(self, 8, 0); }
/* 10 0x0983b668, 12 */
extern "C" void resetToEnd__24ImdIteratorOverDLinkImplGv(void *self) { store8(self, 12, 0); store32(self, 8, 0); }
/* 11 0x0983bcb0, 12 */
extern "C" void *__ct__14ImdDeleterBaseGPv(void *self, void *value) { store32(self, 0, w32(value)); return self; }
/* 12 0x0983bd94, 12 */
extern "C" void *orphanValue__14ImdDeleterBaseGv(void *self) { void *v=p32(load32(self,0)); store32(self,0,0); return v; }
/* 13 0x0983c088, 12 */
extern "C" word32 getSize__20ImdDictionaryOfTemplCGv(const void *self) { return load32(cp32(load32(self,0)),8); }
/* 14 0x0983c094, 12 */
extern "C" word32 getBucketOverflow__20ImdDictionaryOfTemplCGv(const void *self) { return load32(cp32(load32(self,0)),12); }
/* 15 0x0983c0a0, 12 */
extern "C" word32 getMaxChain__20ImdDictionaryOfTemplCGv(const void *self) { return load32(cp32(load32(self,0)),16); }
/* 16 0x0983e594, 12 */
extern "C" void *getFile__11ImdInitFileGv(void *self) { return p32(load32(cp32(load32(self,0)),0)); }
/* 17 0x09839d04, 16 */
extern "C" void *assignFrom__12ImdAttributeGRC12ImdAttribute(void *self, const void *other) { store32(self,0,load32(other,0)); return self; }
/* 18 0x0983c1f0, 16 */
extern "C" void *key__28ImdDictionaryOfTemplIteratorGv(void *self) { void *i=p32(load32(self,0)); void *n=p32(load32(i,8)); return p32(load32(n,0)); }
/* 19 0x0983d4f0, 16 */
extern "C" void *__as__9ImdHandleGRC9ImdHandle(void *self, const void *other) { store32(self,0,load32(other,0)); return self; }
/* 20 0x0983b02c, 24 */
extern "C" void *getData__23ImdRealCollectionOfImplGP8ImdDLink(void *, void *link) { return link ? p32(load32(link,8)) : 0; }
/* 21 0x0983d538, 24 */
extern "C" void setValue__9ImdHandleGPv(void *self, void *value) { void *h=p32(load32(self,0)); if(h) store32(h,0,w32(value)); }
/* 22 0x0983f040, 24 */
extern "C" int _mapModeToFileMode__12ImdMemoryMapSGi(int mode) { return (mode & 2) ? 2 : 0; }
/* 23 0x0983d500, 28 */
extern "C" void *getValue__9ImdHandleGv(void *self) { void *h=p32(load32(self,0)); return h ? p32(load32(h,0)) : 0; }
/* 24 0x0983d51c, 28 */
extern "C" const void *getValue__9ImdHandleCGv(const void *self) { const void *h=cp32(load32(self,0)); return h ? cp32(load32(h,0)) : 0; }
/* 25 0x098387e0, 32 */
extern "C" void *__ct__Q2_9ImdString9StringRepGv(void *self) { store32(self,0,0); store32(self,4,1); *(uint16_t *)((unsigned char*)self+8)=0; *(uint16_t *)((unsigned char*)self+10)=0xffff; return self; }
/* 26 0x09839a90, 32 */
extern "C" void *__ct__12ImdAttributeGUi(void *self, word32 name) { store32(self,0,name); store32(self,4,w32(__vtbl__12ImdAttribute)); return self; }
/* 27 0x09839ab0, 36 */
extern "C" void *__ct__12ImdAttributeGRC12ImdAttribute(void *self, const void *other) { store32(self,4,w32(__vtbl__12ImdAttribute)); store32(self,0,load32(other,0)); return self; }
/* 28 0x0983bbc4, 36 */
extern "C" void insertBefore__8ImdDLinkGP8ImdDLink(void *self, void *node) { void *prev=p32(load32(self,0)); if(prev) store32(prev,4,w32(node)); store32(node,4,w32(self)); store32(node,0,w32(prev)); store32(self,0,w32(node)); }
/* 29 0x0983bbe8, 36 */
extern "C" void insertAfter__8ImdDLinkGP8ImdDLink(void *self, void *node) { void *next=p32(load32(self,4)); if(next) store32(next,0,w32(node)); store32(node,0,w32(self)); store32(node,4,w32(next)); store32(self,4,w32(node)); }
/* 30 0x0983af6c, 44 */
extern "C" void *getNext__23ImdRealCollectionOfImplGP8ImdDLink(void *self, void *link) { if(!link) return 0; void *n=p32(load32(link,4)); return n==p32(load32(self,4)) ? 0 : n; }
/* 31 0x0983af98, 44 */
extern "C" void *getPrev__23ImdRealCollectionOfImplGP8ImdDLink(void *self, void *link) { if(!link) return 0; void *p=p32(load32(link,0)); return p==p32(load32(self,4)) ? 0 : p; }
/* 32 0x0983d3d0, 44 */
extern "C" word32 addReference__9ImdHandleGv(void *self) { void *h=p32(load32(self,0)); if(!h) return 0; word32 n=load32(h,4)+1; store32(h,4,n); return n; }
/* 33 0x0983d550, 44 */
extern "C" word32 removeReference__9ImdHandleGv(void *self) { void *h=p32(load32(self,0)); if(!h) return 0; word32 n=load32(h,4)-1; store32(h,4,n); return n; }
/* 34 0x0983bc0c, 48 */
extern "C" void remove__8ImdDLinkGv(void *self) { void *p=p32(load32(self,0)), *n=p32(load32(self,4)); if(p) store32(p,4,w32(n)); if(n) store32(n,0,w32(p)); }
/* 35 0x0983afc4, 52 */
extern "C" void *getFirst__23ImdRealCollectionOfImplGv(void *self) { return getNext__23ImdRealCollectionOfImplGP8ImdDLink(self,p32(load32(self,4))); }
/* 36 0x0983aff8, 52 */
extern "C" void *getLast__23ImdRealCollectionOfImplGv(void *self) { return getPrev__23ImdRealCollectionOfImplGP8ImdDLink(self,p32(load32(self,4))); }
/* 37 0x0983bc3c, 52 */
extern "C" void *__nw__8ImdDLinkSGUi(unsigned int) { return fetch__Q2_8ImdDLink8RecyclerGv(&_list__Q2_8ImdDLink8Recycler); }
/* 38 0x0983bf48, 52 */
extern "C" int add__20ImdDictionaryOfTemplGPvP11ImdHashable(void *self, void *value, void *key) { return add__19ImdDictionaryOfImplGPvP11ImdHashable(p32(load32(self,0)),value,key); }
/* 39 0x0983bf7c, 52 */
extern "C" int remove__20ImdDictionaryOfTemplGRC11ImdHashable(void *self, const void *key) { return remove__19ImdDictionaryOfImplGRC11ImdHashable(p32(load32(self,0)),key); }
/* 40 0x0983bfb0, 52 */
extern "C" void *lookupByKey__20ImdDictionaryOfTemplGRC11ImdHashablePPC11ImdHashable(void *self,const void *key,const void **found) { return lookup__19ImdDictionaryOfImplCGRC11ImdHashablePPC11ImdHashable(cp32(load32(self,0)),key,found); }
/* 41 0x0983bfe4, 52 */
extern "C" void *lookupByKey__20ImdDictionaryOfTemplCGRC11ImdHashablePPC11ImdHashable(const void *self,const void *key,const void **found) { return lookup__19ImdDictionaryOfImplCGRC11ImdHashablePPC11ImdHashable(cp32(load32(self,0)),key,found); }
/* 42 0x0983c188, 52 */
extern "C" void reset__28ImdDictionaryOfTemplIteratorGv(void *self) { reset__20ImdDictionaryOfTImplGv(p32(load32(self,0))); }
/* 43 0x0983c1bc, 52 */
extern "C" void *__cl__28ImdDictionaryOfTemplIteratorGv(void *self) { return __cl__20ImdDictionaryOfTImplGv(p32(load32(self,0))); }
/* 44 0x0983c200, 52 */
extern "C" void *remove__28ImdDictionaryOfTemplIteratorGv(void *self) { return remove__20ImdDictionaryOfTImplGv(p32(load32(self,0))); }
/* 45 0x09837f18, 56 */
extern "C" int __ne__9ImdStringCGRC9ImdString(const void *self,const void *other) { const void *rep=cp32(load32(other,4)); return __ne__9ImdStringCGPCc(self,(const char*)cp32(load32(rep,0))); }
/* 46 0x09837f50, 56 */
extern "C" int __eq__9ImdStringCGRC9ImdString(const void *self,const void *other) { const void *rep=cp32(load32(other,4)); return __eq__9ImdStringCGPCc(self,(const char*)cp32(load32(rep,0))); }
/* 47 0x0983815c, 56 */
extern "C" int __ne__9ImdStringCGPCc_compat(const void *self,const char *s) __asm__("__ne__9ImdStringCGPCc");
extern "C" int __ne__9ImdStringCGPCc_compat(const void *self,const char *s) { return __eq__9ImdStringCGPCc(self,s) ? 0 : 1; }
/* 48 0x0983ad30, 56 */
extern "C" void *add__23ImdRealCollectionOfImplGPv(void *self,void *value) { return insertBefore__23ImdRealCollectionOfImplGP8ImdDLinkPv(self,p32(load32(self,4)),value); }
/* 49 0x0983b3d0, 56 */
extern "C" void *append__26ImdOrderedCollectionOfImplGPv(void *self,void *value) { return insertBefore__23ImdRealCollectionOfImplGP8ImdDLinkPv(self,p32(load32(self,4)),value); }
/* 50 0x0983b408, 56 */
extern "C" void *prepend__26ImdOrderedCollectionOfImplGPv(void *self,void *value) { return insertAfter__23ImdRealCollectionOfImplGP8ImdDLinkPv(self,p32(load32(self,4)),value); }
