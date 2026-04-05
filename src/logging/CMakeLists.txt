# NXP Logging with Quill

add_library(nxp_logging STATIC
    nxp_log.cpp
)

target_include_directories(nxp_logging PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Find or fetch Quill
find_package(quill QUIET)

if(NOT quill_FOUND)
    message(STATUS "Quill not found, fetching from GitHub...")
    include(FetchContent)
    FetchContent_Declare(
        quill
        GIT_REPOSITORY https://github.com/odygrd/quill.git
        GIT_TAG        v3.8.0
    )
    FetchContent_MakeAvailable(quill)
endif()

target_link_libraries(nxp_logging PUBLIC quill::quill)

# Enable C++17 for Quill
target_compile_features(nxp_logging PUBLIC cxx_std_17)