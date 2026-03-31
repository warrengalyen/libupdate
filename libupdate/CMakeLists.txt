add_library(update SHARED
    src/update.c
    src/platform_process_stub.c
    src/platform_fs_stub.c
)

target_include_directories(update
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/platform
)

if (WIN32)
    target_compile_definitions(update PRIVATE UPDATE_EXPORTS)
else ()
    target_compile_options(update PRIVATE -fvisibility=hidden)
endif ()

set_target_properties(update PROPERTIES
    OUTPUT_NAME update
    DEBUG_POSTFIX ""
)

# MinGW/MSYS GCC uses a "lib" prefix for DLLs by default; match MSVC naming (update.dll).
if (WIN32 AND (MINGW OR MSYS))
    set_target_properties(update PROPERTIES PREFIX "")
endif ()
