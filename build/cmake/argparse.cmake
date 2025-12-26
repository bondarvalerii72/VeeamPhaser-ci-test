include(FetchContent)

FetchContent_Declare(
    argparse
    GIT_REPOSITORY https://github.com/p-ranav/argparse
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(argparse)

list(APPEND COMMON_LIBS argparse)
list(APPEND COMMON_INCLUDE_DIRS ${argparse_SOURCE_DIR}/include)
