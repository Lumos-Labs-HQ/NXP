# NXP Protocol Tests

add_executable(test_tcp test_tcp.c)
target_link_libraries(test_tcp PRIVATE nxp_core nxp_crypto nxp_congestion nxp_platform nxp_memory nxp_util)
