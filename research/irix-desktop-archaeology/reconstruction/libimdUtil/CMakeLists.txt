add_library(imdUtil SHARED
    c++init.cxx
    decomp/imdMkdirP.c++
)

set_target_properties(imdUtil PROPERTIES
    OUTPUT_NAME imdUtil
    VERSION 1.0.0
    SOVERSION 1
)
