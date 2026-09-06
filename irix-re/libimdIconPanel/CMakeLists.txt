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

if(IMDICONPANEL_MOTIF_INCLUDE_DIR AND
   IMDICONPANEL_MOTIF_LIBRARY AND
   IMDICONPANEL_XT_LIBRARY)
    add_library(imdIconPanel SHARED
        ArgList.c++
        ImdIconPanel.c++
        IconLayout.c++
        ModelBasedIconPanel.c++
        ModelBasedIconView.c++
        ViewPanelAdopt.c++
    )

    target_include_directories(imdIconPanel
        PUBLIC
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/../libimdModel/Base"
            "${IMDICONPANEL_MOTIF_INCLUDE_DIR}"
    )

    target_link_libraries(imdIconPanel
        PUBLIC
            "${IMDICONPANEL_MOTIF_LIBRARY}"
            "${IMDICONPANEL_XT_LIBRARY}"
    )

    if(TARGET imdModel)
        target_link_libraries(imdIconPanel PUBLIC imdModel)
    endif()

    if(TARGET Sgm)
        target_compile_definitions(imdIconPanel PRIVATE IMDICONPANEL_HAVE_RECONSTRUCTED_SGM=1)
        target_link_libraries(imdIconPanel PUBLIC Sgm)
        message(STATUS "using reconstructed libSgm for ImdViewPanel superclass")
    elseif(CMAKE_DL_LIBS)
        target_compile_definitions(imdIconPanel PRIVATE IMDICONPANEL_HAVE_DLFCN=1)
        target_link_libraries(imdIconPanel PUBLIC "${CMAKE_DL_LIBS}")
        message(STATUS "dynamic loader available; ImdViewPanel will probe for SgVisualDrawingArea at runtime")
    else()
        message(STATUS "dynamic loader unavailable; reconstructed ImdViewPanel will use XmDrawingArea")
    endif()

    set_target_properties(imdIconPanel PROPERTIES
        OUTPUT_NAME imdIconPanel
        VERSION 1.0.0
        SOVERSION 1
    )
    set_property(TARGET imdIconPanel PROPERTY CXX_STANDARD 11)
    set_property(TARGET imdIconPanel PROPERTY CXX_STANDARD_REQUIRED ON)
else()
    message(STATUS "Motif/Xt not found; skipping reconstructed libimdIconPanel")
endif()
