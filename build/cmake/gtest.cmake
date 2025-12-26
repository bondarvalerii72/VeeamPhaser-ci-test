include(FetchContent)

# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest
    GIT_TAG        v1.17.x
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(googletest)

list(APPEND TEST_INCLUDE_DIRS
    ${gtest_SOURCE_DIR}/include
    ${gmock_SOURCE_DIR}/include
)
