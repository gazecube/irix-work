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

add_library(irix_recomp_libc_bridge STATIC
    runtime/IrixLibcBridge.c
)
target_include_directories(irix_recomp_libc_bridge PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/runtime
)
target_link_libraries(irix_recomp_libc_bridge PUBLIC
    irix_recomp_runtime
)
set_target_properties(irix_recomp_libc_bridge PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

find_package(X11)
if(X11_FOUND)
    add_library(irix_recomp_x11_bridge STATIC
        runtime/IrixX11Bridge.c
    )
    target_include_directories(irix_recomp_x11_bridge PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/runtime
    )
    target_link_libraries(irix_recomp_x11_bridge PUBLIC
        irix_recomp_runtime
        X11::X11
    )
    set_target_properties(irix_recomp_x11_bridge PROPERTIES
        POSITION_INDEPENDENT_CODE ON
    )
endif()

set(IRIX_DEV_TREE "" CACHE PATH "Path to an extracted IRIX development tree")
set(IRIX_RUNTIME_TREE "" CACHE PATH "Path to an extracted IRIX runtime tree")
set(IRIX_INTERNAL_TREE "" CACHE PATH "Path to a tree containing usr/lib32/internal")

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
        DEPENDS
            tools/build_core_stack.py
            tools/recomp_linked_stack.py
            tools/link_stack_n32.py
            tools/translate_n32.py
            tools/translate_n32_core.py
            tools/extract_ar.py
            runtime/IrixGuest.h
        VERBATIM
    )

    add_custom_target(irix_recompiled_core ALL DEPENDS "${IRIX_RECOMP_CORE_LIB}")
    add_library(irix_recompiled_stack STATIC IMPORTED GLOBAL)
    set_target_properties(irix_recompiled_stack PROPERTIES IMPORTED_LOCATION "${IRIX_RECOMP_CORE_LIB}")
    add_dependencies(irix_recompiled_stack irix_recompiled_core)

    if(IRIX_RUNTIME_TREE AND IRIX_INTERNAL_TREE)
        set(IRIX_RECOMP_DESKTOP_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated-desktop")
        set(IRIX_RECOMP_DESKTOP_LIB "${IRIX_RECOMP_DESKTOP_DIR}/libirix_desktop_bundle.a")
        set(IRIX_RECOMP_CORE_LINK "${IRIX_RECOMP_CORE_DIR}/generated/image/link.json")

        add_custom_command(
            OUTPUT "${IRIX_RECOMP_DESKTOP_LIB}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${IRIX_RECOMP_DESKTOP_DIR}"
            COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tools/build_desktop_bundle.py"
                    --runtime-tree "${IRIX_RUNTIME_TREE}"
                    --internal-tree "${IRIX_INTERNAL_TREE}"
                    --core-link "${IRIX_RECOMP_CORE_LINK}"
                    -o "${IRIX_RECOMP_DESKTOP_DIR}"
                    --cc "${CMAKE_C_COMPILER}"
            DEPENDS
                irix_recompiled_core
                tools/build_desktop_bundle.py
                tools/recomp_elf_bundle.py
                tools/gen_prebound_bindings.py
                tools/translate_n32.py
                tools/translate_n32_core.py
                tools/translate_n32_regimm.py
                runtime/IrixGuest.h
            VERBATIM
        )

        add_custom_target(irix_recompiled_desktop ALL DEPENDS "${IRIX_RECOMP_DESKTOP_LIB}")
        add_library(irix_desktop_bundle STATIC IMPORTED GLOBAL)
        set_target_properties(irix_desktop_bundle PROPERTIES IMPORTED_LOCATION "${IRIX_RECOMP_DESKTOP_LIB}")
        add_dependencies(irix_desktop_bundle irix_recompiled_desktop)
    endif()
endif()
