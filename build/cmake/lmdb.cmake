include(FetchContent)

FetchContent_Declare(
    lmdb
    GIT_REPOSITORY https://github.com/LMDB/lmdb.git
    GIT_TAG        LMDB_0.9.29
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(lmdb)

add_library(lmdb STATIC
    ${lmdb_SOURCE_DIR}/libraries/liblmdb/mdb.c
    ${lmdb_SOURCE_DIR}/libraries/liblmdb/midl.c
)
target_include_directories(lmdb PUBLIC ${lmdb_SOURCE_DIR}/libraries/liblmdb)

list(APPEND COMMON_INCLUDE_DIRS ${lmdb_SOURCE_DIR}/libraries/liblmdb)
list(APPEND COMMON_LIBS lmdb)
