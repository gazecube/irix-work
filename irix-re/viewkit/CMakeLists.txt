find_path(VIEWKLASS_INCLUDE_DIR
    NAMES Vk/VkApp.h
    HINTS
        /opt/MaXX/include
        /usr/local/include
        /usr/include
)

find_library(VIEWKLASS_LIBRARY
    NAMES Vk libVk
    HINTS
        /opt/MaXX/lib64
        /opt/MaXX/lib
        /usr/local/lib64
        /usr/local/lib
        /usr/lib64
        /usr/lib
)

find_library(MOTIF_LIBRARY
    NAMES Xm libXm
    HINTS
        /opt/MaXX/lib64
        /opt/MaXX/lib
        /usr/local/lib64
        /usr/local/lib
        /usr/lib64
        /usr/lib
)

find_library(XT_LIBRARY NAMES Xt libXt)
find_library(X11_LIBRARY NAMES X11 libX11)

if(VIEWKLASS_INCLUDE_DIR AND VIEWKLASS_LIBRARY AND MOTIF_LIBRARY AND XT_LIBRARY AND X11_LIBRARY)
    add_library(viewklass INTERFACE)
    target_include_directories(viewklass INTERFACE "${VIEWKLASS_INCLUDE_DIR}")
    target_link_libraries(viewklass INTERFACE
        "${VIEWKLASS_LIBRARY}"
        "${MOTIF_LIBRARY}"
        "${XT_LIBRARY}"
        "${X11_LIBRARY}"
    )

    add_executable(viewkit_probe ViewKitProbe.c++)
    target_link_libraries(viewkit_probe PRIVATE viewklass)
    set_property(TARGET viewkit_probe PROPERTY CXX_STANDARD 11)
    set_property(TARGET viewkit_probe PROPERTY CXX_STANDARD_REQUIRED ON)

    message(STATUS "ViewKlass enabled: ${VIEWKLASS_LIBRARY}")
else()
    message(STATUS "ViewKlass not found; skipping viewkit_probe")
endif()
