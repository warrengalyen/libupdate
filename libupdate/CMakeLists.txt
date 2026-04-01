add_library(update SHARED
    src/update.c
    src/sha256.c
    src/platform_process.c
    src/platform_fs.c
)

if (WIN32 AND NOT CYGWIN)
    target_sources(update PRIVATE src/http_transport_winhttp.c)
    target_link_libraries(update PRIVATE winhttp)
else ()
    target_sources(update PRIVATE src/http_transport_socket.c)
    target_compile_definitions(update PRIVATE _DEFAULT_SOURCE)
endif ()

# update.c uses pthread except on native Windows (SRWLOCK). Cygwin may set WIN32.
if (WIN32 AND NOT CYGWIN)
else ()
    find_package(Threads REQUIRED)
    target_link_libraries(update PRIVATE Threads::Threads)
endif ()

target_include_directories(update
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/platform
        ${CMAKE_CURRENT_SOURCE_DIR}/src
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

set(LIBUPDATE_ROOT_BIN_DIR "${CMAKE_SOURCE_DIR}/bin")
add_custom_command(
    TARGET update
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${LIBUPDATE_ROOT_BIN_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:update>"
        "${LIBUPDATE_ROOT_BIN_DIR}/"
    COMMENT "Copy update -> ${LIBUPDATE_ROOT_BIN_DIR}"
)
