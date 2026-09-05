find_path(SGM_MOTIF_INCLUDE_DIR
    NAMES Xm/Xm.h
    HINTS
        /usr/local/include
        /usr/include
        /opt/MaXX/include
)

find_library(SGM_MOTIF_LIBRARY
    NAMES Xm libXm
    HINTS
        /usr/local/lib64
        /usr/local/lib
        /usr/lib64
        /usr/lib
        /opt/MaXX/lib64
        /opt/MaXX/lib
)

find_library(SGM_XT_LIBRARY
    NAMES Xt libXt
    HINTS
        /usr/local/lib64
        /usr/local/lib
        /usr/lib64
        /usr/lib
        /opt/MaXX/lib64
        /opt/MaXX/lib
)

find_library(SGM_X11_LIBRARY
    NAMES X11 libX11
    HINTS
        /usr/local/lib64
        /usr/local/lib
        /usr/lib64
        /usr/lib
        /opt/MaXX/lib64
        /opt/MaXX/lib
)

if(SGM_MOTIF_INCLUDE_DIR AND SGM_MOTIF_LIBRARY AND SGM_XT_LIBRARY AND SGM_X11_LIBRARY)
    add_library(Sgm SHARED
        SgmCompat.c
    )

    target_include_directories(Sgm
        PUBLIC
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
            "${SGM_MOTIF_INCLUDE_DIR}"
    )

    target_link_libraries(Sgm
        PUBLIC
            "${SGM_MOTIF_LIBRARY}"
            "${SGM_XT_LIBRARY}"
            "${SGM_X11_LIBRARY}"
    )

    set_target_properties(Sgm PROPERTIES
        OUTPUT_NAME Sgm
        SOVERSION 1
        VERSION 1.0
    )

    set_property(TARGET Sgm PROPERTY C_STANDARD 99)
    set_property(TARGET Sgm PROPERTY C_STANDARD_REQUIRED ON)

    message(STATUS "Building reconstructed IRIX libSgm.so.1 compatibility library")
else()
    message(STATUS "Motif/Xt/X11 not found; skipping reconstructed libSgm")
endif()
