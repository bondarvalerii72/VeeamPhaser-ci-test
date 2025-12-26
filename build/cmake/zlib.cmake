include(FetchContent)

set(ZLIB_BUILD_EXAMPLES OFF)

FetchContent_Declare(
    zlib
    GIT_REPOSITORY https://github.com/madler/zlib.git
    GIT_TAG        v1.3.1
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(zlib)

list(APPEND COMMON_INCLUDE_DIRS ${zlib_SOURCE_DIR} ${zlib_BINARY_DIR})
list(APPEND COMMON_LIBS zlibstatic)
