# Build the runnable file manager bootstrap even when ViewKlass is unavailable.
# When the reconstructed FmApp/FmWindow layer comes online this target remains
# the installed application entry point.

if(TARGET viewklass AND TARGET imdIconPanel AND TARGET imdApp)
    add_executable(fm FmMain.c++ FmIconSurface.c++)
    target_link_libraries(fm PRIVATE viewklass imdIconPanel imdApp)
else()
    find_path(FM_MOTIF_INCLUDE_DIR
        NAMES Xm/Xm.h
        HINTS
            /usr/local/include
            /usr/include
    )

    find_library(FM_MOTIF_LIBRARY
        NAMES Xm libXm
        HINTS
            /usr/local/lib64
            /usr/local/lib
            /usr/lib64
            /usr/lib
    )
    find_library(FM_XT_LIBRARY
        NAMES Xt libXt
        HINTS
            /usr/local/lib64
            /usr/local/lib
            /usr/lib64
            /usr/lib
    )
    find_library(FM_X11_LIBRARY
        NAMES X11 libX11
        HINTS
            /usr/local/lib64
            /usr/local/lib
            /usr/lib64
            /usr/lib
    )

    if(FM_MOTIF_INCLUDE_DIR AND FM_MOTIF_LIBRARY AND
       FM_XT_LIBRARY AND FM_X11_LIBRARY AND TARGET imdIconPanel AND TARGET imdApp)
        add_executable(fm FmMain.c++ FmIconSurface.c++)
        target_include_directories(fm PRIVATE "${FM_MOTIF_INCLUDE_DIR}")
        target_link_libraries(fm PRIVATE
            imdApp
            imdIconPanel
            "${FM_MOTIF_LIBRARY}"
            "${FM_XT_LIBRARY}"
            "${FM_X11_LIBRARY}"
        )
    else()
        message(STATUS "Motif/Xt/X11 or reconstructed imdApp/imdIconPanel unavailable; skipping fm")
    endif()
endif()

