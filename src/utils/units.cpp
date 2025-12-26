/**
 * @file units.cpp
 * @brief Implementation of unit conversion utilities.
 *
 * This file provides functions for converting between human-readable and numeric
 * representations of data sizes (bytes/KB/MB/GB/TB) and time durations (seconds
 * to days/hours/minutes/seconds). Supports both binary (1024-based) and hexadecimal
 * input formats.
 */

#include "units.hpp"

#include <algorithm>
#include <map>
#include <stdexcept>
#include <vector>

/**
 * @brief Converts bytes to human-readable string with appropriate unit.
 *
 * Automatically selects the most appropriate unit (bytes, Kb, Mb, Gb, Tb)
 * based on the size, ensuring the numeric value is less than 4096.
 *
 * @param size Size in bytes.
 * @param default_unit Unit suffix to use for raw bytes (e.g., " bytes", "").
 * @param min_unit Minimum unit divisor (1 for bytes, 1024 for KB, etc.).
 * @return Human-readable size string (e.g., "15Mb", "2048 bytes").
 */
std::string bytes2human(uint64_t size, const char* default_unit, uint64_t min_unit){
    static const std::vector<std::string> units { "", "Kb", "Mb", "Gb", "Tb" };

    size_t i = 0;
    while( min_unit > 1 ){
        min_unit /= 1024;
        size /= 1024;
        i++;
    }
    while( i<units.size()-1 && size >= 4096 ){
        i++;
        size /= 1024;
    }
    return std::to_string(size) + (i == 0 ? default_unit : units[i]);
}

/**
 * @brief Converts human-readable size string to bytes.
 *
 * Parses strings like "15Mb", "2GB", "0x1000", "4096" and converts them
 * to byte values. Supports KB/MB/GB/TB units (case-insensitive) and
 * hexadecimal notation (0x prefix).
 *
 * @param size Human-readable size string.
 * @return Size in bytes.
 * @throws std::runtime_error If unit is unsupported or value overflows.
 */
uint64_t human2bytes(const std::string& size) {
    static const std::map<std::string, uint64_t> units = {
        {"kb", 1024},
        {"mb", 1024 * 1024},
        {"gb", 1024 * 1024 * 1024},
        {"tb", 1024ULL * 1024 * 1024 * 1024}
    };

    // if size starts with "0x" then it's a hex number
    if (size.length() > 2 && size[0] == '0' && (size[1]|0x20) == 'x') {
        return std::stoull(size, nullptr, 16);
    }

    size_t i = 0;
    for (; i < size.length(); ++i) {
        if (!isdigit(size[i])) {
            break;
        }
    }

    std::string numberPart = size.substr(0, i);
    std::string unitPart = i < size.length() ? size.substr(i) : "";
    std::transform(unitPart.begin(), unitPart.end(), unitPart.begin(), ::tolower);

    uint64_t number = std::stoull(numberPart);

    if( unitPart.size() == 1 )
        unitPart += 'b';

    if (!unitPart.empty() && units.find(unitPart) == units.end()) {
        throw std::runtime_error("Unsupported unit: " + unitPart);
    }

    uint64_t multiplier = unitPart.empty() ? 1 : units.at(unitPart);
    uint64_t result = number * multiplier;

    // Simple overflow check, not comprehensive
    if (multiplier != 1 && result / multiplier != number) {
        throw std::runtime_error("Resulting value out of range.");
    }

    return result;
}

/**
 * @brief Converts seconds to human-readable duration string.
 *
 * Formats time duration as combinations of days, hours, minutes, and seconds
 * (e.g., "2d5h", "3h15m30s", "45s"). The maxUnits parameter limits how many
 * different units are shown.
 *
 * @param seconds Duration in seconds.
 * @param maxUnits Maximum number of different time units to display.
 * @return Human-readable duration string.
 */
std::string seconds2human(uint64_t seconds, size_t maxUnits) {
    // Define time units and their corresponding abbreviations
    static const std::vector<std::pair<uint64_t, std::string>> units = {
        {86400, "d"}, // Days
        {3600, "h"},  // Hours
        {60, "m"},    // Minutes
        {1, "s"}      // Seconds
    };

    std::string result;
    size_t unitsAdded = 0;

    for (const auto& unit : units) {
        if (seconds >= unit.first || unitsAdded > 0) { // Ensure we process lower units if a higher one has been added
            if (unitsAdded < maxUnits) {
                uint64_t amount = seconds / unit.first;
                seconds %= unit.first; // Calculate remainder for the next unit
                if (amount > 0 || unitsAdded > 0) { // Add the unit if it's non-zero or if we've already added a unit before
                    result += std::to_string(amount) + unit.second;
                    ++unitsAdded;
                }
            } else {
                break; // Stop if we've added the maximum number of units
            }
        }
    }

    return result.empty() ? "0s" : result; // Handle the case where seconds is 0
}
