# NXP OpenSSL Finder - Ensure OpenSSL 3.x is found
# This is included by the root CMakeLists.txt via find_package(OpenSSL)
# This file provides additional validation

if(OPENSSL_FOUND)
    if(OPENSSL_VERSION VERSION_LESS "3.0.0")
        message(FATAL_ERROR "NXP requires OpenSSL >= 3.0.0, found ${OPENSSL_VERSION}")
    endif()
    message(STATUS "NXP: OpenSSL ${OPENSSL_VERSION} found at ${OPENSSL_INCLUDE_DIR}")
endif()
