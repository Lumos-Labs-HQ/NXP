# Simple fuzzing targets

add_executable(fuzz_packet fuzz_packet.c)
target_link_libraries(fuzz_packet PRIVATE nxp_core nxp_util)

add_executable(fuzz_frame fuzz_frame.c)
target_link_libraries(fuzz_frame PRIVATE nxp_core nxp_util)

add_test(NAME fuzz_packet COMMAND fuzz_packet)
add_test(NAME fuzz_frame COMMAND fuzz_frame)
