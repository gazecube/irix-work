# Build the runnable file manager bootstrap even when ViewKlass is unavailable.
# When the reconstructed FmApp/FmWindow layer comes online this target remains
# the installed application entry point.

if(TARGET viewklass)
    add_executable(fm FmMain.c++)
    target_link_libraries(fm PRIVATE viewklass)
else()
    find_path(FM_MOTIF_INCLUDE_DIR
        NAMES Xm/Xm.h
        HINTS
            /usr/local/include
            /usr/include
            /opt/MaXX/include
    )

    find_library(FM_MOTIF_LIBRARY
        NAMES Xm libXm
        HINTS
            /usr/local/lib64
            /usr/local/lib
            /usr/lib64
            /usr/lib
            /opt/MaXX/lib64
            /opt/MaXX/lib
    )
    find_library(FM_XT_LIBRARY
        NAMES Xt libXt
        HINTS
            /usr/local/lib64
            /usr/local/lib
            /usr/lib64
            /usr/lib
            /opt/MaXX/lib64
            /opt/MaXX/lib
    )
    find_library(FM_X11_LIBRARY
        NAMES X11 libX11
        HINTS
            /usr/local/lib64
            /usr/local/lib
            /usr/lib64
            /usr/lib
            /opt/MaXX/lib64
            /opt/MaXX/lib
    )

    if(FM_MOTIF_INCLUDE_DIR AND FM_MOTIF_LIBRARY AND
       FM_XT_LIBRARY AND FM_X11_LIBRARY)
        add_executable(fm FmMain.c++)
        target_include_directories(fm PRIVATE "${FM_MOTIF_INCLUDE_DIR}")
        target_link_libraries(fm PRIVATE
            "${FM_MOTIF_LIBRARY}"
            "${FM_XT_LIBRARY}"
            "${FM_X11_LIBRARY}"
        )
    else()
        message(STATUS "Motif/Xt/X11 unavailable; skipping fm")
    endif()
endif()

if(TARGET fm)
    if(TARGET imdIconPanel)
        target_link_libraries(fm PRIVATE imdIconPanel)
        target_include_directories(fm PRIVATE ../libimdIconPanel)
    else()
        message(FATAL_ERROR "fm requires reconstructed libimdIconPanel")
    endif()

    set_property(TARGET fm PROPERTY CXX_STANDARD 11)
    set_property(TARGET fm PROPERTY CXX_STANDARD_REQUIRED ON)
    install(TARGETS fm RUNTIME DESTINATION bin)
endif()
