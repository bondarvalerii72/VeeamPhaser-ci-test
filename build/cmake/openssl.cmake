if(WIN32)
    # fetch pre-built viaduck openssl
    set(SYSTEM_OPENSSL OFF CACHE BOOL "" FORCE)
    set(BUILD_OPENSSL OFF CACHE BOOL "" FORCE)
    set(CROSS ON CACHE BOOL "" FORCE)
    set(CROSS_TARGET mingw CACHE STRING "" FORCE)
    set(OPENSSL_BUILD_VERSION 3.3.3)
    set(OPENSSL_USE_STATIC_LIBS ON CACHE BOOL "" FORCE)

    include(FetchContent)
    FetchContent_Declare(
        openssl-cmake
        GIT_REPOSITORY https://github.com/viaduck/openssl-cmake
        GIT_TAG        v3
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(openssl-cmake)

    # OPENSSL_INCLUDE_DIR is set by openssl-cmake, but not at configure time
    list(APPEND COMMON_INCLUDE_DIRS ${openssl-cmake_BINARY_DIR}/openssl-prefix/src/openssl/usr/local/include)
    list(APPEND STATIC_LIBS crypto_static_lib ws2_32 crypt32)
else()
    # use system openssl
    list(APPEND COMMON_LIBS crypto)
endif()
