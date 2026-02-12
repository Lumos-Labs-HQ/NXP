target_sources(nxp_memory PRIVATE
    arena.c
    pool.c
    packet_buffer.c
    slab.c
)

target_include_directories(nxp_memory
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/src/memory
    PRIVATE
        ${PROJECT_BINARY_DIR}/include
)
