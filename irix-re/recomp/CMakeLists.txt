find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(UNICORN QUIET IMPORTED_TARGET unicorn)
endif()
if(NOT TARGET PkgConfig::UNICORN)
    find_path(UNICORN_INCLUDE_DIR NAMES unicorn/unicorn.h)
    find_library(UNICORN_LIBRARY NAMES unicorn)
endif()

add_library(irix_recomp_runtime SHARED
    runtime/IrixGuest.c
    runtime/IrixPath.c
    runtime/IrixMipsInterp.c
    runtime/IrixMipsJit.c
    runtime/IrixCompat.c
    runtime/IrixKernelInfo.c
    runtime/IrixElf.c
)

target_include_directories(irix_recomp_runtime PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/runtime
)

target_link_libraries(irix_recomp_runtime PRIVATE m)

if(TARGET PkgConfig::UNICORN)
    message(STATUS "IRIX MIPS JIT: using Unicorn via pkg-config")
    target_compile_definitions(irix_recomp_runtime PRIVATE IRIX_HAVE_UNICORN=1 IRIX_DEFAULT_JIT=1)
    target_link_libraries(irix_recomp_runtime PRIVATE PkgConfig::UNICORN)
elseif(UNICORN_INCLUDE_DIR AND UNICORN_LIBRARY)
    message(STATUS "IRIX MIPS JIT: using Unicorn ${UNICORN_LIBRARY}")
    target_compile_definitions(irix_recomp_runtime PRIVATE IRIX_HAVE_UNICORN=1 IRIX_DEFAULT_JIT=1)
    target_include_directories(irix_recomp_runtime PRIVATE ${UNICORN_INCLUDE_DIR})
    target_link_libraries(irix_recomp_runtime PRIVATE ${UNICORN_LIBRARY})
else()
    message(WARNING "Unicorn not found; IRIX JIT backend disabled (install Unicorn development files)")
endif()

set_target_properties(irix_recomp_runtime PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME irixre
    VERSION 1.0.0
    SOVERSION 1
)

include(CTest)
if(BUILD_TESTING)
    add_executable(irix_guest_path_test
        tests/IrixGuestPathTest.c
        runtime/IrixPath.c
    )
    target_include_directories(irix_guest_path_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/runtime
    )
    add_test(NAME irix_guest_path_test COMMAND irix_guest_path_test)

    add_executable(irix_guest_identity_test tests/IrixGuestIdentityTest.c)
    target_link_libraries(irix_guest_identity_test PRIVATE
        -Wl,--whole-archive
        irix_recomp_libc_bridge
        irix_recomp_x11_bridge
        -Wl,--no-whole-archive
        irix_recomp_runtime
        X11::X11
        m
    )
    add_test(NAME irix_guest_identity_test COMMAND irix_guest_identity_test)

    add_executable(irix_guest_memory_io_test tests/IrixGuestMemoryIoTest.c)
    target_link_libraries(irix_guest_memory_io_test PRIVATE
        -Wl,--whole-archive
        irix_recomp_libc_bridge
        irix_recomp_x11_bridge
        -Wl,--no-whole-archive
        irix_recomp_runtime
        X11::X11
        m
    )
    add_test(NAME irix_guest_memory_io_test COMMAND irix_guest_memory_io_test)

    add_executable(irix_mips_call_boundary_test tests/IrixMipsCallBoundaryTest.c)
    target_link_libraries(irix_mips_call_boundary_test PRIVATE
        -Wl,--whole-archive
        irix_recomp_libc_bridge
        irix_recomp_x11_bridge
        -Wl,--no-whole-archive
        irix_recomp_runtime
        X11::X11
        m
    )
    add_test(NAME irix_mips_call_boundary_test COMMAND irix_mips_call_boundary_test)

    add_executable(irix_posix_cftime_test tests/IrixPosixCftimeTest.c)
    target_link_libraries(irix_posix_cftime_test PRIVATE
        -Wl,--whole-archive
        irix_recomp_libc_bridge
        irix_recomp_x11_bridge
        -Wl,--no-whole-archive
        irix_recomp_runtime
        m
    )
    add_test(NAME irix_posix_cftime_test COMMAND irix_posix_cftime_test)

    add_executable(irix_posix_cuserid_test tests/IrixPosixCuseridTest.c)
    target_link_libraries(irix_posix_cuserid_test PRIVATE
        -Wl,--whole-archive
        irix_recomp_libc_bridge
        irix_recomp_x11_bridge
        -Wl,--no-whole-archive
        irix_recomp_runtime
        X11::X11
        m
    )
    add_test(NAME irix_posix_cuserid_test COMMAND irix_posix_cuserid_test)

    add_executable(irix_posix_execle_test tests/IrixPosixExecleTest.c)
    target_link_libraries(irix_posix_execle_test PRIVATE
        -Wl,--whole-archive
        irix_recomp_libc_bridge
        irix_recomp_x11_bridge
        -Wl,--no-whole-archive
        irix_recomp_runtime
        X11::X11
        m
    )
    add_test(NAME irix_posix_execle_test COMMAND irix_posix_execle_test)
