set(VIEWKLASS_ROOT "" CACHE PATH "Optional ViewKlass installation prefix")

set(_viewklass_include_hints)
set(_viewklass_library_hints)

if(VIEWKLASS_ROOT)
    list(APPEND _viewklass_include_hints
        "${VIEWKLASS_ROOT}/include"
    )
    list(APPEND _viewklass_library_hints
        "${VIEWKLASS_ROOT}/lib64"
        "${VIEWKLASS_ROOT}/lib"
    )
endif()

# Transitional compatibility only. New installations should prefer a normal
# system prefix, CMAKE_PREFIX_PATH, or VIEWKLASS_ROOT.
list(APPEND _viewklass_include_hints
    /usr/local/include
    /usr/include
    /opt/MaXX/include
)
list(APPEND _viewklass_library_hints
    /usr/local/lib64
    /usr/local/lib
    /usr/lib64
    /usr/lib
    /opt/MaXX/lib64
    /opt/MaXX/lib
)

find_path(VIEWKLASS_INCLUDE_DIR
    NAMES Vk/VkApp.h
    HINTS ${_viewklass_include_hints}
)

find_library(VIEWKLASS_LIBRARY
    NAMES Vk libVk
    HINTS ${_viewklass_library_hints}
)

# Keep libraries which belong to the ViewKlass/Motif runtime in one family.
# In particular, do not accidentally link libVk from MaXX with the host
# libXm/libXp/libXt when MaXX provides its own copies with the same SONAMEs.
# This also works naturally after ViewKlass moves to /usr/local: the directory
# containing libVk becomes the preferred co-located runtime directory.
if(VIEWKLASS_LIBRARY)
    get_filename_component(_viewklass_runtime_dir "${VIEWKLASS_LIBRARY}" DIRECTORY)
endif()

function(find_viewklass_runtime_library out_var)
    set(_names ${ARGN})

    if(_viewklass_runtime_dir)
        find_library(${out_var}
            NAMES ${_names}
            PATHS "${_viewklass_runtime_dir}"
            NO_DEFAULT_PATH
        )
    endif()

    if(NOT ${out_var})
        find_library(${out_var}
            NAMES ${_names}
            HINTS ${_viewklass_library_hints}
        )
    endif()

    set(${out_var} "${${out_var}}" PARENT_SCOPE)
endfunction()

find_viewklass_runtime_library(MOTIF_LIBRARY Xm libXm)
find_viewklass_runtime_library(SCHEME_LIBRARY Scheme libScheme)
find_viewklass_runtime_library(XP_LIBRARY Xp libXp)
find_viewklass_runtime_library(XPM_LIBRARY Xpm libXpm)
find_viewklass_runtime_library(XMU_LIBRARY Xmu libXmu)
find_viewklass_runtime_library(XEXT_LIBRARY Xext libXext)
find_viewklass_runtime_library(XRENDER_LIBRARY Xrender libXrender)
find_viewklass_runtime_library(XFT_LIBRARY Xft libXft)
find_viewklass_runtime_library(XT_LIBRARY Xt libXt)
find_viewklass_runtime_library(X11_LIBRARY X11 libX11)

if(VIEWKLASS_INCLUDE_DIR AND VIEWKLASS_LIBRARY AND MOTIF_LIBRARY AND
   SCHEME_LIBRARY AND XP_LIBRARY AND XPM_LIBRARY AND XMU_LIBRARY AND
   XEXT_LIBRARY AND XRENDER_LIBRARY AND XFT_LIBRARY AND XT_LIBRARY AND
   X11_LIBRARY)
    add_library(viewklass INTERFACE)
    target_include_directories(viewklass INTERFACE "${VIEWKLASS_INCLUDE_DIR}")
    target_link_libraries(viewklass INTERFACE
        "${VIEWKLASS_LIBRARY}"
        "${MOTIF_LIBRARY}"
        "${SCHEME_LIBRARY}"
        "${XP_LIBRARY}"
        "${XPM_LIBRARY}"
        "${XMU_LIBRARY}"
        "${XEXT_LIBRARY}"
        "${XRENDER_LIBRARY}"
        "${XFT_LIBRARY}"
        "${XT_LIBRARY}"
        "${X11_LIBRARY}"
    )

    add_executable(viewkit_probe ViewKitProbe.c++)
    target_link_libraries(viewkit_probe PRIVATE viewklass)
    set_property(TARGET viewkit_probe PROPERTY CXX_STANDARD 11)
    set_property(TARGET viewkit_probe PROPERTY CXX_STANDARD_REQUIRED ON)

    set(PANDORA_APP_DEFAULTS_DIR "${CMAKE_CURRENT_BINARY_DIR}/app-defaults")
    file(MAKE_DIRECTORY "${PANDORA_APP_DEFAULTS_DIR}")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/app-defaults/Clogin"
        "${PANDORA_APP_DEFAULTS_DIR}/Clogin"
        COPYONLY
    )

    if(TARGET imdIconPanel)
        add_executable(pandora_viewkit_probe
            ../clogin/PandoraViewKit.c++
            ../clogin/PandoraPresentation.c++
            ../clogin/PandoraViewKitProbe.c++
            ../clogin/Base/PwdNameList.c++
        )
        target_include_directories(pandora_viewkit_probe PRIVATE
            ../clogin
            ../libimdIconPanel
        )
        target_compile_definitions(pandora_viewkit_probe PRIVATE
            "PANDORA_APP_DEFAULTS_PATH=\"${PANDORA_APP_DEFAULTS_DIR}/%N\""
        )
        target_link_libraries(pandora_viewkit_probe PRIVATE
            viewklass
            imdIconPanel
        )
        set_property(TARGET pandora_viewkit_probe PROPERTY CXX_STANDARD 11)
        set_property(TARGET pandora_viewkit_probe PROPERTY CXX_STANDARD_REQUIRED ON)
    else()
        message(STATUS "libimdIconPanel unavailable; skipping Pandora probe")
    endif()

    message(STATUS "ViewKlass enabled: ${VIEWKLASS_LIBRARY}")
    message(STATUS "ViewKlass runtime family: ${_viewklass_runtime_dir}")
else()
    message(STATUS "ViewKlass dependency closure incomplete; skipping ViewKit probes")
endif()
