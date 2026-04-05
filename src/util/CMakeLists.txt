target_sources(nxp_util PRIVATE
    log.c
    varint.c
    hash_map.c
    ring_buffer.c
    intrusive_list.c
    priority_queue.c
    random.c
    crc32c.c
    flight_recorder.c
    error_tracker.c
)

target_include_directories(nxp_util
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/src/util
    PRIVATE
        ${PROJECT_BINARY_DIR}/include
)

# Windows needs bcrypt for random
if(WIN32)
    target_link_libraries(nxp_util PRIVATE bcrypt)
endif()
