/**
 * @file FileTestInfo.cpp
 * @brief Implementation of file test and extraction statistics tracking.
 *
 * This file provides functionality to track and report statistics about file
 * extraction and testing operations. It maintains counts of successful blocks,
 * various error types, and provides formatted output in both human-readable
 * and JSON formats for logging and reporting.
 */

#include "FileTestInfo.hpp"

#include <nlohmann/json.hpp>

/**
 * @brief Returns a formatted header string for tabular output.
 * @return Header string with column labels for file statistics.
 */
std::string FileTestInfo::header() const {
    return fmt::format("{:>9} {:>9} {:>9} {:>7} {:>8} {:>8} {:>8} {:>8} {:>8} {:>8}  {:9}  {}",
        "TotalBLK", "sparse", "OK_BLK", "OK%", "missMD", "missHT", "errRead", "eDecomp", "errCRC", "size", "id", "name");
}

/**
 * @brief Formats file statistics as a human-readable string.
 *
 * @param color If true, includes ANSI color codes for visual indication of status.
 * @return Formatted statistics string with all counts and percentages.
 */
std::string FileTestInfo::to_string(bool color) const {
    double perc = percent();
    return fmt::format("{:9} {:9} {:9} {}{:7.2f}{} {}{:8}{} {}{:8}{} {:8} {:8} {:8} {:>8}  {}  {}",
        total_blocks,
        sparse_blocks,
        nOK,
        (color ? (perc == 100.0 ? ANSI_COLOR_GREEN : (perc <= 50.0 ? ANSI_COLOR_RED : ANSI_COLOR_YELLOW)) : ""), perc, (color ? ANSI_COLOR_RESET : ""),
        (color && nMissMD != 0 ? ANSI_COLOR_RED    : ""), nMissMD, (color ? ANSI_COLOR_RESET : ""),
        (color && nMissHT != 0 ? ANSI_COLOR_YELLOW : ""), nMissHT, (color ? ANSI_COLOR_RESET : ""),
        nReadErr, nErrDecomp, nErrCRC, bytes2human(size), ppi, name);

}

/**
 * @brief Calculates the percentage of successfully extracted blocks.
 *
 * Excludes sparse blocks from calculation. Returns 99.99% if all blocks succeeded
 * but there were still some errors (edge case handling).
 *
 * @return Percentage of successful blocks (0.0 to 100.0).
 */
double FileTestInfo::percent() const {
    size_t total = total_blocks - sparse_blocks;
    double perc = total == 0 ? 0 : (100.0 * nOK / total);
    if (perc >= 100.0 && (nMissMD !=0 || nMissHT != 0 || nErrDecomp != 0 || nErrCRC != 0 || nReadErr != 0)) {
        perc = 99.99;
    }
    return perc;
}

/**
 * @brief Formats file statistics as a JSON string.
 *
 * Provides machine-readable output of all statistics for programmatic processing
 * or logging to JSON files. Handles UTF-8 encoding of paths correctly on both
 * Windows and POSIX systems.
 *
 * @return JSON string containing all file statistics.
 */
std::string FileTestInfo::to_json() const {
    std::string md_fname_str;
#ifdef _WIN32
    // Use codecvt to convert wstring to UTF-8
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> conv;
    md_fname_str = conv.to_bytes(md_fname.wstring());
#else
    md_fname_str = md_fname.string(); // Already UTF-8 on POSIX
#endif

    const auto j = nlohmann::ordered_json{
            {"id", ppi.to_string()},
            {"pathname", pathname},
            {"size", size},
            {"type", fmt::format("{}", type)},
            {"total_blocks", total_blocks},
            {"sparse_blocks", sparse_blocks},
            {"nOK", nOK},
            {"percent", percent()},
            {"nMissMD", nMissMD},
            {"nMissHT", nMissHT},
            {"nErrDecomp", nErrDecomp},
            {"nErrCRC", nErrCRC},
            {"nReadErr", nReadErr},
            {"md_fname", md_fname_str}
    };
    return j.dump();
}
