add_library(irix_recomp_runtime STATIC
    runtime/IrixGuest.c
)

target_include_directories(irix_recomp_runtime PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/runtime
)

set_target_properties(irix_recomp_runtime PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
