# Mock HTTP integration: LIBUPDATE_INTEGRATION_TESTS=1 (POSIX / Cygwin).
# Native Windows WinHTTP: also LIBUPDATE_INTEGRATION_WINHTTP=1.
# Full update_perform + updater: LIBUPDATE_E2E_PERFORM=1 (test_apply.c).
#
# Unity: prefer git submodule at tests/third_party/Unity, else FetchContent.
if (POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif ()

set(UNITY_SRC_DIR "")
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/Unity/src/unity.c")
    set(UNITY_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/Unity")
else ()
    include(FetchContent)
    FetchContent_Declare(
        libupdate_unity_fetch
        GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git
        GIT_TAG v2.6.0
    )
    FetchContent_GetProperties(libupdate_unity_fetch)
    if (NOT libupdate_unity_fetch_POPULATED)
        FetchContent_Populate(libupdate_unity_fetch)
    endif ()
    set(UNITY_SRC_DIR "${libupdate_unity_fetch_SOURCE_DIR}")
endif ()

add_library(libupdate_unity STATIC "${UNITY_SRC_DIR}/src/unity.c")
target_include_directories(libupdate_unity PUBLIC "${UNITY_SRC_DIR}/src")

set(LIBUPDATE_HTTP_SERVE_PY "${CMAKE_CURRENT_LIST_DIR}/mock_server/http_serve.py")
set(LIBUPDATE_FIXTURE_UPDATE_JSON "${CMAKE_CURRENT_LIST_DIR}/fixtures/update.json")
find_program(LIBUPDATE_PYTHON_EXE NAMES python3 python)
if (NOT LIBUPDATE_PYTHON_EXE)
    set(LIBUPDATE_PYTHON_EXE "")
endif ()

# Deterministic minimal.zip (STORE) for miniz + embedded SHA-256.
set(_libupdate_cfg_root "${CMAKE_BINARY_DIR}/tests/fixtures_cfg")
set(_libupdate_cfg_zip "${_libupdate_cfg_root}/minimal.zip")
if (NOT LIBUPDATE_PYTHON_EXE STREQUAL "")
    file(MAKE_DIRECTORY "${_libupdate_cfg_root}")
    execute_process(
        COMMAND "${LIBUPDATE_PYTHON_EXE}" "${CMAKE_CURRENT_LIST_DIR}/mock_server/mk_minimal_zip.py" "${_libupdate_cfg_zip}"
        RESULT_VARIABLE _libupdate_minzip_rc
    )
    if (NOT _libupdate_minzip_rc EQUAL 0)
        message(FATAL_ERROR "Could not create ${_libupdate_cfg_zip} (mk_minimal_zip.py failed)")
    endif ()
else ()
    message(FATAL_ERROR "Python 3 is required to generate test ZIP fixtures (install python or add Unity submodule path)")
endif ()
file(SHA256 "${_libupdate_cfg_zip}" LIBUPDATE_FIXTURE_MINIMAL_SHA256)

set(LIBUPDATE_FIXTURE_MINIMAL_ZIP "${CMAKE_BINARY_DIR}/tests/fixtures/minimal.zip")
add_custom_command(
    OUTPUT "${LIBUPDATE_FIXTURE_MINIMAL_ZIP}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/tests/fixtures"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_libupdate_cfg_zip}" "${LIBUPDATE_FIXTURE_MINIMAL_ZIP}"
    DEPENDS "${_libupdate_cfg_zip}"
    VERBATIM
    COMMENT "Stage minimal.zip test fixture"
)

set(LIBUPDATE_HAVE_SLIP_ZIP 0)
set(LIBUPDATE_FIXTURE_SLIP_ZIP "")
if (LIBUPDATE_PYTHON_EXE)
    set(LIBUPDATE_FIXTURE_SLIP_ZIP "${CMAKE_BINARY_DIR}/tests/fixtures/zip_slip.zip")
    add_custom_command(
        OUTPUT "${LIBUPDATE_FIXTURE_SLIP_ZIP}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/tests/fixtures"
        COMMAND "${LIBUPDATE_PYTHON_EXE}" "${CMAKE_CURRENT_SOURCE_DIR}/mock_server/mk_slip_zip.py" "${LIBUPDATE_FIXTURE_SLIP_ZIP}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/mock_server/mk_slip_zip.py"
        VERBATIM
        COMMENT "Generate zip_slip.zip (path traversal entry)"
    )
    set(LIBUPDATE_HAVE_SLIP_ZIP 1)
endif ()

add_custom_target(libupdate_test_fixtures DEPENDS "${LIBUPDATE_FIXTURE_MINIMAL_ZIP}")
if (LIBUPDATE_HAVE_SLIP_ZIP)
    add_custom_target(libupdate_slip_zip DEPENDS "${LIBUPDATE_FIXTURE_SLIP_ZIP}")
    add_dependencies(libupdate_test_fixtures libupdate_slip_zip)
endif ()

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/test_config.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/test_config.h"
    @ONLY
)

