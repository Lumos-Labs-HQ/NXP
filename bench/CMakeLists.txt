# NXP Benchmarks

add_executable(bench_protocols bench_protocols.c)
target_link_libraries(bench_protocols PRIVATE nxp_static)
