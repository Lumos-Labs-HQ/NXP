target_include_directories(nxp_platform
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/src/platform
    PRIVATE
        ${PROJECT_BINARY_DIR}/include
)

# Common event loop code (timer management, run loop) - shared across all backends
target_sources(nxp_platform PRIVATE event_loop.c)

if(WIN32)
    target_sources(nxp_platform PRIVATE
        windows/platform_win.c
        windows/win_time.c
        windows/win_thread.c
        windows/winsock_socket.c
        windows/iocp_event_loop.c
    )
    target_link_libraries(nxp_platform PRIVATE ws2_32 mswsock)

    if(NXP_ENABLE_RIO)
        target_sources(nxp_platform PRIVATE windows/rio_event_loop.c)
        target_compile_definitions(nxp_platform PRIVATE NXP_HAS_RIO=1)
    endif()

elseif(UNIX AND NOT APPLE)
    target_sources(nxp_platform PRIVATE
        linux/platform_linux.c
        linux/posix_time.c
        linux/posix_thread.c
        linux/posix_socket.c
        linux/epoll_event_loop.c
    )
    target_compile_definitions(nxp_platform PRIVATE
        _POSIX_C_SOURCE=200809L
        _DEFAULT_SOURCE
    )
    target_link_libraries(nxp_platform PRIVATE pthread)

    if(NXP_ENABLE_IO_URING)
        target_sources(nxp_platform PRIVATE linux/iouring_event_loop.c)
        target_link_libraries(nxp_platform PRIVATE ${URING_LIBRARIES})
        target_compile_definitions(nxp_platform PRIVATE NXP_HAS_IO_URING=1)
    endif()
endif()
