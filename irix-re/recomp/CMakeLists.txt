add_library(irix_recomp_runtime SHARED
    runtime/IrixGuest.c
    runtime/IrixCompat.c
    runtime/IrixElf.c
)

target_include_directories(irix_recomp_runtime PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/runtime
)

set_target_properties(irix_recomp_runtime PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME irixre
    VERSION 1.0.0
    SOVERSION 1
)

add_library(irix_recomp_libc_bridge STATIC
    runtime/IrixLibcBridge.c
    runtime/IrixPosixBridgeUnit.c
    runtime/IrixCLibExtraBridgeUnit.c
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
find_path(XPM_INCLUDE_DIR X11/xpm.h)
find_library(XPM_LIBRARY NAMES Xpm)
if(X11_FOUND)
    add_library(irix_recomp_x11_bridge STATIC
        runtime/IrixX11BridgeUnit.c
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
    if(XPM_INCLUDE_DIR AND XPM_LIBRARY)
        message(STATUS "Using host Xpm: ${XPM_LIBRARY}")
        target_include_directories(irix_recomp_x11_bridge PRIVATE ${XPM_INCLUDE_DIR})
        target_compile_definitions(irix_recomp_x11_bridge PRIVATE IRIX_HAVE_XPM=1)
        target_link_libraries(irix_recomp_x11_bridge PUBLIC ${XPM_LIBRARY})
    else()
        message(WARNING "Host Xpm development files not found; Xpm bridges will be disabled")
    endif()
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
    set(IRIX_RECOMP_CORE_DSOS
        "${IRIX_RECOMP_CORE_DIR}/libXt.so"
        "${IRIX_RECOMP_CORE_DIR}/libXm.so"
        "${IRIX_RECOMP_CORE_DIR}/libSgm.so"
        "${IRIX_RECOMP_CORE_DIR}/libvk.so"
        "${IRIX_RECOMP_CORE_DIR}/libMrm.so"
        "${IRIX_RECOMP_CORE_DIR}/libUil.so"
    )

    add_custom_command(
        OUTPUT "${IRIX_RECOMP_CORE_LIB}" ${IRIX_RECOMP_CORE_DSOS}
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${IRIX_RECOMP_CORE_DIR}"
        COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/build_core_stack.py"
                "${IRIX_DEV_TREE}"
                -o "${IRIX_RECOMP_CORE_DIR}"
                --cc "${CMAKE_C_COMPILER}"
                --runtime-lib "$<TARGET_FILE:irix_recomp_runtime>"
        DEPENDS
            irix_recomp_runtime
            tools/build_core_stack.py
            tools/recomp_linked_stack.py
            tools/link_stack_n32.py
            tools/translate_n32.py
            tools/translate_n32_core.py
            tools/extract_ar.py
            runtime/IrixGuest.h
        VERBATIM
    )

    add_custom_target(irix_recompiled_core ALL
        DEPENDS "${IRIX_RECOMP_CORE_LIB}" ${IRIX_RECOMP_CORE_DSOS})
    add_library(irix_recompiled_stack STATIC IMPORTED GLOBAL)
    set_target_properties(irix_recompiled_stack PROPERTIES IMPORTED_LOCATION "${IRIX_RECOMP_CORE_LIB}")
    add_dependencies(irix_recompiled_stack irix_recompiled_core)

    # Native ELF containers for the statically translated IRIX libraries.
    # They preserve the original library boundaries while the existing
    # monolithic archive remains available to bootstrap the current runners.
    foreach(_irix_core_name Xt Xm Sgm vk Mrm Uil)
        add_library(irix_recompiled_${_irix_core_name}_so SHARED IMPORTED GLOBAL)
        set_target_properties(irix_recompiled_${_irix_core_name}_so PROPERTIES
            IMPORTED_LOCATION "${IRIX_RECOMP_CORE_DIR}/lib${_irix_core_name}.so")
        add_dependencies(irix_recompiled_${_irix_core_name}_so irix_recompiled_core)
    endforeach()

    if(IRIX_RUNTIME_TREE AND IRIX_INTERNAL_TREE)
        set(IRIX_RECOMP_CORE_LINK "${IRIX_RECOMP_CORE_DIR}/generated/image/link.json")
        set(IRIX_ECOSYSTEM_MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/ecosystem.json")
        set(IRIX_ECOSYSTEM_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated-ecosystem")
        set(IRIX_APP_BUILDER "${CMAKE_CURRENT_SOURCE_DIR}/tools/build_irix_app_bundle.py")

        execute_process(
            COMMAND "${Python3_EXECUTABLE}" "${IRIX_APP_BUILDER}"
                    --manifest "${IRIX_ECOSYSTEM_MANIFEST}" --list-apps
            RESULT_VARIABLE IRIX_ECOSYSTEM_LIST_RESULT
            OUTPUT_VARIABLE IRIX_ECOSYSTEM_APP_LINES
            ERROR_VARIABLE IRIX_ECOSYSTEM_LIST_ERROR
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        if(NOT IRIX_ECOSYSTEM_LIST_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to read IRIX ecosystem manifest: ${IRIX_ECOSYSTEM_LIST_ERROR}")
        endif()
        string(REPLACE "\n" ";" IRIX_ECOSYSTEM_APPS "${IRIX_ECOSYSTEM_APP_LINES}")

        if(X11_FOUND)
            set(IRIX_FAST_QUARK_INC "${IRIX_ECOSYSTEM_DIR}/irix_fast_quarks.inc")
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
                    COMMAND "${CMAKE_COMMAND}" -E make_directory "${IRIX_ECOSYSTEM_DIR}"
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

            add_custom_target(irix_ecosystem)

            foreach(_app_line IN LISTS IRIX_ECOSYSTEM_APPS)
                if(_app_line STREQUAL "")
                    continue()
                endif()
                string(REPLACE "|" ";" _app_fields "${_app_line}")
                list(LENGTH _app_fields _app_field_count)
                if(NOT _app_field_count EQUAL 4)
                    message(FATAL_ERROR "Bad ecosystem app record: ${_app_line}")
                endif()
                list(GET _app_fields 0 _app_name)
                list(GET _app_fields 1 _app_prefix)
                list(GET _app_fields 2 _app_kind)
                list(GET _app_fields 3 _app_path)

                if(NOT _app_kind STREQUAL "runtime")
                    message(FATAL_ERROR "Runner currently requires runtime app: ${_app_name}")
                endif()

                set(_app_dir "${CMAKE_CURRENT_BINARY_DIR}/generated-${_app_name}")
                set(_app_lib "${_app_dir}/libirix_${_app_name}_bundle.a")
                set(_app_build_target "irix_recompiled_${_app_name}")
                set(_app_bundle_target "irix_${_app_name}_bundle")
                set(_app_runner_target "irix_${_app_name}_runner")
                get_filename_component(_app_elf_name "${_app_path}" NAME)

                add_custom_command(
                    OUTPUT "${_app_lib}"
                    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_app_dir}"
                    COMMAND "${Python3_EXECUTABLE}" "${IRIX_APP_BUILDER}"
                            --manifest "${IRIX_ECOSYSTEM_MANIFEST}"
                            --app "${_app_name}"
                            --runtime-tree "${IRIX_RUNTIME_TREE}"
                            --internal-tree "${IRIX_INTERNAL_TREE}"
                            --dev-tree "${IRIX_DEV_TREE}"
                            --core-link "${IRIX_RECOMP_CORE_LINK}"
                            -o "${_app_dir}"
                            --cc "${CMAKE_C_COMPILER}"
                            --runtime-lib "$<TARGET_FILE:irix_recomp_runtime>"
                    DEPENDS
                        irix_recompiled_core
                        irix_recomp_runtime
                        "${IRIX_ECOSYSTEM_MANIFEST}"
                        tools/build_irix_app_bundle.py
                        tools/audit_n32_bundle.py
                        tools/recomp_elf_bundle.py
                        tools/gen_prebound_bindings.py
                        tools/translate_n32.py
                        tools/translate_n32_core.py
                        tools/translate_n32_regimm.py
                        runtime/IrixGuest.h
                    VERBATIM
                )

                add_custom_target(${_app_build_target} DEPENDS "${_app_lib}")
                add_library(${_app_bundle_target} STATIC IMPORTED GLOBAL)
                set_target_properties(${_app_bundle_target} PROPERTIES IMPORTED_LOCATION "${_app_lib}")
                add_dependencies(${_app_bundle_target} ${_app_build_target})

                # The CMake target keeps its unique internal name, but the
                # produced artifact is the reconstructed IRIX program itself.
                # AppRunner.c is now bootstrap/startup glue for that ELF, not
                # the public identity of the executable.
                add_executable(${_app_runner_target} runtime/AppRunner.c)
                set_target_properties(${_app_runner_target} PROPERTIES
                    OUTPUT_NAME "${_app_elf_name}"
                    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin"
                    BUILD_RPATH "${CMAKE_CURRENT_BINARY_DIR};${_app_dir}/lib"
                )
                add_dependencies(${_app_runner_target} irix_recompiled_core ${_app_build_target})
                if(TARGET irix_fast_quarks)
                    add_dependencies(${_app_runner_target} irix_fast_quarks)
                    target_compile_definitions(${_app_runner_target} PRIVATE IRIX_HAVE_FAST_QUARKS=1)
                endif()
                target_include_directories(${_app_runner_target} PRIVATE
                    ${CMAKE_CURRENT_SOURCE_DIR}/runtime
                    ${IRIX_ECOSYSTEM_DIR}
                )
                target_compile_definitions(${_app_runner_target} PRIVATE
                    IRIX_RUNTIME_TREE="${IRIX_RUNTIME_TREE}"
                    IRIX_INTERNAL_TREE="${IRIX_INTERNAL_TREE}"
                    IRIX_DEV_TREE="${IRIX_DEV_TREE}"
                    IRIX_CORE_IMAGE_DIR="${IRIX_RECOMP_CORE_DIR}/generated/image"
                    IRIX_APP_NAME="${_app_name}"
                    IRIX_APP_PATH="${_app_path}"
                    IRIX_APP_BUILD_DIR="${_app_dir}"
                    IRIX_APP_REGISTER=register_${_app_prefix}
                )
                target_link_libraries(${_app_runner_target} PRIVATE
                    ${_app_bundle_target}
                    irix_recompiled_stack
                    irix_recomp_libc_bridge
                    irix_recomp_x11_bridge
                    irix_recomp_runtime
                    X11::X11
                    ${CMAKE_DL_LIBS}
                    m
                )
                add_dependencies(irix_ecosystem ${_app_runner_target})
            endforeach()
        endif()
    endif()
endif()
