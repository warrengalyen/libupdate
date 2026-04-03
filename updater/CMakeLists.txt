add_executable(updater
    main.c
    update_state.c
    "${CMAKE_SOURCE_DIR}/libupdate/src/json_mini.c"
)

target_include_directories(updater PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/libupdate/src
)

target_link_libraries(updater PRIVATE update)

set(LIBUPDATE_ROOT_BIN_DIR "${CMAKE_SOURCE_DIR}/bin")
add_custom_command(
    TARGET updater
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${LIBUPDATE_ROOT_BIN_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:updater>"
        "${LIBUPDATE_ROOT_BIN_DIR}/"
    COMMENT "Copy updater -> ${LIBUPDATE_ROOT_BIN_DIR}"
)
