# NXP Platform Detection - OS, features, C23 support

include(CheckIncludeFile)
include(CheckSymbolExists)

# Detect platform
if(WIN32)
    set(NXP_PLATFORM "windows" CACHE STRING "Target platform")
    set(NXP_HAS_IOCP TRUE)
    message(STATUS "NXP Platform: Windows (IOCP)")
elseif(UNIX AND NOT APPLE)
    set(NXP_PLATFORM "linux" CACHE STRING "Target platform")
    set(NXP_HAS_EPOLL TRUE)
    message(STATUS "NXP Platform: Linux (epoll)")

    # Check for io_uring
    if(NXP_ENABLE_IO_URING)
        check_include_file("liburing.h" HAVE_LIBURING_H)
        if(HAVE_LIBURING_H)
            set(NXP_HAS_IO_URING TRUE)
            message(STATUS "NXP: io_uring support enabled")
        else()
            message(WARNING "io_uring requested but liburing.h not found")
            set(NXP_ENABLE_IO_URING OFF)
        endif()
    endif()
elseif(APPLE)
    set(NXP_PLATFORM "macos" CACHE STRING "Target platform")
    message(STATUS "NXP Platform: macOS (kqueue) - experimental")
else()
    message(FATAL_ERROR "Unsupported platform")
endif()

# Check C23 feature availability
check_include_file("stdckdint.h" NXP_HAS_STDCKDINT)
check_include_file("stdbit.h" NXP_HAS_STDBIT)

if(NXP_HAS_STDCKDINT)
    message(STATUS "NXP: <stdckdint.h> available")
else()
    message(STATUS "NXP: <stdckdint.h> NOT available - will use fallback")
endif()

if(NXP_HAS_STDBIT)
    message(STATUS "NXP: <stdbit.h> available")
else()
    message(STATUS "NXP: <stdbit.h> NOT available - will use fallback")
endif()

# Check for Linux-specific UDP features
if(NXP_PLATFORM STREQUAL "linux")
    check_symbol_exists(UDP_GRO "linux/udp.h" NXP_HAS_UDP_GRO)
    check_symbol_exists(UDP_SEGMENT "linux/udp.h" NXP_HAS_UDP_GSO)
endif()

# Generate platform config header
configure_file(
    "${PROJECT_SOURCE_DIR}/cmake/nxp_platform_config.h.in"
    "${PROJECT_BINARY_DIR}/include/nxp/nxp_platform_config.h"
    @ONLY
)
