include(FetchContent)

FetchContent_Declare(
    blake3z
    GIT_REPOSITORY https://github.com/bondarvalerii72/blake3z
    GIT_TAG v1.0
    PATCH_COMMAND git lfs install && git lfs pull
)
FetchContent_MakeAvailable(blake3z)

# Disable pkg-config file installation for blake3 to avoid missing file error
set(BLAKE3_INSTALL_PKGCONFIG OFF CACHE BOOL "" FORCE)

list(APPEND COMMON_INCLUDE_DIRS ${blake3z_SOURCE_DIR})
