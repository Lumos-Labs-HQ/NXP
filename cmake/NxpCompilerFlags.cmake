# NXP Compiler Flags - C23 enforcement, warnings, sanitizers

# Common warning flags for GCC/Clang
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wshadow
        -Wdouble-promotion
        -Wformat=2
        -Wnull-dereference
        -Wimplicit-fallthrough
        -Wstrict-prototypes
        -Wno-unused-parameter
        -fvisibility=hidden
        -fno-common
        -fstack-protector-strong
    )

    # Release flags
    set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG -flto")
    set(CMAKE_C_FLAGS_RELWITHDEBINFO "-O2 -g -DNDEBUG -fno-omit-frame-pointer")
    set(CMAKE_C_FLAGS_DEBUG "-O0 -g3 -DNXP_DEBUG=1 -fno-omit-frame-pointer")

    # Fortify source in release
    add_compile_definitions($<$<CONFIG:Release>:_FORTIFY_SOURCE=2>)
    add_compile_definitions($<$<CONFIG:RelWithDebInfo>:_FORTIFY_SOURCE=2>)
endif()

# MSVC fallback (limited C23, but keep it working)
if(MSVC)
    add_compile_options(/W4 /WX)
    set(CMAKE_C_FLAGS_RELEASE "/O2 /DNDEBUG /GL")
    set(CMAKE_C_FLAGS_DEBUG "/Od /Zi /DNXP_DEBUG=1")
endif()

# Sanitizers
if(NXP_ENABLE_ASAN)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address)
    endif()
endif()

if(NXP_ENABLE_UBSAN)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        add_compile_options(-fsanitize=undefined)
        add_link_options(-fsanitize=undefined)
    endif()
endif()

if(NXP_ENABLE_TSAN)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        add_compile_options(-fsanitize=thread)
        add_link_options(-fsanitize=thread)
    endif()
endif()

# Windows-specific defines
if(WIN32)
    add_compile_definitions(
        _WIN32_WINNT=0x0A00
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
endif()
