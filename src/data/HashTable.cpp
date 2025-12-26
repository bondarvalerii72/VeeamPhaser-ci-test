/**
 * @file HashTable.cpp
 * @brief Implementation of HashTable for managing carved block hash lookups.
 *
 * This file provides a hash table implementation for efficiently storing and looking up
 * MD5 hashes of carved data blocks. It supports loading from CSV files, caching to binary
 * format, memory-mapped file access for performance, and binary search for fast hash lookups.
 * The hash table is critical for reconstructing files from carved data when the original
 * VBK structure is unavailable or corrupted.
 */

#include "HashTable.hpp"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "utils/Progress.hpp"
#include "io/Writer.hpp"
#include "io/Reader.hpp"

/**
 * @brief Parses a compression type string into the corresponding enum value.
 *
 * @param comp_str String representation of compression type ("LZ4", "ZLIB", "NONE").
 * @return Corresponding ECompType enum value.
 * @throws std::invalid_argument If compression type string is unknown.
 */
ECompType HashTable::parseCompType(const std::string& comp_str) {
    if (comp_str == "LZ4") return CT_LZ4;
    if (comp_str == "ZLIB") return CT_ZLIB_LO;
    if (comp_str == "NONE") return CT_NONE;
    
    throw std::invalid_argument("Unknown compression type: " + comp_str);
}

/**
 * @brief Loads hash table entries from a CSV text file.
 *
 * Parses a CSV file containing carved block information with fields:
 * offset, compressed_size, original_size, MD5_hash, CRC32, compression_type.
 * Each entry is tagged with the specified device index for multi-device scenarios.
 *
 * @param filename Path to the CSV file to load.
 * @param device_index Index identifying which device/source this data came from.
 * @return True if file was successfully loaded, false if file cannot be opened.
 */
bool HashTable::loadFromTextFile(const std::string& filename, uint8_t device_index) {
    std::ifstream file(filename);
    if (!file.is_open()) return false;

    size_t fsize = 0;
    file.seekg(0, std::ios::end);
    fsize = file.tellg();
    file.seekg(0, std::ios::beg);

    m_entries.reserve(m_entries.size() + fsize / 80); // bytes per line approx
    std::string line;

    Progress progress(fsize);
    
    while (std::getline(file, line)) {
        if (line.empty()) continue;

        auto tokens = splitCSV(line);
        if (tokens.size() >= 5) {
            HashEntry entry;
            entry.offset = Hex2Dec64(tokens[0]);
            entry.comp_size = (uint32_t)Hex2Dec64(tokens[1]);
            entry.orig_size = (uint32_t)Hex2Dec64(tokens[2]);
            entry.hash = digest_t::parse(tokens[3]);
            entry.keyset_id = digest_t(0);
            // Parse keyset_id and compression type if available
            if (tokens.size() >= 7) {
                entry.comp_type = parseCompType(tokens[5]);
                entry.keyset_id = digest_t::parse(tokens[6]);
            } else if (tokens.size() == 6) {
                // Parse compression type if available (6th field in CSV)
                const std::string& t5 = tokens[5];
                try {
                    entry.comp_type = parseCompType(t5);
                } catch (const std::invalid_argument&) {
                    entry.comp_type = CT_LZ4; // legacy default
                    entry.keyset_id = digest_t::parse(t5);
                }
            } else {
                entry.comp_type = CT_LZ4;  // Default for legacy entries : TODO: verify if this fallback is appropriate
            }
            entry.device_index = device_index;
            // entry.crc = (uint32_t)Hex2Dec64(tokens[4]);
            
            m_entries.push_back(entry);
            progress.found();
        }
        progress.update(file.tellg());
    }

    progress.finish();
    
    m_begin = m_entries.data();
    m_end = m_begin + m_entries.size();
    return true;
}

/**
 * @brief Sorts hash table entries and removes duplicates.
 *
 * Sorts all entries by hash value for efficient binary search and removes
 * duplicate entries (same hash). This must be called after loading all
 * data files and before performing lookups.
 *
 * @return True if entries were sorted successfully, false if hash table is empty.
 */
bool HashTable::sortEntries() {

    if (m_entries.empty()) {
        return false;
    }

    std::sort(m_entries.begin(), m_entries.end(), [](const HashEntry& a, const HashEntry& b) {
        return a.hash < b.hash;
    });

    m_entries.erase(std::unique(m_entries.begin(), m_entries.end(), [](const HashEntry& a, const HashEntry& b) {
        return a.hash == b.hash;
    }), m_entries.end());

    m_begin = m_entries.data();
    m_end = m_begin + m_entries.size();

    return true;
}

/**
 * @brief Saves the hash table to a binary cache file.
 *
 * Writes the hash table to a binary format for faster loading in subsequent runs.
 * The cache includes a header with entry count and device count for validation.
 *
 * @param cacheFilename Path where the cache file will be written.
 * @param num_of_devices Number of source devices (for cache validation).
 * @return True on successful save.
 */
