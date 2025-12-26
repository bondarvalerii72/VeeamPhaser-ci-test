/**
 * @file hexdump.cpp
 * @brief Implementation of hexdump utility functions.
 *
 * This file provides C++ wrappers around the C hexdump implementation,
 * allowing hex dumping of std::string and std::vector<uint8_t> data.
 */

#include "hexdump.hpp"
#include "hexdump.c"

/**
 * @brief Hex dumps a std::string.
 * @param str String to dump.
 * @param prefix Prefix to print before each line.
 */
void hexdump(const std::string& str, const char* prefix) {
    hexdump(str.data(), str.size(), prefix);
}

/**
 * @brief Hex dumps a std::vector<uint8_t>.
 * @param str Vector to dump.
 * @param prefix Prefix to print before each line.
 */
void hexdump(const std::vector<uint8_t>& str, const char* prefix) {
    hexdump(str.data(), str.size(), prefix);
}
