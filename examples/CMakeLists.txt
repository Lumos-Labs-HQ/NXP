# NXP Protocol Examples

add_executable(test_tcp test_tcp.c)
target_link_libraries(test_tcp PRIVATE nxp_core nxp_crypto nxp_congestion nxp_platform nxp_memory nxp_util)

add_executable(test_udp test_udp.c)
target_link_libraries(test_udp PRIVATE nxp_core nxp_crypto nxp_congestion nxp_platform nxp_memory nxp_util)

add_executable(test_error_tracking test_error_tracking.c)
target_link_libraries(test_error_tracking PRIVATE 
    nxp_api 
    nxp_core 
    nxp_crypto 
    nxp_congestion 
    nxp_util
    nxp_platform 
    nxp_memory
    OpenSSL::Crypto
)
if(NXP_ENABLE_LOGGING)
    target_link_libraries(test_error_tracking PRIVATE nxp_logging)
endif()

add_executable(test_logging test_logging.c)
target_link_libraries(test_logging PRIVATE 
    nxp_api nxp_core nxp_crypto nxp_congestion nxp_util nxp_platform nxp_memory OpenSSL::Crypto
)
if(NXP_ENABLE_LOGGING)
    target_link_libraries(test_logging PRIVATE nxp_logging)
endif()

add_executable(test_metrics test_metrics.c)
target_link_libraries(test_metrics PRIVATE 
    nxp_api nxp_core nxp_crypto nxp_congestion nxp_util nxp_platform nxp_memory OpenSSL::Crypto
)
if(NXP_ENABLE_LOGGING)
    target_link_libraries(test_metrics PRIVATE nxp_logging)
endif()
