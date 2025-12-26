include(FetchContent)

add_compile_definitions(SPDLOG_COMPILED_LIB)
set(SPDLOG_FMT_EXTERNAL OFF)

FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog
    GIT_TAG        v1.x
    GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(spdlog)

list(APPEND COMMON_LIBS spdlog::spdlog)
list(APPEND COMMON_INCLUDE_DIRS ${spdlog_SOURCE_DIR}/include)
