include(FetchContent)

FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json
  GIT_TAG        v3.12.0
)

FetchContent_MakeAvailable(nlohmann_json)

list(APPEND COMMON_LIBS nlohmann_json::nlohmann_json)
