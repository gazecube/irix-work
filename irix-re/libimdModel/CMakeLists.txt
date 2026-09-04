add_library(imdModel SHARED
    c++init.cxx
)

target_link_libraries(imdModel PRIVATE imdUtil)

set_target_properties(imdModel PROPERTIES
    OUTPUT_NAME imdModel
    VERSION 1.0.0
    SOVERSION 1
)