set(LIBUPDATE_TEST_SOURCES
    test_main.c
    test_helpers.c
    test_update_init.c
    test_verify.c
    test_extract.c
    test_download.c
    test_apply.c
    test_updater.c
    test_platform.c
    test_stress.c
)

add_executable(update_tests ${LIBUPDATE_TEST_SOURCES})
add_dependencies(update_tests libupdate_test_fixtures)

target_sources(update_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/libupdate/src/sha256.c"
    "${CMAKE_SOURCE_DIR}/libupdate/src/platform_fs.c"
    "${CMAKE_SOURCE_DIR}/libupdate/src/platform_process.c"
)
target_include_directories(update_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}
        ${CMAKE_SOURCE_DIR}/libupdate/include
        ${CMAKE_SOURCE_DIR}/libupdate/src
        ${CMAKE_SOURCE_DIR}/libupdate/platform
)

target_compile_definitions(update_tests PRIVATE UPDATE_BUILD_TEST_HOOKS)

target_link_libraries(update_tests PRIVATE update libupdate_unity)

if (WIN32 AND NOT CYGWIN)
else ()
    find_package(Threads REQUIRED)
    target_link_libraries(update_tests PRIVATE Threads::Threads)
endif ()

if (WIN32)
    target_compile_definitions(update_tests PRIVATE _CRT_SECURE_NO_WARNINGS)
endif ()

set_target_properties(update_tests PROPERTIES
    BUILD_RPATH "${CMAKE_BINARY_DIR}/libupdate"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

add_executable(libupdate_perform_child child_perform.c)
target_include_directories(libupdate_perform_child PRIVATE
    ${CMAKE_SOURCE_DIR}/libupdate/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_compile_definitions(libupdate_perform_child PRIVATE UPDATE_BUILD_TEST_HOOKS)
target_link_libraries(libupdate_perform_child PRIVATE update)
set_target_properties(libupdate_perform_child PROPERTIES
    BUILD_RPATH "${CMAKE_BINARY_DIR}/libupdate"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

set(LIBUPDATE_ROOT_BIN_DIR "${CMAKE_SOURCE_DIR}/bin")

macro(libupdate_copy_runtime_deps _tgt)
    add_custom_command(
        TARGET ${_tgt}
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${LIBUPDATE_ROOT_BIN_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:update>"
            "$<TARGET_FILE_DIR:${_tgt}>/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:updater>"
            "$<TARGET_FILE_DIR:${_tgt}>/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:${_tgt}>"
            "${LIBUPDATE_ROOT_BIN_DIR}/"
        COMMENT "Copy update, updater, ${_tgt} -> $<TARGET_FILE_DIR:${_tgt}> and ${LIBUPDATE_ROOT_BIN_DIR}"
    )
endmacro()
libupdate_copy_runtime_deps(update_tests)
libupdate_copy_runtime_deps(libupdate_perform_child)

add_test(NAME update_tests COMMAND update_tests)
set_tests_properties(update_tests PROPERTIES TIMEOUT 300)

add_custom_target(check COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -R update_tests)
