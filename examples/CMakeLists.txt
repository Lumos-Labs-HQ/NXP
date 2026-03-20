# NXP Protocol Tests

add_executable(test_udp test_udp.c)
target_link_libraries(test_udp PRIVATE nxp_core nxp_crypto nxp_congestion nxp_platform nxp_memory nxp_util)

# Real production examples with full API
add_executable(real_server real_server.c)
target_link_libraries(real_server PRIVATE nxp_api nxp_core nxp_crypto nxp_platform nxp_util nxp_memory nxp_congestion OpenSSL::SSL OpenSSL::Crypto)

add_executable(real_client real_client.c)
target_link_libraries(real_client PRIVATE nxp_api nxp_core nxp_crypto nxp_platform nxp_util nxp_memory nxp_congestion OpenSSL::SSL OpenSSL::Crypto)
