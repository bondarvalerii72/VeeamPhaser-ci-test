include(FetchContent)

set(LZ4_BUILD_CLI OFF CACHE BOOL "" FORCE)
set(LZ4_BUILD_LEGACY_LZ4C OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    lz4
    GIT_REPOSITORY https://github.com/lz4/lz4.git
    GIT_TAG        v1.10.0
    GIT_SHALLOW    TRUE
)
# Fetch content but do not automatically add it to the build
FetchContent_MakeAvailable(lz4)

set(lz4_PATCH_FNAME "scripts/patches/lz4_decompress_return_consumed_bytes.patch")

# Check if the patch is already applied
execute_process(
    COMMAND git apply --reverse --check --ignore-space-change --ignore-whitespace ${CMAKE_CURRENT_SOURCE_DIR}/${lz4_PATCH_FNAME}
    WORKING_DIRECTORY ${lz4_SOURCE_DIR}
    RESULT_VARIABLE PATCH_CHECK_RESULT
    ERROR_VARIABLE PATCH_CHECK_ERROR
    OUTPUT_VARIABLE PATCH_CHECK_OUTPUT
)

if(PATCH_CHECK_RESULT EQUAL 0)
    message(STATUS "Patch already applied, skipping.")
else()
    message(STATUS "Applying ${lz4_PATCH_FNAME} ..")
    execute_process(
        COMMAND git apply --ignore-space-change --ignore-whitespace ${CMAKE_CURRENT_SOURCE_DIR}/${lz4_PATCH_FNAME}
        WORKING_DIRECTORY ${lz4_SOURCE_DIR}
        RESULT_VARIABLE PATCH_RESULT
        ERROR_VARIABLE PATCH_ERROR
        OUTPUT_VARIABLE PATCH_OUTPUT
    )

    if(NOT PATCH_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to apply patch:\n${PATCH_ERROR}")
    endif()
endif()

add_subdir_ex(${lz4_SOURCE_DIR}/build/cmake ${lz4_BINARY_DIR})

list(APPEND COMMON_INCLUDE_DIRS ${lz4_SOURCE_DIR}/lib)
list(APPEND COMMON_LINK_DIRS ${lz4_BINARY_DIR})
list(APPEND COMMON_LIBS lz4)
