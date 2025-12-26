/**
 * @file to_hexdump.cpp
 * @brief Implementation of configurable hex dump formatting.
 *
 * This file provides a Hexdump class that can format binary data as hex dumps
 * with automatic width selection or explicit width specification. It supports
 * duplicate line compression (showing * for repeated lines) and customizable
 * indentation and prefixes.
 */

#include "to_hexdump.hpp"

extern int g_hexdump_width;

/**
 * @brief Converts the Hexdump to a formatted string.
 *
 * If g_hexdump_width is set, uses that width. Otherwise, tries 16, 24, and 32
 * byte widths and returns the result with the fewest lines.
 *
 * @return Formatted hex dump string.
 */
Hexdump::operator std::string() const {
    if( g_hexdump_width > 0 ){
        // explicit width
        return to_string(g_hexdump_width);
    } else {
        // try 16, 24, 32 and return the shortest dump
        std::string output;
        int best_newlines = 0;

        for( int width : {32, 24, 16} ){
            std::string tmp = to_string(width);
            int newlines = std::count(tmp.begin(), tmp.end(), '\n');
            if( output.empty() || newlines < best_newlines ){
                output = tmp;
                best_newlines = newlines;
            }
        }
        return output;
    }
}

/**
 * @brief Formats the hex dump with the specified line width.
 *
 * Creates a hex dump with the given number of bytes per line, including
 * both hex values and ASCII representation. Compresses duplicate lines
 * by showing a single * character.
 *
 * @param width Number of bytes per line (typically 16, 24, or 32).
 * @return Formatted hex dump string.
 */
std::string Hexdump::to_string(size_t width) const {
    char tmp[0x20];
    uint8_t *buf = (uint8_t*)m_ptr;
    std::vector<uint8_t> last_line(width);
    int last_line_valid = 0;
    int duplicate_count = 0;

    std::string output = m_prefix;

    for (size_t i = 0; i < m_size; i += width) {
        std::vector<uint8_t> current_line(width);
        size_t line_size = (i + width < m_size) ? width : (m_size - i);

        // Copy the current line
        memcpy(current_line.data(), &buf[i], line_size);

        // Check if the current line is the same as the last line
        if (last_line_valid && memcmp(current_line.data(), last_line.data(), width) == 0) {
            // Duplicate line
            if (duplicate_count == 0) {
                snprintf(tmp, sizeof(tmp), "%*s*\n", m_indent, "");
                output += tmp; // Print "*" only once
            }
            duplicate_count++;
            continue;
        }

        // Format the offset
        snprintf(tmp, sizeof(tmp), "%*s%08zx:", m_indent, "", i);
        output += tmp;

        for (size_t j = 0; j < width; j++) {
            if ( j%8 == 0 )
                output += " ";

            if (i + j < m_size) {
                // Format tmp byte manually
                snprintf(tmp, sizeof(tmp), "%02x", buf[i + j]);
                output += tmp;
                output += " ";
            } else {
                output += "   ";  // Fill space if less than width bytes in a line
            }
        }

        output += " ";

        for (size_t j = 0; j < width; j++) {
            if (i + j < m_size)
                output += isprint(buf[i + j]) ? (char)buf[i + j] : '.';
        }

        output += "\n";

        // Update the last line and reset the duplicate count
        last_line = std::move(current_line);
        last_line_valid = 1;
        duplicate_count = 0;
    }

    return output;
}
