add_library(imdUtil SHARED
    c++init.cxx
    MemoryMap.c++
)

# RecoveredBatch001.c++ and RecoveredBatch002.c++ are binary-archaeology
# scaffolding recovered from the original IRIX/N32 libimdUtil.so. They model
# SGI object state using 32-bit pointer slots and raw SGI vtable layouts. That
# representation is not ABI-safe in the native x86-64 host build, and the
# batches intentionally still reference unrecovered SGI support symbols.
#
# Keep them in-tree as evidence/recovery material, but do not link them into
# the portable libimdUtil until their classes are reconstructed with native
# host layouts and the original semantics are restored behind that interface.

set_target_properties(imdUtil PROPERTIES
    OUTPUT_NAME imdUtil
    VERSION 1.0.0
    SOVERSION 1
)