endif()

add_library(irix_recomp_libc_bridge STATIC
    runtime/IrixLibcBridge.c
    runtime/IrixPosixBridgeUnit.c
    runtime/IrixCLibExtraBridgeUnit.c
    runtime/IrixCLibMoreBridge.c
    runtime/IrixSignalBridge.c
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
    crypt
)
set_target_properties(irix_recomp_libc_bridge PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

find_package(X11)
find_path(XPM_INCLUDE_DIR X11/xpm.h)
find_library(XPM_LIBRARY NAMES Xpm)
# The hybrid Xt bridge must use the host distro libXt; keep this lookup
# deliberately rooted in /usr so a stale cached path cannot leak in.
unset(IRIX_HOST_XT_LIBRARY CACHE)
find_library(IRIX_HOST_XT_LIBRARY NAMES Xt
    PATHS
        /usr/lib
        /usr/lib64
        /usr/lib/x86_64-linux-gnu
    NO_DEFAULT_PATH
)
if(X11_FOUND)
    if(BUILD_TESTING)
        add_executable(irix_x11_color_test
            tests/IrixX11ColorTest.c runtime/IrixX11Color.c runtime/IrixPath.c)
        target_include_directories(irix_x11_color_test PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/runtime)
        target_link_libraries(irix_x11_color_test PRIVATE X11::X11)
        add_test(NAME irix_x11_color_test COMMAND irix_x11_color_test)
    endif()
    add_library(irix_recomp_x11_bridge STATIC
        runtime/IrixX11BridgeUnit.c
        runtime/IrixX11BridgeExtraUnit.c
        runtime/IrixX11KeyboardBridge.c
        runtime/IrixX11ConnectionBridge.c
        runtime/IrixX11ContextBridge.c
        runtime/IrixX11BridgeBulkUnit.c
        runtime/IrixRuntimeX11Bridge.c
        runtime/IrixX11Color.c
        runtime/IrixRuntimeXtBridge.c
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
    if(X11_Xext_LIB)
        message(STATUS "Using host Xext: ${X11_Xext_LIB}")
        target_compile_definitions(irix_recomp_x11_bridge PRIVATE IRIX_HAVE_XEXT=1)
        target_link_libraries(irix_recomp_x11_bridge PUBLIC ${X11_Xext_LIB})
    else()
        message(WARNING "Host Xext development/runtime library not found; X Shape bridges will be disabled")
    endif()
    if(IRIX_HOST_XT_LIBRARY)
        message(STATUS "Using host Xt: ${IRIX_HOST_XT_LIBRARY}")
        target_link_libraries(irix_recomp_x11_bridge PUBLIC ${IRIX_HOST_XT_LIBRARY})
    else()
        message(WARNING "Host Xt development/runtime library not found; Xt hybrid bridge will fail to link")
    endif()
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
        OUTPUT "${IRIX_RECOMP_CORE_LIB}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${IRIX_RECOMP_CORE_DIR}"
        COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/build_core_stack.py"
                "${IRIX_DEV_TREE}"
                -o "${IRIX_RECOMP_CORE_DIR}"
                --cc "${CMAKE_C_COMPILER}"
                --runtime-lib "$<TARGET_FILE:irix_recomp_runtime>"
                --skip-shared
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

    # Static recompilation is opt-in. The JIT/original-binary bring-up
    # path must never trigger the expensive generated-core build implicitly.
    add_custom_target(irix_recompiled_core
        DEPENDS "${IRIX_RECOMP_CORE_LIB}")
    add_library(irix_recompiled_stack STATIC IMPORTED GLOBAL)
    set_target_properties(irix_recompiled_stack PROPERTIES IMPORTED_LOCATION "${IRIX_RECOMP_CORE_LIB}")
    add_dependencies(irix_recompiled_stack irix_recompiled_core)

    # The translated core is built once and shared by every runner as
    # libirixcore.so instead of being copied into each executable.  Symbols
    # it needs from the host bridges resolve against the runner's exported
    # dynamic symbols, exactly as for the static link.
    set(IRIX_RECOMP_CORE_SHARED "${CMAKE_CURRENT_BINARY_DIR}/libirixcore.so")
    add_custom_command(
        OUTPUT "${IRIX_RECOMP_CORE_SHARED}"
        COMMAND "${CMAKE_C_COMPILER}" -shared
                -o "${IRIX_RECOMP_CORE_SHARED}"
                -Wl,-soname,libirixcore.so
                -Wl,--whole-archive "${IRIX_RECOMP_CORE_LIB}"
                -Wl,--no-whole-archive
                "-Wl,-rpath,${CMAKE_CURRENT_BINARY_DIR}"
        DEPENDS "${IRIX_RECOMP_CORE_LIB}"
        COMMENT "Linking shared IRIX translated core"
        VERBATIM
    )
    add_custom_target(irix_recompiled_core_shared_build
        DEPENDS "${IRIX_RECOMP_CORE_SHARED}")
    add_library(irix_recompiled_core_shared SHARED IMPORTED GLOBAL)
    set_target_properties(irix_recompiled_core_shared PROPERTIES
        IMPORTED_LOCATION "${IRIX_RECOMP_CORE_SHARED}"
        IMPORTED_SONAME libirixcore.so)
    add_dependencies(irix_recompiled_core_shared irix_recompiled_core_shared_build)

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
        set(IRIX_RECOMP_ROOT "${CMAKE_CURRENT_BINARY_DIR}/root")
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
                    # Prefer the ABI/development libX11: recovered runtime
                    # libX11 builds can omit the private SGI quark table
                    # consumed by extract_fast_quarks.py.
                    "${IRIX_DEV_TREE}/usr/lib/abi/libX11.so.3"
                    "${IRIX_DEV_TREE}/usr/lib/abi/libX11.so.1"
                    "${IRIX_DEV_TREE}/usr/lib/abi/libX11.so"
                    "${IRIX_RUNTIME_TREE}/usr/lib32/libX11.so.1"
                    "${IRIX_RUNTIME_TREE}/usr/lib32/libX11.so.3"
                    "${IRIX_RUNTIME_TREE}/usr/lib32/libX11.so")
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
            add_custom_target(irix_ecosystem_jit)

            # Explicit host-install step for the reconstructed IRIX desktop
            # resource/configuration payload. Normal builds remain confined to
            # the build tree; this target deliberately installs the staged
            # payload at its original IRIX absolute paths on the host.
            add_custom_target(irix_install_runtime_payloads
                COMMAND "${CMAKE_COMMAND}" -E copy_directory
                        "${IRIX_RECOMP_ROOT}/usr/lib/X11"
                        "/usr/lib/X11"
                COMMAND "${CMAKE_COMMAND}" -E copy_directory
                        "${IRIX_RECOMP_ROOT}/usr/lib32/X11"
                        "/usr/lib32/X11"
                COMMAND "${CMAKE_COMMAND}" -E copy_directory
                        "${IRIX_RECOMP_ROOT}/usr/share/X11"
                        "/usr/share/X11"
                DEPENDS irix_ecosystem
                COMMENT "Installing IRIX desktop runtime resources to host IRIX paths"
                VERBATIM
            )

            # Minimal Interactive Desktop session bring-up.
            #
            # This follows /var/X11/xdm/Xsession.dt ordering for the core
            # visible session:
            #   4Dwm -launch -> wait4wm -> toolchest -> fm -b
            #
            # The full Xsession.dt also performs first-login, soundscheme,
            # bgicons, sysadmin, and power-management setup. Those are
            # intentionally deferred until the core desktop stack is stable.
            configure_file(
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/irix-session.sh.in"
                "${CMAKE_CURRENT_BINARY_DIR}/quick/irix-session"
                @ONLY
            )
            file(CHMOD "${CMAKE_CURRENT_BINARY_DIR}/quick/irix-session"
                 PERMISSIONS
                    OWNER_READ OWNER_WRITE OWNER_EXECUTE
                    GROUP_READ GROUP_EXECUTE
                    WORLD_READ WORLD_EXECUTE)

            # The default 4Dwm Log Out action runs the original endsession
            # script. Stage the script and interpreter; gettxt, xconfirm, and
            # xprop are built as normal guest applications at their IRIX paths.
            add_custom_target(irix_session_logout_payloads
                COMMAND "${CMAKE_COMMAND}" -E make_directory
                        "${IRIX_RECOMP_ROOT}/sbin"
                        "${IRIX_RECOMP_ROOT}/usr/bin"
                        "${IRIX_RECOMP_ROOT}/usr/bin/X11"
                        "${IRIX_RECOMP_ROOT}/usr/lib/X11"
                        "${IRIX_RECOMP_ROOT}/usr/include/X11"
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                        "${IRIX_RUNTIME_TREE}/sbin/sh"
                        "${IRIX_RECOMP_ROOT}/sbin/sh"
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                        "${IRIX_RUNTIME_TREE}/usr/bin/X11/endsession"
                        "${IRIX_RECOMP_ROOT}/usr/bin/X11/endsession"
                COMMAND chmod +x
                        "${IRIX_RECOMP_ROOT}/sbin/sh"
                        "${IRIX_RECOMP_ROOT}/usr/bin/X11/endsession"
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                        "${IRIX_RUNTIME_TREE}/usr/lib/X11/system.4Dwmrc"
                        "${IRIX_RECOMP_ROOT}/usr/lib/X11/system.4Dwmrc"
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                        "${IRIX_RUNTIME_TREE}/usr/lib/X11/system.chestrc"
                        "${IRIX_RECOMP_ROOT}/usr/lib/X11/system.chestrc"
                DEPENDS
                    "${IRIX_RUNTIME_TREE}/sbin/sh"
                    "${IRIX_RUNTIME_TREE}/usr/bin/X11/endsession"
                    "${IRIX_RUNTIME_TREE}/usr/lib/X11/system.4Dwmrc"
                    "${IRIX_RUNTIME_TREE}/usr/lib/X11/system.chestrc"
                COMMENT "Staging guest logout script and helper images"
                VERBATIM
            )

            add_custom_target(irix_session_visual_payloads
                COMMAND "${CMAKE_COMMAND}" -E make_directory
                        "${IRIX_RECOMP_ROOT}/usr/include/X11/bitmaps"
                COMMAND "${CMAKE_COMMAND}" -E copy_directory
                        "${IRIX_RUNTIME_TREE}/usr/include/X11/bitmaps"
                        "${IRIX_RECOMP_ROOT}/usr/include/X11/bitmaps"
                COMMENT "Staging authentic IRIX X11 bitmap and pixmap assets"
                VERBATIM
            )

            add_custom_target(irix_session
                DEPENDS irix_ecosystem irix_session_logout_payloads
                        irix_session_visual_payloads
                COMMENT "Interactive Desktop core session is ready at recomp/quick/irix-session"
            )

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
                get_filename_component(_app_guest_dir "${_app_path}" DIRECTORY)
                set(_app_root_dir "${IRIX_RECOMP_ROOT}/${_app_guest_dir}")

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
                            --rootfs "${IRIX_RECOMP_ROOT}"
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
                add_executable(${_app_runner_target} EXCLUDE_FROM_ALL runtime/AppRunner.c)
                set_target_properties(${_app_runner_target} PROPERTIES
                    OUTPUT_NAME "${_app_elf_name}"
                    RUNTIME_OUTPUT_DIRECTORY "${_app_root_dir}"
                    BUILD_RPATH "${CMAKE_CURRENT_BINARY_DIR}"
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
                    IRIX_DIRECT_MAIN=1
                    IRIX_RUNTIME_TREE="${IRIX_RUNTIME_TREE}"
                    IRIX_INTERNAL_TREE="${IRIX_INTERNAL_TREE}"
                    IRIX_DEV_TREE="${IRIX_DEV_TREE}"
                    IRIX_CORE_IMAGE_DIR="${IRIX_RECOMP_CORE_DIR}/generated/image"
                    IRIX_APP_NAME="${_app_name}"
                    IRIX_APP_PATH="${_app_path}"
                    IRIX_APP_BUILD_DIR="${_app_dir}"
                    IRIX_RECOMP_ROOT="${IRIX_RECOMP_ROOT}"
                    IRIX_GUEST_EXEC_RUNNER="${CMAKE_CURRENT_BINARY_DIR}/irix-guest-exec"
                    IRIX_APP_REGISTER=register_${_app_prefix}
                )
                target_link_libraries(${_app_runner_target} PRIVATE
                    ${_app_bundle_target}
                    irix_recompiled_core_shared
                    irix_recomp_libc_bridge
                    irix_recomp_x11_bridge
                    irix_recomp_runtime
                    X11::X11
                    ${CMAKE_DL_LIBS}
                    m
                )
                # irix_recomp_runtime has a narrow reverse callback into the
                # statically linked libc bridge for native compatibility
                # helpers. Export runner symbols so the dynamic loader can
                # resolve those callbacks from the main executable.
                set_target_properties(${_app_runner_target} PROPERTIES ENABLE_EXPORTS ON)
                add_dependencies(irix_ecosystem ${_app_runner_target})

                # Desktop bring-up shortcut: use the normal AppRunner/runtime
                # but bypass PT_INTERP/rld and enter the translated executable
                # main directly. Keep one runner implementation; do not revive
                # the old disposable DesktopRunner harness.
                if(_app_name STREQUAL "desktop")
                    add_executable(irix_desktop_quick_runner EXCLUDE_FROM_ALL
                        runtime/AppRunner.c)
                    set_target_properties(irix_desktop_quick_runner PROPERTIES
                        OUTPUT_NAME "desktop-quick"
                        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/quick"
                        # Keep the bring-up runner pinned to the runtime built
                        # beside this tree.  Using an origin-relative DT_RPATH
                        # avoids accidentally loading an older installed
                        # libirixre.so from LD_LIBRARY_PATH while iterating on
                        # interpreter fixes.
                        BUILD_RPATH "$ORIGIN/.."
                    )
                    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
                        target_link_options(irix_desktop_quick_runner PRIVATE
                            "-Wl,--disable-new-dtags")
                    endif()
                    add_dependencies(irix_desktop_quick_runner
                        irix_recomp_runtime
                        irix_recompiled_core
                        ${_app_build_target}
                    )
                    if(TARGET irix_fast_quarks)
                        add_dependencies(irix_desktop_quick_runner irix_fast_quarks)
                        target_compile_definitions(irix_desktop_quick_runner PRIVATE
                            IRIX_HAVE_FAST_QUARKS=1)
                    endif()
                    target_include_directories(irix_desktop_quick_runner PRIVATE
                        ${CMAKE_CURRENT_SOURCE_DIR}/runtime
                        ${IRIX_ECOSYSTEM_DIR}
                    )
                    target_compile_definitions(irix_desktop_quick_runner PRIVATE
                        IRIX_DIRECT_MAIN=1
                        IRIX_RUNTIME_TREE="${IRIX_RUNTIME_TREE}"
                        IRIX_INTERNAL_TREE="${IRIX_INTERNAL_TREE}"
                        IRIX_DEV_TREE="${IRIX_DEV_TREE}"
                        IRIX_CORE_IMAGE_DIR="${IRIX_RECOMP_CORE_DIR}/generated/image"
                        IRIX_APP_NAME="${_app_name}"
                        IRIX_APP_PATH="${_app_path}"
                        IRIX_APP_BUILD_DIR="${_app_dir}"
                        IRIX_RECOMP_ROOT="${IRIX_RECOMP_ROOT}"
                        IRIX_GUEST_EXEC_RUNNER="${CMAKE_CURRENT_BINARY_DIR}/irix-guest-exec"
                        IRIX_APP_REGISTER=register_${_app_prefix}
                    )
                    target_link_libraries(irix_desktop_quick_runner PRIVATE
                        ${_app_bundle_target}
                        irix_recompiled_core_shared
                        irix_recomp_libc_bridge
                        irix_recomp_x11_bridge
                        irix_recomp_runtime
                        X11::X11
                        ${CMAKE_DL_LIBS}
                        m
                    )
                    set_target_properties(irix_desktop_quick_runner PROPERTIES ENABLE_EXPORTS ON)
                endif()

                # Emergency direct-main runners for the visible core session.
                # These deliberately bypass PT_INTERP/rld, like desktop-quick,
                # so the session can come up even while the original rld JIT
                # path is still being repaired.
                if(_app_name STREQUAL "4dwm" OR
                   _app_name STREQUAL "wait4wm" OR
                   _app_name STREQUAL "toolchest" OR
                   _app_name STREQUAL "fm" OR
                   _app_name STREQUAL "fmserv")
                    set(_quick_target "irix_${_app_name}_quick_runner")
                    add_executable(${_quick_target} EXCLUDE_FROM_ALL
                        runtime/AppRunner.c)
                    set_target_properties(${_quick_target} PROPERTIES
                        OUTPUT_NAME "${_app_elf_name}-quick"
                        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/quick"
                        BUILD_RPATH "$ORIGIN/.."
                    )
                    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
                        target_link_options(${_quick_target} PRIVATE
                            "-Wl,--disable-new-dtags")
                    endif()
                    add_dependencies(${_quick_target}
                        irix_recomp_runtime
                        irix_recompiled_core
                        ${_app_build_target}
                    )
                    if(TARGET irix_fast_quarks)
                        add_dependencies(${_quick_target} irix_fast_quarks)
                        target_compile_definitions(${_quick_target} PRIVATE
                            IRIX_HAVE_FAST_QUARKS=1)
                    endif()
                    target_include_directories(${_quick_target} PRIVATE
                        ${CMAKE_CURRENT_SOURCE_DIR}/runtime
                        ${IRIX_ECOSYSTEM_DIR}
                    )
                    target_compile_definitions(${_quick_target} PRIVATE
                        IRIX_DIRECT_MAIN=1
                        IRIX_RUNTIME_TREE="${IRIX_RUNTIME_TREE}"
                        IRIX_INTERNAL_TREE="${IRIX_INTERNAL_TREE}"
                        IRIX_DEV_TREE="${IRIX_DEV_TREE}"
                        IRIX_CORE_IMAGE_DIR="${IRIX_RECOMP_CORE_DIR}/generated/image"
                        IRIX_APP_NAME="${_app_name}"
                        IRIX_APP_PATH="${_app_path}"
                        IRIX_APP_BUILD_DIR="${_app_dir}"
                        IRIX_RECOMP_ROOT="${IRIX_RECOMP_ROOT}"
                        IRIX_GUEST_EXEC_RUNNER="${CMAKE_CURRENT_BINARY_DIR}/irix-guest-exec"
                        IRIX_APP_REGISTER=register_${_app_prefix}
                    )
                    target_link_libraries(${_quick_target} PRIVATE
                        ${_app_bundle_target}
                        irix_recompiled_core_shared
                        irix_recomp_libc_bridge
                        irix_recomp_x11_bridge
                        irix_recomp_runtime
                        X11::X11
                        ${CMAKE_DL_LIBS}
                        m
                    )
                    set_target_properties(${_quick_target} PROPERTIES ENABLE_EXPORTS ON)
                endif()

                # Fast bring-up path: run the original IRIX MIPS executable
                # through the Unicorn R4000 JIT without generating or linking
                # translated C for the app/core stack.
                set(_jit_runner_target "irix_${_app_name}_jit_runner")
                set(_jit_root_dir "${CMAKE_CURRENT_BINARY_DIR}/jit-root/${_app_guest_dir}")
                add_executable(${_jit_runner_target} runtime/AppRunner.c)
                set_target_properties(${_jit_runner_target} PROPERTIES
                    OUTPUT_NAME "${_app_elf_name}"
                    RUNTIME_OUTPUT_DIRECTORY "${_jit_root_dir}"
                    BUILD_RPATH "${CMAKE_CURRENT_BINARY_DIR}"
                )
                target_include_directories(${_jit_runner_target} PRIVATE
                    ${CMAKE_CURRENT_SOURCE_DIR}/runtime
                    ${IRIX_ECOSYSTEM_DIR}
                )
                target_compile_definitions(${_jit_runner_target} PRIVATE
                    IRIX_JIT_ONLY=1
                    IRIX_RUNTIME_TREE="${IRIX_RUNTIME_TREE}"
                    IRIX_INTERNAL_TREE="${IRIX_INTERNAL_TREE}"
                    IRIX_DEV_TREE="${IRIX_DEV_TREE}"
                    IRIX_CORE_IMAGE_DIR=""
                    IRIX_APP_NAME="${_app_name}"
                    IRIX_APP_PATH="${_app_path}"
                    IRIX_RECOMP_ROOT="${IRIX_RECOMP_ROOT}"
                    IRIX_GUEST_EXEC_RUNNER="${CMAKE_CURRENT_BINARY_DIR}/irix-guest-exec"
                )
                target_link_libraries(${_jit_runner_target} PRIVATE
                    irix_recomp_libc_bridge
                    irix_recomp_x11_bridge
                    irix_recomp_runtime
                    X11::X11
                    ${CMAKE_DL_LIBS}
                    m
                )
                set_target_properties(${_jit_runner_target} PROPERTIES ENABLE_EXPORTS ON)
                add_dependencies(irix_ecosystem_jit ${_jit_runner_target})
            endforeach()

            # Generic original-path process entry. Child execve calls route
            # MIPS ELFs and #! scripts here so guest paths, argv, environment,
            # PT_INTERP and rld are handled by the same runtime without
            # per-application quick-runner aliases.
            add_executable(irix_guest_exec_runner runtime/AppRunner.c)
            set_target_properties(irix_guest_exec_runner PROPERTIES
                OUTPUT_NAME "irix-guest-exec"
                RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
                BUILD_RPATH "$ORIGIN"
                ENABLE_EXPORTS ON
            )
            target_include_directories(irix_guest_exec_runner PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}/runtime
                ${IRIX_ECOSYSTEM_DIR}
            )
            target_compile_definitions(irix_guest_exec_runner PRIVATE
                IRIX_JIT_ONLY=1
                IRIX_RUNTIME_TREE="${IRIX_RUNTIME_TREE}"
                IRIX_INTERNAL_TREE="${IRIX_INTERNAL_TREE}"
                IRIX_DEV_TREE="${IRIX_DEV_TREE}"
                IRIX_CORE_IMAGE_DIR=""
                IRIX_APP_NAME="guest-exec"
                IRIX_APP_PATH="usr/bin/X11/4Dwm"
                IRIX_RECOMP_ROOT="${IRIX_RECOMP_ROOT}"
                IRIX_GUEST_EXEC_RUNNER="${CMAKE_CURRENT_BINARY_DIR}/irix-guest-exec"
            )
            target_link_libraries(irix_guest_exec_runner PRIVATE
                irix_recomp_libc_bridge
                irix_recomp_x11_bridge
                irix_recomp_runtime
                X11::X11
                ${CMAKE_DL_LIBS}
                m
            )
            add_dependencies(irix_session irix_guest_exec_runner)
            add_dependencies(irix_ecosystem_jit irix_guest_exec_runner)
        endif()
    endif()
