include(CheckIncludeFile)

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
    set(CMAKE_REQUIRED_INCLUDES "${SGM_MOTIF_INCLUDE_DIR}")
    check_include_file("Xm/DrawingAP.h" SGM_HAVE_DRAWINGAP_H)
    check_include_file("Xm/BaseClassP.h" SGM_HAVE_BASECLASSP_H)
    check_include_file("Xm/ManagerP.h" SGM_HAVE_MANAGERP_H)
    check_include_file("Xm/PrimitiveP.h" SGM_HAVE_PRIMITIVEP_H)
    unset(CMAKE_REQUIRED_INCLUDES)

    add_library(Sgm SHARED
        SgmCompat.c
        DropPocket.c
        Finder.c
        ZoomBar.c
        ThumbWheel.c
        VisualDrawingA.c
        VisualDrawingAHost.c
        AbiObjects.c
        AbiStubs.c
    )

    target_include_directories(Sgm
        PUBLIC
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
            "${SGM_MOTIF_INCLUDE_DIR}"
    )

    if(SGM_HAVE_DRAWINGAP_H AND SGM_HAVE_BASECLASSP_H)
        target_compile_definitions(Sgm PRIVATE
            SGM_HAVE_DRAWINGAP_H=1
            SGM_HAVE_BASECLASSP_H=1
        )
        message(STATUS "private Motif DrawingArea/BaseClass headers found; building native SgVisualDrawingArea subclass")
    else()
        message(STATUS "private Motif DrawingArea/BaseClass headers incomplete; SgVisualDrawingArea will use compatibility fallback")
    endif()

    if(SGM_HAVE_MANAGERP_H)
        target_compile_definitions(Sgm PRIVATE SGM_HAVE_MANAGERP_H=1)
        message(STATUS "private Motif Manager header found; building native SgFinder/SgZoomBar subclasses")
    else()
        message(STATUS "private Motif Manager header missing; SgFinder/SgZoomBar will use compatibility fallbacks")
    endif()

    if(SGM_HAVE_PRIMITIVEP_H)
        message(STATUS "private Motif Primitive header found; building native SgDropPocket/SgThumbWheel subclasses")
    else()
        message(FATAL_ERROR "Xm/PrimitiveP.h is required for the native SgDropPocket/SgThumbWheel reconstruction")
    endif()

    target_link_libraries(Sgm
        PUBLIC
            "${SGM_MOTIF_LIBRARY}"
            "${SGM_XT_LIBRARY}"
            "${SGM_X11_LIBRARY}"
        PRIVATE
            ${CMAKE_DL_LIBS}
    )

    set_target_properties(Sgm PROPERTIES
        OUTPUT_NAME Sgm
        SOVERSION 1
        VERSION 1.0
    )

    set_property(TARGET Sgm PROPERTY C_STANDARD 99)
    set_property(TARGET Sgm PROPERTY C_STANDARD_REQUIRED ON)

    add_executable(sgm_visual_drawing_area_smoke
        SgVisualDrawingAreaSmoke.c
    )
    target_link_libraries(sgm_visual_drawing_area_smoke PRIVATE Sgm)
    set_property(TARGET sgm_visual_drawing_area_smoke PROPERTY C_STANDARD 99)
    set_property(TARGET sgm_visual_drawing_area_smoke PROPERTY C_STANDARD_REQUIRED ON)

    add_executable(sgm_finder_smoke
        SgFinderSmoke.c
    )
    target_link_libraries(sgm_finder_smoke PRIVATE Sgm)
    set_property(TARGET sgm_finder_smoke PROPERTY C_STANDARD 99)
    set_property(TARGET sgm_finder_smoke PROPERTY C_STANDARD_REQUIRED ON)

    message(STATUS "Building reconstructed IRIX libSgm.so.1 compatibility library")
else()
    message(STATUS "Motif/Xt/X11 not found; skipping reconstructed libSgm")
endif()
