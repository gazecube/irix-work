add_library(irix_recomp_runtime STATIC
    runtime/IrixGuest.c
    runtime/IrixElf.c
)

target_include_directories(irix_recomp_runtime PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/runtime
)

set_target_properties(irix_recomp_runtime PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

set(IRIX_DEV_TREE "" CACHE PATH "Path to an extracted IRIX development tree")

if(IRIX_DEV_TREE)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(IRIX_RECOMP_CORE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated-core")
    set(IRIX_RECOMP_CORE_LIB "${IRIX_RECOMP_CORE_DIR}/libirix_recompiled_stack.a")

    add_custom_command(
        OUTPUT "${IRIX_RECOMP_CORE_LIB}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${IRIX_RECOMP_CORE_DIR}"
        COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/build_core_stack.py"
                "${IRIX_DEV_TREE}"
                -o "${IRIX_RECOMP_CORE_DIR}"
                --cc "${CMAKE_C_COMPILER}"
                -j 1
        DEPENDS
            tools/build_core_stack.py
            tools/recomp_linked_stack.py
            tools/link_stack_n32.py
            tools/translate_n32.py
            tools/extract_ar.py
            runtime/IrixGuest.h
        VERBATIM
    )

    add_custom_target(irix_recompiled_core ALL
        DEPENDS "${IRIX_RECOMP_CORE_LIB}"
    )

    add_library(irix_recompiled_stack STATIC IMPORTED GLOBAL)
    set_target_properties(irix_recompiled_stack PROPERTIES
        IMPORTED_LOCATION "${IRIX_RECOMP_CORE_LIB}"
    )
    add_dependencies(irix_recompiled_stack irix_recompiled_core)
endif()