endif()

# Session helpers (desktop-integration/xsession), staged into the runtime
# root at their original IRIX paths so Toolchest menu commands can find them:
#  - waitmap: Toolchest runs every checkexec command as
#    "/usr/lib/desktop/waitmap /bin/sh -c <cmd>"; the original traps under the
#    guest runner, so this pass-through simply runs its arguments.
#  - irix-endsession: native Log Out (Toolchest runs commands through the
#    guest shell, so it cannot be a host shell script).
if(DEFINED IRIX_RECOMP_ROOT)
    set(_IRIX_XSESSION_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/../../desktop-integration/xsession")
    if(EXISTS "${_IRIX_XSESSION_DIR}/waitmap-passthrough.c")
        add_executable(irix_waitmap_passthrough
            "${_IRIX_XSESSION_DIR}/waitmap-passthrough.c")
        set_target_properties(irix_waitmap_passthrough PROPERTIES
            OUTPUT_NAME waitmap
            RUNTIME_OUTPUT_DIRECTORY "${IRIX_RECOMP_ROOT}/usr/lib/desktop")
        add_executable(irix_endsession
            "${_IRIX_XSESSION_DIR}/irix-endsession.c")
        set_target_properties(irix_endsession PROPERTIES
            OUTPUT_NAME irix-endsession
            RUNTIME_OUTPUT_DIRECTORY "${IRIX_RECOMP_ROOT}/usr/bin/X11")
    endif()
endif()
