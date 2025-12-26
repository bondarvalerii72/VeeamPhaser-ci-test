/**
 * @file TestCommand.cpp
 * @brief Implementation of the TestCommand for system self-testing.
 *
 * This file provides functionality to verify that the compilation environment
 * and type sizes meet the requirements for the application. It checks that
 * fundamental types have the expected sizes, which is critical for binary
 * data structure parsing and file format compatibility.
 */

#include "TestCommand.hpp"
#include "utils/common.hpp"

REGISTER_COMMAND(TestCommand);

/**
 * @brief Constructs a TestCommand with the specified registration status.
 * @param reg Boolean indicating whether to register this command with the command registry.
 */
TestCommand::TestCommand(bool reg) : Command(reg, TEST_CMD_NAME, "self-test") {
}

/**
 * @brief Executes system self-tests to verify type sizes and platform compatibility.
 *
 * Validates that fundamental C/C++ types have the expected sizes:
 * - int: 4 bytes
 * - off_t: 8 bytes (for large file support)
 * - size_t: 8 bytes
 * - ssize_t: 8 bytes
 * - uint64_t: 8 bytes
 * - void*: 8 bytes (64-bit pointers)
 *
 * These checks ensure the application can correctly parse binary file formats
 * and handle large files on the current platform.
 *
 * @return EXIT_SUCCESS (0) if all tests pass, EXIT_FAILURE (1) if any test fails.
 */
int TestCommand::run() {
    logger->trace("selftest: sizeof(int)       = {}", sizeof(int));
    logger->trace("selftest: sizeof(off_t)     = {}", sizeof(off_t));
    logger->trace("selftest: sizeof(size_t)    = {}", sizeof(size_t));
    logger->trace("selftest: sizeof(ssize_t)   = {}", sizeof(ssize_t));
    logger->trace("selftest: sizeof(uint64_t)  = {}", sizeof(uint64_t));
    logger->trace("selftest: sizeof(uintmax_t) = {}", sizeof(uintmax_t));
    logger->trace("selftest: sizeof(void*)     = {}", sizeof(void*));

    if( sizeof(int) != 4 ){
        logger->critical("selftest: sizeof(int) != 4");
        return 1;
    }

    if( sizeof(off_t) != 8 ){
        logger->critical("selftest: sizeof(off_t) != 8");
        return 1;
    }

    if( sizeof(size_t) != 8 ){
        logger->critical("selftest: sizeof(size_t) != 8");
        return 1;
    }

    if( sizeof(ssize_t) != 8 ){
        logger->critical("selftest: sizeof(ssize_t) != 8");
        return 1;
    }

    if( sizeof(uint64_t) != 8 ){
        logger->critical("selftest: sizeof(uint64_t) != 8");
        return 1;
    }

    if( sizeof(void*) != 8 ){
        logger->critical("selftest: sizeof(void*) != 8");
        return 1;
    }

    logger->trace("selftest: OK");
    return 0;
}
