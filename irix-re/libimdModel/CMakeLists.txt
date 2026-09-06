add_library(imdModel SHARED
    c++init.cxx
    Base/ImdString.c++
    Base/ImdDNA.c++
)

target_include_directories(imdModel
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/Base"
)

target_link_libraries(imdModel PRIVATE imdUtil)

set_target_properties(imdModel PROPERTIES
    OUTPUT_NAME imdModel
    VERSION 1.0.0
    SOVERSION 1
)

set_property(TARGET imdModel PROPERTY CXX_STANDARD 11)
set_property(TARGET imdModel PROPERTY CXX_STANDARD_REQUIRED ON)
