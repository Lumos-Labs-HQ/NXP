# NXP Integration Tests

# Include paths
set(INTEGRATION_INCLUDES
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src/platform
    ${PROJECT_SOURCE_DIR}/src/util
    ${PROJECT_SOURCE_DIR}/tests
    ${PROJECT_BINARY_DIR}/include
)

# Test: UDP Echo + Event Loop (Phase 3)
add_executable(test_udp_echo test_udp_echo.c)
target_include_directories(test_udp_echo PRIVATE ${INTEGRATION_INCLUDES})
target_link_libraries(test_udp_echo PRIVATE nxp_platform nxp_util nxp_memory)
add_test(NAME test_udp_echo COMMAND test_udp_echo)
