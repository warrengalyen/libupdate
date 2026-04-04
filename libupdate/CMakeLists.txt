if (NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniz/miniz.h")
    message(FATAL_ERROR "miniz submodule missing: run \"git submodule update --init libupdate/third_party/miniz\"")
endif ()

add_library(update SHARED
    src/update.c
    src/update_path.c
    src/update_remote_check.c
    src/update_ops.c
    src/update_extract.c
    src/json_mini.c
    src/sha256.c
    src/platform_process.c
    src/platform_fs.c
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniz/miniz.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniz/miniz_tinfl.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniz/miniz_tdef.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniz/miniz_zip.c"
)

if (WIN32 AND NOT CYGWIN)
    target_sources(update PRIVATE src/http_transport_winhttp.c)
    target_link_libraries(update PRIVATE winhttp)
else ()
    target_sources(update PRIVATE src/http_transport_socket.c)
    target_compile_definitions(update PRIVATE _DEFAULT_SOURCE)

    find_package(OpenSSL QUIET)
    if (OpenSSL_FOUND)
        target_compile_definitions(update PRIVATE LIBUPDATE_HAVE_OPENSSL)
        target_link_libraries(update PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    else ()
        message(WARNING "OpenSSL not found: HTTPS will not be available on this platform")
    endif ()
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
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniz
)

target_compile_definitions(update PRIVATE MINIZ_NO_ARCHIVE_WRITING_APIS)

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

if (LIBUPDATE_BUILD_TESTS)
    target_compile_definitions(update PRIVATE UPDATE_BUILD_TEST_HOOKS)
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
