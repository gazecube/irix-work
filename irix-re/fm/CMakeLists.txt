# Build the runnable file manager bootstrap even when ViewKlass is unavailable.
# When the reconstructed FmApp/FmWindow layer comes online this target remains
# the installed application entry point.

if(TARGET viewklass AND TARGET imdIconPanel)
    add_executable(fm FmMain.c++ FmIconSurface.c++)
    target_link_libraries(fm PRIVATE viewklass imdIconPanel)
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
       FM_XT_LIBRARY AND FM_X11_LIBRARY AND TARGET imdIconPanel)
        add_executable(fm FmMain.c++ FmIconSurface.c++)
        target_include_directories(fm PRIVATE "${FM_MOTIF_INCLUDE_DIR}")
        target_link_libraries(fm PRIVATE
            imdIconPanel
            "${FM_MOTIF_LIBRARY}"
            "${FM_XT_LIBRARY}"
            "${FM_X11_LIBRARY}"
        )
    else()
        message(STATUS "Motif/Xt/X11 or reconstructed imdIconPanel unavailable; skipping fm")
    endif()
endif()

if(TARGET fm)
    # MaXX ships its own libSgm.so.1 with the same historical SONAME as our
    # reconstruction.  The reconstructed library now owns SgThumbWheel as a
    # native widget as well as the ImdViewPanel superclass symbols.  Keep it
    # ahead of MaXX for this executable.
    if(TARGET Sgm)
        set_property(TARGET fm PROPERTY
            BUILD_RPATH "$ORIGIN/../libSgm")
        set_property(TARGET fm PROPERTY BUILD_RPATH_USE_ORIGIN TRUE)
        if(UNIX AND NOT APPLE)
            target_link_options(fm PRIVATE "-Wl,--disable-new-dtags")
        endif()
    endif()

    set_property(TARGET fm PROPERTY CXX_STANDARD 11)
    set_property(TARGET fm PROPERTY CXX_STANDARD_REQUIRED ON)
    install(TARGETS fm RUNTIME DESTINATION bin)
endif()

# Runnable background/desktop host.  This keeps the binary-proven
# ImdBackWindow -> ImdViewPanel -> BackLayout path executable while the
# remaining desktop model constructor path is recovered.
if(TARGET imdIconPanel)
    add_executable(iid-desktop DesktopMain.c++ BackLayoutPortable.c++)
    target_link_libraries(iid-desktop PRIVATE imdIconPanel)

    if(FM_MOTIF_INCLUDE_DIR)
        target_include_directories(iid-desktop PRIVATE "${FM_MOTIF_INCLUDE_DIR}")
    endif()
    if(FM_MOTIF_LIBRARY)
        target_link_libraries(iid-desktop PRIVATE "${FM_MOTIF_LIBRARY}")
    endif()
    if(FM_XT_LIBRARY)
        target_link_libraries(iid-desktop PRIVATE "${FM_XT_LIBRARY}")
    endif()
    if(FM_X11_LIBRARY)
        target_link_libraries(iid-desktop PRIVATE "${FM_X11_LIBRARY}")
    endif()

    # RE: libimdIconPanel directly references the historical
    # sgVisualDrawingAreaWidgetClass data symbol.  MaXX also ships a
    # libSgm.so.1, but that compatibility library does not provide this
    # reconstructed symbol.  Keep our sibling libSgm first here exactly as fm
    # already does, otherwise the dynamic loader can bind libimdIconPanel to
    # MaXX's libSgm and fail before main().
    if(TARGET Sgm)
        target_link_libraries(iid-desktop PRIVATE Sgm)
        set_property(TARGET iid-desktop PROPERTY
            BUILD_RPATH "$ORIGIN/../libSgm")
        set_property(TARGET iid-desktop PROPERTY BUILD_RPATH_USE_ORIGIN TRUE)
        if(UNIX AND NOT APPLE)
            target_link_options(iid-desktop PRIVATE "-Wl,--disable-new-dtags")
        endif()
    endif()

    set_property(TARGET iid-desktop PROPERTY CXX_STANDARD 11)
    set_property(TARGET iid-desktop PROPERTY CXX_STANDARD_REQUIRED ON)
    install(TARGETS iid-desktop RUNTIME DESTINATION bin)
endif()
