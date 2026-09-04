cmake_minimum_required(VERSION 3.16)
project(irix_desktop_reconstruction LANGUAGES C CXX)

set(CMAKE_C_STANDARD 90)
set(CMAKE_C_STANDARD_REQUIRED OFF)
set(CMAKE_CXX_STANDARD 98)
set(CMAKE_CXX_STANDARD_REQUIRED OFF)

add_subdirectory(libimdUtil)
add_subdirectory(libimdModel)
add_subdirectory(viewkit)
