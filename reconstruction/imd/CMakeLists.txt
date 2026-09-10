add_library(clogin_imd_compat STATIC
    ImdString.c++
    ImdIconPanel.c++
    DragManager.c++
    ViewPanelAdopt.c++
    InvertedTeeRenderer.c++
)
target_include_directories(clogin_imd_compat PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(clogin_imd_compat PUBLIC Xm Xt X11)
set_property(TARGET clogin_imd_compat PROPERTY CXX_STANDARD 11)
set_property(TARGET clogin_imd_compat PROPERTY CXX_STANDARD_REQUIRED ON)
