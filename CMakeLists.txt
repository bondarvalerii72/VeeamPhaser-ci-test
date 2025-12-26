cmake_minimum_required(VERSION 3.14)

project(VeeamPhaser)

file(GLOB CORE_SOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} 
    src/core/*.cpp 
    src/data/*.cpp 
    src/io/*.cpp 
    src/processing/*.cpp 
    src/scanning/*.cpp 
    src/utils/*.cpp 
    src/utils/*.c
    src/commands/*.cpp)
list(REMOVE_ITEM CORE_SOURCES src/main.cpp)

list(APPEND MAIN_SOURCES src/main.cpp)

set(CMAKE_BUILD_TYPE RelWithDebInfo) # so we get debug info for backtraces
set(CMAKE_BUILD_PARALLEL_LEVEL 4)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(COMMON_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR})

###############################################################################
# modules and stuff
###############################################################################

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/build/cmake")

include(_ipo)
include(argparse)
#include(fmt)
include(gather_commands) # appends to MAIN_SOURCES
include(libbacktrace)
include(lz4)
include(mio)
include(nlohmann_json)
include(openssl)
include(spdlog)
#include(thread_pool)
include(zlib)
include(zstd)
include(lmdb)

if(APPLE)
    include(_apple)   # should be before setup_main(), but after all fetched-and-built libs
elseif(WIN32)
    include(_windows)
endif()

###############################################################################
# Helper macros to reduce repetition
###############################################################################

function(setup_common target_name)
    target_include_directories(${target_name} PRIVATE ${COMMON_INCLUDE_DIRS})
    target_link_directories(${target_name} PRIVATE ${COMMON_LINK_DIRS})

    # show more warnings
    if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic -Wparentheses -Wno-unused-function)
    elseif (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        target_compile_options(${target_name} PRIVATE /W4 /analyze)
    endif()
endfunction()

function(setup_main target_name is_static)
    if (${ARGC} GREATER 2)
        set(target_name "${target_name}${ARGV2}")
    endif()
    set(output_name ${target_name})

    add_executable(${target_name} ${MAIN_SOURCES})
    if (is_static)
        if (NOT WIN32)
            # win32 has them already linked in core
            target_link_libraries(${target_name} PRIVATE ${STATIC_LIBS})
        endif()
    else()
        target_link_libraries(${target_name} PRIVATE ${DYNAMIC_LIBS})
    endif()
    set_target_properties(${target_name} PROPERTIES OUTPUT_NAME "${output_name}")
    target_link_libraries(${target_name} PRIVATE core)
    add_dependencies(${target_name} version)
    setup_common(${target_name})
endfunction()

###############################################################################
# Main target setup
###############################################################################

# Build core lib just once for all targets
add_library(core STATIC ${CORE_SOURCES})
target_link_libraries(core PRIVATE ${COMMON_LIBS})
setup_common(core)

if(WIN32)
    # core needs openssl, so linking it here will build it automatically as a dependency.
    target_link_libraries(core PRIVATE ${STATIC_LIBS} -static)
    # static only, because required msvc*.dll are typically missing, and exe size difference is negligible.
    setup_main(${PROJECT_NAME} TRUE)
elseif(APPLE)
    # dynamic only, because static libraries on macOS are not supported by default
    setup_main(${PROJECT_NAME} FALSE)
else()
    setup_main(${PROJECT_NAME} FALSE)
    setup_main(${PROJECT_NAME} TRUE "_static")
endif()

# should be after setup_main()
if(APPLE)
    add_custom_command(TARGET ${PROJECT_NAME}
        POST_BUILD
        COMMAND dsymutil $<TARGET_FILE:${PROJECT_NAME}>
        COMMENT "Running dsymutil to generate debug symbols for macOS"
    )
endif()

# Clean target equivalent to Makefile's clean
set_directory_properties(PROPERTIES CLEAN_NO_CUSTOM true)

add_custom_target(version
    ${CMAKE_COMMAND} -D SRC=${CMAKE_SOURCE_DIR}/build/version.h.in
    -D DST=${CMAKE_SOURCE_DIR}/dist/version.h
    -P ${CMAKE_SOURCE_DIR}/build/cmake/generate_version.cmake
    )

###############################################################################
# Install rules to copy binaries to ./dist directory
###############################################################################

# Determine platform-specific subdirectory
if(WIN32)
    set(DIST_PLATFORM "windows")
elseif(APPLE)
    set(DIST_PLATFORM "macos")
else()
    set(DIST_PLATFORM "linux")
endif()

# Set the installation prefix to the dist directory
set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/dist/${DIST_PLATFORM}" CACHE PATH "Install prefix" FORCE)

# Install the main executable
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION .
    BUNDLE DESTINATION .
)

# Install the static executable if it exists (Linux only)
if(NOT WIN32 AND NOT APPLE)
    install(TARGETS ${PROJECT_NAME}_static
        RUNTIME DESTINATION .
    )
endif()

enable_testing() # should be before defining any tests
add_subdirectory(tests)
