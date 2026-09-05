if(TARGET Sgm AND TARGET imdIconPanel)
    add_library(imdApp SHARED
        Finders/IconFinder.c++
    )

    target_include_directories(imdApp
        PUBLIC
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/Finders"
    )

    target_link_libraries(imdApp
        PUBLIC
            Sgm
            imdIconPanel
    )

    set_target_properties(imdApp PROPERTIES
        OUTPUT_NAME imdApp
        VERSION 1.0.0
        SOVERSION 1
    )
    set_property(TARGET imdApp PROPERTY CXX_STANDARD 11)
    set_property(TARGET imdApp PROPERTY CXX_STANDARD_REQUIRED ON)

    add_executable(imd_icon_finder_smoke ImdIconFinderSmoke.c++)
    target_link_libraries(imd_icon_finder_smoke PRIVATE imdApp)
    set_property(TARGET imd_icon_finder_smoke PROPERTY CXX_STANDARD 11)
    set_property(TARGET imd_icon_finder_smoke PROPERTY CXX_STANDARD_REQUIRED ON)

    # Force the reconstructed libSgm ahead of a co-installed compatibility
    # library with the same historical SONAME.
    set_property(TARGET imd_icon_finder_smoke PROPERTY
        BUILD_RPATH "$ORIGIN:$ORIGIN/../libSgm")
    set_property(TARGET imd_icon_finder_smoke PROPERTY
        BUILD_RPATH_USE_ORIGIN TRUE)
    if(UNIX AND NOT APPLE)
        target_link_options(imd_icon_finder_smoke PRIVATE
            "-Wl,--disable-new-dtags")
    endif()

    message(STATUS "Building binary-backed libimdApp ImdIconFinder consumer")
else()
    message(STATUS "Sgm/libimdIconPanel unavailable; skipping reconstructed libimdApp")
endif()
