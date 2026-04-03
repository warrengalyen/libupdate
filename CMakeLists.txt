cmake_minimum_required(VERSION 3.16)

project(libupdate_project LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(LIBUPDATE_BUILD_TESTS "Build libupdate tests (Unity)" ON)

add_subdirectory(libupdate)
add_subdirectory(updater)

if (LIBUPDATE_BUILD_TESTS)
    include(CTest)
    add_subdirectory(tests)
endif ()
