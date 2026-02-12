# NXP Source - Internal static libraries

# Utility library (data structures, logging)
add_library(nxp_util STATIC)
add_subdirectory(util)

# Memory management (arena, pool, slab)
add_library(nxp_memory STATIC)
add_subdirectory(memory)

# Platform abstraction (sockets, threads, time)
add_library(nxp_platform STATIC)
add_subdirectory(platform)

# Link dependencies between internal libs
target_link_libraries(nxp_memory PRIVATE nxp_util)
target_link_libraries(nxp_platform PRIVATE nxp_util nxp_memory)

# Stub libraries for future phases (empty for now)
add_library(nxp_congestion STATIC congestion/cc_stub.c)
target_include_directories(nxp_congestion PUBLIC
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src/congestion
)

add_library(nxp_crypto STATIC
    crypto/aead.c
    crypto/hkdf.c
    crypto/handshake_crypto.c
    crypto/header_protection.c
)
target_include_directories(nxp_crypto PUBLIC
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src/crypto
    ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(nxp_crypto PRIVATE OpenSSL::Crypto)

add_library(nxp_core STATIC
    core/packet.c
    core/frame.c
    core/ack.c
    core/flow_control.c
    core/stream.c
    core/scheduler.c
    core/connection.c
    core/handshake.c
)
target_include_directories(nxp_core PUBLIC
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src/core
    ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(nxp_core PRIVATE nxp_util nxp_crypto)

# Public API (stub for now)
add_library(nxp_api STATIC api/nxp_api_stub.c)
target_include_directories(nxp_api PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

# Final shared library
add_library(nxp SHARED api/nxp_api_stub.c)
target_link_libraries(nxp
    PRIVATE
        nxp_api
        nxp_core
        nxp_congestion
        nxp_crypto
        nxp_platform
        nxp_memory
        nxp_util
)
target_include_directories(nxp
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# Static library variant
add_library(nxp_static STATIC api/nxp_api_stub.c)
target_link_libraries(nxp_static
    PUBLIC
        nxp_api
        nxp_core
        nxp_congestion
        nxp_crypto
        nxp_platform
        nxp_memory
        nxp_util
)
target_include_directories(nxp_static
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
