# NXP Unit Tests

# Include paths
set(TEST_INCLUDES
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src/memory
    ${PROJECT_SOURCE_DIR}/src/util
    ${PROJECT_SOURCE_DIR}/src/platform
    ${PROJECT_SOURCE_DIR}/tests
    ${PROJECT_BINARY_DIR}/include
)

# Test: Arena allocator
add_executable(test_arena test_arena.c)
target_include_directories(test_arena PRIVATE ${TEST_INCLUDES})
target_link_libraries(test_arena PRIVATE nxp_memory nxp_util)
add_test(NAME test_arena COMMAND test_arena)

# Test: Object pool
add_executable(test_pool test_pool.c)
target_include_directories(test_pool PRIVATE ${TEST_INCLUDES})
target_link_libraries(test_pool PRIVATE nxp_memory nxp_util)
add_test(NAME test_pool COMMAND test_pool)

# Test: Varint encoding
add_executable(test_varint test_varint.c)
target_include_directories(test_varint PRIVATE ${TEST_INCLUDES})
target_link_libraries(test_varint PRIVATE nxp_util)
add_test(NAME test_varint COMMAND test_varint)

# Test: Hash map
add_executable(test_hash_map test_hash_map.c)
target_include_directories(test_hash_map PRIVATE ${TEST_INCLUDES})
target_link_libraries(test_hash_map PRIVATE nxp_util)
add_test(NAME test_hash_map COMMAND test_hash_map)

# Test: Ring buffer
add_executable(test_ring_buffer test_ring_buffer.c)
target_include_directories(test_ring_buffer PRIVATE ${TEST_INCLUDES})
target_link_libraries(test_ring_buffer PRIVATE nxp_util)
add_test(NAME test_ring_buffer COMMAND test_ring_buffer)

# Test: Priority queue
add_executable(test_priority_queue test_priority_queue.c)
target_include_directories(test_priority_queue PRIVATE ${TEST_INCLUDES})
target_link_libraries(test_priority_queue PRIVATE nxp_util)
add_test(NAME test_priority_queue COMMAND test_priority_queue)

# Test: Checked integer arithmetic
add_executable(test_checked_int test_checked_int.c)
target_include_directories(test_checked_int PRIVATE ${TEST_INCLUDES})
target_link_libraries(test_checked_int PRIVATE nxp_util)
add_test(NAME test_checked_int COMMAND test_checked_int)

# Test: Packet buffer pool
add_executable(test_packet_buffer test_packet_buffer.c)
target_include_directories(test_packet_buffer PRIVATE ${TEST_INCLUDES})
target_link_libraries(test_packet_buffer PRIVATE nxp_memory nxp_util)
add_test(NAME test_packet_buffer COMMAND test_packet_buffer)

# Test: Packet engine (Phase 2)
add_executable(test_packet test_packet.c)
target_include_directories(test_packet PRIVATE ${TEST_INCLUDES} ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(test_packet PRIVATE nxp_core nxp_util)
add_test(NAME test_packet COMMAND test_packet)

# Test: Frame engine (Phase 2)
add_executable(test_frame test_frame.c)
target_include_directories(test_frame PRIVATE ${TEST_INCLUDES} ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(test_frame PRIVATE nxp_core nxp_util)
add_test(NAME test_frame COMMAND test_frame)

# Test: CRC32C (Phase 2)
add_executable(test_crc32c test_crc32c.c)
target_include_directories(test_crc32c PRIVATE ${TEST_INCLUDES})
target_link_libraries(test_crc32c PRIVATE nxp_util)
add_test(NAME test_crc32c COMMAND test_crc32c)

# Test: Stream management (Phase 4)
add_executable(test_stream test_stream.c)
target_include_directories(test_stream PRIVATE ${TEST_INCLUDES} ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(test_stream PRIVATE nxp_core nxp_util)
add_test(NAME test_stream COMMAND test_stream)

# Test: ACK tracker + loss detection (Phase 4)
add_executable(test_ack test_ack.c)
target_include_directories(test_ack PRIVATE ${TEST_INCLUDES} ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(test_ack PRIVATE nxp_core nxp_util)
add_test(NAME test_ack COMMAND test_ack)

# Test: Connection engine (Phase 4)
add_executable(test_connection test_connection.c)
target_include_directories(test_connection PRIVATE ${TEST_INCLUDES} ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(test_connection PRIVATE nxp_core nxp_util)
add_test(NAME test_connection COMMAND test_connection)

# Test: Crypto primitives (Phase 5)
add_executable(test_crypto test_crypto.c)
target_include_directories(test_crypto PRIVATE ${TEST_INCLUDES} ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(test_crypto PRIVATE nxp_crypto nxp_core nxp_util OpenSSL::Crypto)
add_test(NAME test_crypto COMMAND test_crypto)

# Test: Handshake state machine (Phase 5)
add_executable(test_handshake test_handshake.c)
target_include_directories(test_handshake PRIVATE ${TEST_INCLUDES} ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(test_handshake PRIVATE nxp_core nxp_crypto nxp_util OpenSSL::Crypto)
add_test(NAME test_handshake COMMAND test_handshake)

# Test: Connection migration (Phase 6)
add_executable(test_migration test_migration.c)
target_include_directories(test_migration PRIVATE ${TEST_INCLUDES} ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(test_migration PRIVATE nxp_core nxp_crypto nxp_util OpenSSL::Crypto)
add_test(NAME test_migration COMMAND test_migration)

# Test: Session tickets + 0-RTT + retry tokens (Phase 6)
add_executable(test_session_ticket test_session_ticket.c)
target_include_directories(test_session_ticket PRIVATE ${TEST_INCLUDES} ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(test_session_ticket PRIVATE nxp_core nxp_crypto nxp_util OpenSSL::Crypto)
add_test(NAME test_session_ticket COMMAND test_session_ticket)

# Test: BBR congestion control (Phase 7)
add_executable(test_bbr test_bbr.c)
target_include_directories(test_bbr PRIVATE ${TEST_INCLUDES} ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(test_bbr PRIVATE nxp_congestion nxp_core nxp_crypto nxp_util OpenSSL::Crypto)
add_test(NAME test_bbr COMMAND test_bbr)
