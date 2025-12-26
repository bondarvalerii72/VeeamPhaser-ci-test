include(FetchContent)

FetchContent_Declare(
    mio
    GIT_REPOSITORY https://github.com/vimpunk/mio
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(mio)

list(APPEND COMMON_INCLUDE_DIRS ${mio_SOURCE_DIR}/include)
