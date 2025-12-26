add_definitions(-DGTEST)

# COMMON_LIBS is needed for nlohmann_json to be found, without making all COMMON_LIBS PUBLIC in core
set(TEST_LINK_LIBS core blake3z_file test_common ${COMMON_LIBS})

if(WIN32)
    #list(APPEND TEST_LINK_LIBS ${STATIC_LIBS} -static)
    #list(APPEND MAIN_SOURCES ${STATIC_MAIN_SOURCES})
else()
    list(APPEND TEST_LINK_LIBS ${DYNAMIC_LIBS})
endif()

include(blake3z)
include(gtest) # should be last to inherit all the deps

function(test test_name)
    set(options)
    set(one_value_args)
    set(multi_value_args SOURCES LIBS)
    cmake_parse_arguments(TEST "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

    list(TRANSFORM TEST_SOURCES PREPEND ../src/)
    add_executable(${test_name} ${test_name}.cpp ${TEST_SOURCES})
    target_include_directories(${test_name} PRIVATE ${COMMON_INCLUDE_DIRS} ${TEST_INCLUDE_DIRS})
    target_link_directories(${test_name} PRIVATE ${COMMON_LINK_DIRS})
    target_link_libraries(${test_name} PRIVATE ${TEST_LINK_LIBS}) # add ${TEST_LIBS} if libs are passed like test(test_name LIBS somelib)
    if(WIN32)
        # gtest_discover_tests() won't work when cross-compiling
        add_test(NAME ${test_name} COMMAND ${test_name})
    else()
        gtest_discover_tests(${test_name})
        if(APPLE)
            add_custom_command(TARGET ${test_name}
                POST_BUILD
                COMMAND dsymutil $<TARGET_FILE:${test_name}>
            )
        endif()
    endif()
endfunction()

add_library(test_common STATIC test_utils.cpp)
target_include_directories(test_common PRIVATE ${COMMON_INCLUDE_DIRS})
target_link_libraries(test_common PRIVATE gtest_main gmock_main)

# auto-discover tests
include(GoogleTest)
file(GLOB TEST_SRCS "${CMAKE_SOURCE_DIR}/tests/*_test.cpp")
foreach(test_src ${TEST_SRCS})
    get_filename_component(test_name ${test_src} NAME_WE)
    test(${test_name})
endforeach()
