include(FetchContent)

set(FMT_HEADER_ONLY OFF CACHE BOOL "")

FetchContent_Declare(
    fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt
    GIT_TAG        11.2.0
    GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(fmt)

list(APPEND COMMON_LIBS fmt::fmt)
