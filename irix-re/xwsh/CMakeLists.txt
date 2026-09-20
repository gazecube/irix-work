find_package(X11 REQUIRED)

add_executable(xwsh xwsh.c)
target_link_libraries(xwsh PRIVATE X11::X11 util)
set_property(TARGET xwsh PROPERTY C_STANDARD 99)
set_property(TARGET xwsh PROPERTY C_STANDARD_REQUIRED ON)

install(TARGETS xwsh RUNTIME DESTINATION bin)
