cmake_minimum_required(VERSION 3.25)

project(nxp
    VERSION 0.1.0
    LANGUAGES C CXX
    DESCRIPTION "NEXUS Protocol - Custom Transport over UDP"
)

# Require C23
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# All static libs need PIC so they can be linked into the shared lib
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Export compile_commands.json for tooling
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Options
option(NXP_BUILD_TESTS     "Build test suite"            ON)
option(NXP_BUILD_BENCH     "Build benchmarks"             ON)
option(NXP_BUILD_EXAMPLES  "Build examples"               ON)
option(NXP_BUILD_TOOLS     "Build tools"                  ON)
option(NXP_ENABLE_ASAN     "Enable AddressSanitizer"      OFF)
option(NXP_ENABLE_UBSAN    "Enable UBSanitizer"           OFF)
option(NXP_ENABLE_TSAN     "Enable ThreadSanitizer"       OFF)
option(NXP_ENABLE_FUZZING  "Enable fuzz targets"          OFF)
option(NXP_ENABLE_IO_URING "Enable io_uring backend"      OFF)
option(NXP_ENABLE_RIO      "Enable Winsock RIO backend"   OFF)
option(NXP_ENABLE_LOGGING  "Enable Quill logging"          OFF)

# Include helpers
include(cmake/NxpCompilerFlags.cmake)
include(cmake/NxpPlatformDetect.cmake)
include(cmake/NxpVersion.cmake)

# Find dependencies
find_package(OpenSSL 3.0 REQUIRED)

if(NXP_ENABLE_IO_URING)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(URING REQUIRED liburing>=2.3)
endif()

# Library targets
add_subdirectory(src)

# Tests
if(NXP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Benchmarks
if(NXP_BUILD_BENCH)
    add_subdirectory(bench)
endif()

# Examples
if(NXP_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Tools
if(NXP_BUILD_TOOLS)
    add_subdirectory(tools)
endif()