if(TARGET fm)
    # The reconstructed libSgm owns SgThumbWheel as a native widget as well
    # as the ImdViewPanel superclass symbols; resolve it from the sibling
    # build directory first.
    if(TARGET Sgm)
        set_property(TARGET fm PROPERTY
            BUILD_RPATH "$ORIGIN/../libSgm:$ORIGIN/../libimdApp")
        set_property(TARGET fm PROPERTY BUILD_RPATH_USE_ORIGIN TRUE)
        if(UNIX AND NOT APPLE)
            target_link_options(fm PRIVATE "-Wl,--disable-new-dtags")
        endif()
    endif()

    set_property(TARGET fm PROPERTY CXX_STANDARD 11)
    set_property(TARGET fm PROPERTY CXX_STANDARD_REQUIRED ON)

    # Stage the preserved SGI FileManager app-default database beside the
    # executable. The current FM bootstrap already uses the original widget
    # names; loading this database restores the original pixmap-labeled sidebar
    # controls, spacing, accelerators, panel geometry and ThumbWheel resources.
    # FileManager includes ImdFinders by an absolute IRIX path, so inline the
    # shipped ImdFinders database here rather than depending on IRIX cpp/Xrm
    # include behavior on the Linux host.
    set(FM_RUNTIME_APP_DEFAULTS_DIR "${CMAKE_CURRENT_BINARY_DIR}/app-defaults")
    set(FM_RUNTIME_BITMAP_DIR "${CMAKE_CURRENT_BINARY_DIR}/sgidesktop")
    file(MAKE_DIRECTORY "${FM_RUNTIME_APP_DEFAULTS_DIR}")
    file(MAKE_DIRECTORY "${FM_RUNTIME_BITMAP_DIR}")

    set(_fm_preserved_app_defaults "${CMAKE_SOURCE_DIR}/evidence/FileManager")
    set(_fm_staged_app_defaults "${FM_RUNTIME_APP_DEFAULTS_DIR}/FileManager")
    if(EXISTS "${_fm_preserved_app_defaults}")
        file(READ "${_fm_preserved_app_defaults}" _fm_app_defaults_text)
        set(_fm_imd_finders_text "")

        # The preserved resources use original IRIX absolute paths. Extract the
        # matching shipped resources from desktop_eoe, then retarget them to
        # build-local copies so Linux hosts do not require an SGI /usr tree.
        set(_fm_desktop_eoe_tar "${CMAKE_SOURCE_DIR}/evidence/desktop_eoe(1).tar")
        if(EXISTS "${_fm_desktop_eoe_tar}")
            find_program(FM_TAR_EXECUTABLE tar)
            if(FM_TAR_EXECUTABLE)
                set(_fm_extract_dir "${CMAKE_CURRENT_BINARY_DIR}/desktop-eoe-stage")
                file(REMOVE_RECURSE "${_fm_extract_dir}")
                file(MAKE_DIRECTORY "${_fm_extract_dir}")
                execute_process(
                    COMMAND "${FM_TAR_EXECUTABLE}" -xf "${_fm_desktop_eoe_tar}" -C "${_fm_extract_dir}"
                    RESULT_VARIABLE _fm_tar_result
                )
                if(_fm_tar_result EQUAL 0)
                    foreach(_fm_bitmap
                            pathfinder.bm
                            iconicDirView.bm
                            listDirView.bm
                            treeDirView.bm
                            columnDirView.bm
                            galleryDirView.bm
                            shelf.bm
                            iconFinderPrevious)
                        file(GLOB_RECURSE _fm_bitmap_candidates
                            "${_fm_extract_dir}/*/${_fm_bitmap}"
                            "${_fm_extract_dir}/${_fm_bitmap}")
                        list(LENGTH _fm_bitmap_candidates _fm_bitmap_count)
                        if(_fm_bitmap_count GREATER 0)
                            list(GET _fm_bitmap_candidates 0 _fm_bitmap_source)
                            configure_file("${_fm_bitmap_source}"
                                           "${FM_RUNTIME_BITMAP_DIR}/${_fm_bitmap}"
                                           COPYONLY)
                        endif()
                    endforeach()

                    file(GLOB_RECURSE _fm_imd_finders_candidates
                        "${_fm_extract_dir}/*/ImdFinders"
                        "${_fm_extract_dir}/ImdFinders")
                    list(LENGTH _fm_imd_finders_candidates _fm_imd_finders_count)
                    if(_fm_imd_finders_count GREATER 0)
                        list(GET _fm_imd_finders_candidates 0 _fm_imd_finders_source)
                        file(READ "${_fm_imd_finders_source}" _fm_imd_finders_text)
                    else()
                        message(WARNING "Could not locate ImdFinders in desktop_eoe; finder will use generic SgFinder resources")
                    endif()
                else()
                    message(WARNING "Could not extract desktop_eoe resource evidence; FM will use host SGI resource paths")
                endif()
            endif()
        endif()

        file(TO_CMAKE_PATH "${FM_RUNTIME_BITMAP_DIR}" _fm_bitmap_resource_dir)
        string(REPLACE
            "/usr/include/X11/bitmaps/sgidesktop"
            "${_fm_bitmap_resource_dir}"
            _fm_app_defaults_text
            "${_fm_app_defaults_text}")

        if(NOT _fm_imd_finders_text STREQUAL "")
            string(REPLACE
                "/usr/include/X11/bitmaps/sgidesktop"
                "${_fm_bitmap_resource_dir}"
                _fm_imd_finders_text
                "${_fm_imd_finders_text}")
            file(WRITE "${FM_RUNTIME_APP_DEFAULTS_DIR}/ImdFinders"
                       "${_fm_imd_finders_text}")
            string(REPLACE
                "#include \"/usr/lib/X11/app-defaults/ImdFinders\""
                "${_fm_imd_finders_text}"
                _fm_app_defaults_text
                "${_fm_app_defaults_text}")
        endif()

        file(WRITE "${_fm_staged_app_defaults}" "${_fm_app_defaults_text}")
    else()
        message(WARNING "Preserved FileManager app-defaults unavailable; FM will use fallback resources")
    endif()

    # Keep the actual ELF as fm.bin and make ./build/fm/fm the normal launcher
    # that supplies XAPPLRESDIR. This makes a local build behave like the IRIX
    # installation without touching the user's global X resource setup.
    set_property(TARGET fm PROPERTY OUTPUT_NAME "fm.bin")
    set(FM_RUNTIME_BINARY "${CMAKE_CURRENT_BINARY_DIR}/fm.bin")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/fm-launcher.in"
                   "${CMAKE_CURRENT_BINARY_DIR}/fm"
                   @ONLY)
    file(CHMOD "${CMAKE_CURRENT_BINARY_DIR}/fm"
         PERMISSIONS
            OWNER_READ OWNER_WRITE OWNER_EXECUTE
            GROUP_READ GROUP_EXECUTE
            WORLD_READ WORLD_EXECUTE)

    install(TARGETS fm RUNTIME DESTINATION libexec/irix-re)
    install(PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/fm"
            DESTINATION bin)
    if(EXISTS "${_fm_staged_app_defaults}")
        install(FILES "${_fm_staged_app_defaults}"
                DESTINATION share/irix-re/app-defaults)
    endif()
endif()

# Runnable background/desktop host. This keeps the binary-proven
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
    # sgVisualDrawingAreaWidgetClass data symbol, which only our sibling
    # libSgm provides; resolve it first exactly as fm does.
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

# Exact original DefaultIconList captured from desktop_eoe (the session feeds it to
# FLtoPopDB and bgicons, as Xsession.dt does).
install(FILES DefaultIconList.irix-6.5
        DESTINATION share/irix-re/fm)
