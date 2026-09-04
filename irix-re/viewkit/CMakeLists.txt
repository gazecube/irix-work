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

find_library(MOTIF_LIBRARY
    NAMES Xm libXm
    HINTS ${_viewklass_library_hints}
)

# MaXX ViewKlass/libXm expose these as part of their actual link/runtime
# dependency closure. Keep them explicit so the probes also link on systems
# using --as-needed or without transitive development symlinks.
find_library(SCHEME_LIBRARY
    NAMES Scheme libScheme
    HINTS ${_viewklass_library_hints}
)
find_library(XP_LIBRARY
    NAMES Xp libXp
    HINTS ${_viewklass_library_hints}
)
find_library(XPM_LIBRARY NAMES Xpm libXpm)
find_library(XMU_LIBRARY NAMES Xmu libXmu)
find_library(XEXT_LIBRARY NAMES Xext libXext)
find_library(XRENDER_LIBRARY NAMES Xrender libXrender)
find_library(XFT_LIBRARY NAMES Xft libXft)
find_library(XT_LIBRARY NAMES Xt libXt)
find_library(X11_LIBRARY NAMES X11 libX11)

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

    add_executable(pandora_viewkit_probe
        ../clogin/PandoraViewKit.c++
        ../clogin/PandoraViewKitProbe.c++
    )
    target_include_directories(pandora_viewkit_probe PRIVATE ../clogin)
    target_link_libraries(pandora_viewkit_probe PRIVATE viewklass)
    set_property(TARGET pandora_viewkit_probe PROPERTY CXX_STANDARD 11)
    set_property(TARGET pandora_viewkit_probe PROPERTY CXX_STANDARD_REQUIRED ON)

    message(STATUS "ViewKlass enabled: ${VIEWKLASS_LIBRARY}")
else()
    message(STATUS "ViewKlass dependency closure incomplete; skipping ViewKit probes")
endif()
