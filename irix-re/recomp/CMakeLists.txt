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
    runtime/IrixPosixBridge.c
    runtime/IrixCLibExtraBridge.c
    runtime/IrixCxxBridge.c
    runtime/IrixRuntimeLibcBridge.c
    runtime/IrixFsBridge.c
    runtime/IrixStdioBridge.c
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
        runtime/IrixX11BridgeExtraUnit.c
        runtime/IrixX11KeyboardBridge.c
        runtime/IrixX11ConnectionBridge.c
        runtime/IrixX11ContextBridge.c
        runtime/IrixX11BridgeBulkUnit.c
        runtime/IrixRuntimeX11Bridge.c
        runtime/IrixX11WindowBridgeUnit.c
        runtime/IrixX11ImageBridgeUnit.c
        runtime/IrixX11WmBridge.c
        runtime/IrixXrmCompat.c
        runtime/IrixXrmQuarkBridge.c
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
                tools/audit_n32_bundle.py
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
        add_dependencies(irix_recompiled_stack irix_recompiled_core)
        add_dependencies(irix_desktop_bundle irix_recompiled_desktop)

        if(X11_FOUND)
            set(IRIX_FAST_QUARK_INC "${IRIX_RECOMP_DESKTOP_DIR}/irix_fast_quarks.inc")
            set(IRIX_X11_GUEST_LIB "")
            foreach(_x11_candidate
                    "${IRIX_RUNTIME_TREE}/usr/lib32/libX11.so.1"
                    "${IRIX_RUNTIME_TREE}/usr/lib32/libX11.so.3"
                    "${IRIX_RUNTIME_TREE}/usr/lib32/libX11.so"
                    "${IRIX_DEV_TREE}/usr/lib/abi/libX11.so.3"
                    "${IRIX_DEV_TREE}/usr/lib/abi/libX11.so.1"
                    "${IRIX_DEV_TREE}/usr/lib/abi/libX11.so")
                if(EXISTS "${_x11_candidate}" AND NOT IRIX_X11_GUEST_LIB)
                    set(IRIX_X11_GUEST_LIB "${_x11_candidate}")
                endif()
            endforeach()

            if(IRIX_X11_GUEST_LIB)
                message(STATUS "Using IRIX libX11 for SGI fast quarks: ${IRIX_X11_GUEST_LIB}")
                add_custom_command(
                    OUTPUT "${IRIX_FAST_QUARK_INC}"
                    COMMAND "${Python3_EXECUTABLE}"
                            "${CMAKE_CURRENT_SOURCE_DIR}/tools/extract_fast_quarks.py"
                            "${IRIX_X11_GUEST_LIB}"
                            -o "${IRIX_FAST_QUARK_INC}"
                            --format c
                    DEPENDS tools/extract_fast_quarks.py "${IRIX_X11_GUEST_LIB}"
                    VERBATIM
                )
                add_custom_target(irix_fast_quarks DEPENDS "${IRIX_FAST_QUARK_INC}")
            else()
                message(WARNING "No IRIX libX11 found; SGI FAST_QUARKS support will be disabled")
            endif()

            add_executable(irix_desktop_runner runtime/DesktopRunner.c)
            add_dependencies(irix_desktop_runner irix_recompiled_core irix_recompiled_desktop)
            if(TARGET irix_fast_quarks)
                add_dependencies(irix_desktop_runner irix_fast_quarks)
                target_compile_definitions(irix_desktop_runner PRIVATE IRIX_HAVE_FAST_QUARKS=1)
            endif()
            target_include_directories(irix_desktop_runner PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}/runtime
                ${IRIX_RECOMP_DESKTOP_DIR}
            )
            target_compile_definitions(irix_desktop_runner PRIVATE
                IRIX_RUNTIME_TREE="${IRIX_RUNTIME_TREE}"
                IRIX_INTERNAL_TREE="${IRIX_INTERNAL_TREE}"
                IRIX_CORE_IMAGE_DIR="${IRIX_RECOMP_CORE_DIR}/generated/image"
            )
            target_link_libraries(irix_desktop_runner PRIVATE
                irix_desktop_bundle
                irix_recompiled_stack
                irix_recomp_libc_bridge
                irix_recomp_x11_bridge
                irix_recomp_runtime
                X11::X11
                m
            )
        endif()
    endif()
endif()
