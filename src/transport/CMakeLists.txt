# NXP Transport Library
# All transport backends are linked into a single static library for now.
# In the future, backends can be made optional via cmake options.

set(TRANSPORT_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../platform/transport.c
    udp/transport_udp.c
    websocket/ws_frame.c
    websocket/ws_handshake.c
    websocket/transport_ws.c
    tcp/transport_tcp.c
)

add_library(nxp_transport STATIC ${TRANSPORT_SOURCES})

target_link_libraries(nxp_transport PRIVATE
    nxp_util
    nxp_memory
    nxp_platform
    OpenSSL::Crypto
)

target_include_directories(nxp_transport PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_BINARY_DIR}/include
    ${PROJECT_SOURCE_DIR}/src/platform
    ${PROJECT_SOURCE_DIR}/src/transport/websocket
)
