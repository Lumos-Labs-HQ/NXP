# NXP Integration Tests

# Include paths
set(INTEGRATION_INCLUDES
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/src/platform
    ${PROJECT_SOURCE_DIR}/src/util
    ${PROJECT_SOURCE_DIR}/src/core
    ${PROJECT_SOURCE_DIR}/src/api
    ${PROJECT_SOURCE_DIR}/src/crypto
    ${PROJECT_SOURCE_DIR}/src/congestion
    ${PROJECT_SOURCE_DIR}/src/memory
    ${PROJECT_SOURCE_DIR}/tests
    ${PROJECT_BINARY_DIR}/include
)

# Test: UDP Echo + Event Loop (Phase 3)
add_executable(test_udp_echo test_udp_echo.c)
target_include_directories(test_udp_echo PRIVATE ${INTEGRATION_INCLUDES})
target_link_libraries(test_udp_echo PRIVATE nxp_platform nxp_util nxp_memory)
add_test(NAME test_udp_echo COMMAND test_udp_echo)

# Test: End-to-End Handshake + Echo (Phase 5+)
add_executable(test_e2e_echo test_e2e_echo.c)
target_include_directories(test_e2e_echo PRIVATE ${INTEGRATION_INCLUDES})
target_link_libraries(test_e2e_echo PRIVATE nxp_api nxp_core nxp_crypto nxp_congestion nxp_platform nxp_memory nxp_util)
add_test(NAME test_e2e_echo COMMAND test_e2e_echo)

# Test: WebSocket protocol E2E (RFC 6455 over socketpair)
add_executable(test_ws_e2e test_ws_e2e.c)
target_include_directories(test_ws_e2e PRIVATE
    ${INTEGRATION_INCLUDES}
    ${PROJECT_SOURCE_DIR}/src/transport/websocket
)
target_link_libraries(test_ws_e2e PRIVATE nxp_transport nxp_util OpenSSL::Crypto)
if(NOT WIN32)
    target_compile_definitions(test_ws_e2e PRIVATE _DEFAULT_SOURCE)
endif()
add_test(NAME test_ws_e2e COMMAND test_ws_e2e)

# Test: TCP length-prefixed framing E2E
add_executable(test_tcp_framing test_tcp_framing.c)
target_include_directories(test_tcp_framing PRIVATE ${INTEGRATION_INCLUDES})
target_link_libraries(test_tcp_framing PRIVATE nxp_transport nxp_util)
if(NOT WIN32)
    target_compile_definitions(test_tcp_framing PRIVATE _POSIX_C_SOURCE=199309L _DEFAULT_SOURCE)
endif()
add_test(NAME test_tcp_framing COMMAND test_tcp_framing)
