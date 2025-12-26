/**
 * @file CRC32Command.cpp
 * @brief Implementation of the CRC32Command for calculating CRC32 checksums.
 *
 * This file provides functionality to calculate both standard CRC32 (zlib) and
 * Veeam's custom CRC32 variant (vcrc32) for files or file regions. It supports
 * calculating checksums for specific offsets and sizes, as well as iterating
 * through a range of sizes for testing purposes.
 */

#include "CRC32Command.hpp"
#include "utils/common.hpp"
#include <zlib.h>

extern "C" {
    uint32_t vcrc32(uint32_t crc, const void *buf, unsigned int len);
}

REGISTER_COMMAND(CRC32Command);

/**
 * @brief Constructs a CRC32Command with the specified registration status.
 * @param reg Boolean indicating whether to register this command with the command registry.
 */
CRC32Command::CRC32Command(bool reg) : Command(reg, "crc32", "calc crc32 of a file") {
    m_parser.add_argument("filename").help("file to calc crc32");
    m_parser.add_argument("--offset").default_value((uint64_t)0ULL).scan<'x', uint64_t>().help("start offset (hex)");
    m_parser.add_argument("--size").default_value((uint64_t)0ULL).scan<'x', uint64_t>().help("size (hex)");
    m_parser.add_argument("--min-size").default_value((uint64_t)0ULL).scan<'x', uint64_t>().help("min size (hex)");
    m_parser.add_argument("--max-size").default_value((uint64_t)0ULL).scan<'x', uint64_t>().help("max size (hex)");
}

/**
 * @brief Calculates and displays both standard and Veeam CRC32 checksums for a file region.
 *
 * Reads data from the specified file at the given offset and size, then calculates
 * both the standard CRC32 (using zlib) and Veeam's custom vcrc32, displaying both results.
 *
 * @param fname Path to the file to checksum.
 * @param offset Starting offset in the file (in bytes).
 * @param size Number of bytes to read. If 0, reads from offset to end of file.
 */
void show_crc32(std::string fname, off_t offset, size_t size){
    FILE* f = fopen(fname.c_str(), "rb");

    if( size == 0 ){
        fseek(f, 0, SEEK_END);
        size = ftell(f) - offset;
    }
    fseek(f, offset, SEEK_SET);


    char* buf = (char*)malloc(size);
    size_t nread = fread(buf, 1, size, f);
    if( nread != size ){
        logger->warn("nread {:x} != size {:x}", nread, size);
    }

    fmt::print("{}: offset={:x} size={:04x} crc32={:08x} vcrc32={:08x}\n", fname, offset, size, crc32(0, (const Bytef*)buf, size), vcrc32(0, buf, size));

    free(buf);
    fclose(f);
}

/**
 * @brief Executes the CRC32 command to calculate checksums.
 *
 * If min-size and max-size are specified, iterates through all sizes in that range
 * calculating checksums for each. Otherwise, calculates checksum for the specified
 * offset and size.
 *
 * @return EXIT_SUCCESS (0) on successful completion.
 */
int CRC32Command::run() {
    if( m_parser.is_used("--min-size") && m_parser.is_used("--max-size") ){
        for( uint64_t size = m_parser.get<uint64_t>("--min-size"); size <= m_parser.get<uint64_t>("--max-size"); size++ ){
            show_crc32(m_parser.get("filename"), m_parser.get<uint64_t>("--offset"), size);
        }
    } else {
        show_crc32(m_parser.get("filename"), m_parser.get<uint64_t>("--offset"), m_parser.get<uint64_t>("--size"));
    }
    return 0;
}
