find_path(IMDICONPANEL_MOTIF_INCLUDE_DIR
    NAMES Xm/Xm.h
    HINTS
        /usr/local/include
        /usr/include
        /opt/MaXX/include
)

find_library(IMDICONPANEL_MOTIF_LIBRARY
    NAMES Xm libXm
    HINTS
        /usr/local/lib64
        /usr/local/lib
        /usr/lib64
        /usr/lib
        /opt/MaXX/lib64
        /opt/MaXX/lib
)

find_library(IMDICONPANEL_XT_LIBRARY
    NAMES Xt libXt
    HINTS
        /usr/local/lib64
        /usr/local/lib
        /usr/lib64
        /usr/lib
        /opt/MaXX/lib64
        /opt/MaXX/lib
)

find_library(IMDICONPANEL_SGM_LIBRARY
    NAMES Sgm libSgm
    HINTS
        /usr/local/lib64
        /usr/local/lib
        /usr/lib64
        /usr/lib
        /opt/MaXX/lib64
        /opt/MaXX/lib
)

if(IMDICONPANEL_MOTIF_INCLUDE_DIR AND
   IMDICONPANEL_MOTIF_LIBRARY AND
   IMDICONPANEL_XT_LIBRARY)
    add_library(imdIconPanel STATIC
        ImdIconPanel.c++
    )

    target_include_directories(imdIconPanel
        PUBLIC
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${IMDICONPANEL_MOTIF_INCLUDE_DIR}"
    )

    target_link_libraries(imdIconPanel
        PUBLIC
            "${IMDICONPANEL_MOTIF_LIBRARY}"
            "${IMDICONPANEL_XT_LIBRARY}"
    )

    if(IMDICONPANEL_SGM_LIBRARY)
        target_compile_definitions(imdIconPanel PRIVATE IMDICONPANEL_HAVE_SGM=1)
        target_link_libraries(imdIconPanel PUBLIC "${IMDICONPANEL_SGM_LIBRARY}")
        message(STATUS "libSgm found; reconstructed ImdViewPanel will use SgVisualDrawingArea")
    else()
        message(STATUS "libSgm not found; reconstructed ImdViewPanel will fall back to XmDrawingArea")
    endif()

    set_property(TARGET imdIconPanel PROPERTY CXX_STANDARD 11)
    set_property(TARGET imdIconPanel PROPERTY CXX_STANDARD_REQUIRED ON)
else()
    message(STATUS "Motif/Xt not found; skipping reconstructed libimdIconPanel")
endif()