bool HashTable::saveToCache(const std::filesystem::path& cacheFilename, std::size_t num_of_devices) const {
    Writer file(cacheFilename);

    CacheFileHeader hdr;
    hdr.num_entries = m_entries.size();
    hdr.number_of_devices = num_of_devices; 
    
    file.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
    file.write(reinterpret_cast<const char*>(m_entries.data()), m_entries.size() * sizeof(HashEntry));
    
    return true;
}

/**
 * @brief Loads hash table from cache using memory-mapped file I/O.
 *
 * Uses memory mapping for efficient access to large hash tables without
 * loading all data into memory. Falls back to regular file I/O if mapping fails.
 *
 * @param cacheFilename Path to the binary cache file.
 * @param num_of_devices Expected number of devices for validation.
 * @return True if successfully memory-mapped, false on error.
 */
bool HashTable::loadMMap(const std::filesystem::path& cacheFilename, std::size_t num_of_devices) {
    std::error_code error;
    m_mmap = mio::make_mmap_source(cacheFilename.native(), error);
    if (error) {
        std::cerr << "Error mapping cache file: " << error.message() << std::endl;
        return false;
    }

    const CacheFileHeader* hdr = reinterpret_cast<const CacheFileHeader*>(m_mmap.data());

    if (!hdr || !hdr->valid(num_of_devices)) {
        m_mmap.unmap();
        std::cerr << "Invalid cache file header" << std::endl;
        return false;
    }

    m_begin = reinterpret_cast<const HashEntry*>(m_mmap.data() + sizeof(CacheFileHeader));
    m_end = m_begin + hdr->num_entries;

    return true;
}

/**
 * @brief Loads entire hash table into memory from cache file.
 *
 * Reads the complete hash table from a binary cache file into a vector.
 * This is the fallback method when memory mapping is not available.
 *
 * @param cacheFilename Path to the binary cache file.
 * @param num_of_devices Expected number of devices for validation.
 * @return True if successfully loaded, false on error or validation failure.
 */
bool HashTable::readAll(const std::filesystem::path& cacheFilename, std::size_t num_of_devices) {
    Reader file(cacheFilename);
    
    CacheFileHeader hdr = {}; // initialize all members to 0
    file.read_at(0, &hdr, sizeof(hdr));

    if( !hdr.valid(num_of_devices) ){
        std::cerr << "Invalid cache file header" << std::endl;
        return false;
    }
    
    m_entries.resize(hdr.num_entries);
    file.read_at(sizeof(hdr), m_entries.data(), hdr.num_entries * sizeof(HashEntry));
    m_begin = &m_entries[0];
    m_end = m_begin + m_entries.size();
    
    return true;
}

/**
 * @brief Loads hash table from binary cache file.
 *
 * Attempts to load using memory mapping first for performance, falls back
 * to reading the entire file if mapping fails.
 *
 * @param cacheFilename Path to the binary cache file.
 * @param num_of_devices Expected number of devices for validation.
 * @return True if successfully loaded by either method, false otherwise.
 */
bool HashTable::loadFromCache(const std::filesystem::path& cacheFilename, std::size_t num_of_devices) {
    m_entries.clear();
    if (loadMMap(cacheFilename, num_of_devices))
        return true;

    return readAll(cacheFilename, num_of_devices);
}

/**
 * @brief Searches for a hash in the sorted hash table using binary search.
 *
 * Performs an efficient O(log n) binary search to find an entry matching
 * the specified MD5 hash. The hash table must be sorted before calling this.
 *
 * @param needle MD5 hash digest to search for.
 * @return Pointer to matching HashEntry if found, nullptr otherwise.
 */
const HashEntry* HashTable::findHash(const digest_t& needle) const {
    const auto* it = std::lower_bound(m_begin, m_end, needle, [](const HashEntry& entry, const digest_t& needle) {
        return entry.hash < needle;
    });

    if( it != m_end && it->hash == needle ) {
        return const_cast<HashEntry*>(it);
    }
    
    return nullptr;
}

/**
 * @brief Converts a hexadecimal string to a 64-bit unsigned integer.
 *
 * @param hex Hexadecimal string to convert (with or without "0x" prefix).
 * @return Converted 64-bit unsigned integer value.
 */
uint64_t HashTable::Hex2Dec64(const std::string& hex) {
    uint64_t result;
    std::stringstream ss;
    ss << std::hex << hex;
    ss >> result;
    return result;
}

/**
 * @brief Splits a CSV string into tokens based on delimiter.
 *
 * @param str String to split.
 * @param delimiter Character delimiter (default: comma).
 * @return Vector of string tokens.
 */
std::vector<std::string> HashTable::splitCSV(const std::string& str, char delimiter) {
    std::vector<std::string> tokens;
    std::stringstream ss(str);
    std::string token;
    while (std::getline(ss, token, delimiter)) {
        tokens.push_back(token);
    }
    return tokens;
}
