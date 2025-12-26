#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <mio/mmap.hpp>

#include "Veeam/VBK/digest_t.hpp"
#include "core/structs.hpp"

using digest_t = Veeam::VBK::digest_t;

struct __attribute__((packed)) HashEntry {
    uint64_t offset;
    digest_t hash;
    digest_t keyset_id;
    uint32_t comp_size;
    uint32_t orig_size;
    ECompType comp_type;  // compression type for carved blocks
    uint8_t device_index;
    uint8_t padding[6];   // pad to 56 bytes (not strictly necessary, but good for performance)

    const std::string to_string() const {
        return fmt::format("<HashEntry offset={:x}, hash={}, keyset_id={}, comp_size={:x}, orig_size={:x}, comp_type={}>",
            offset, hash, keyset_id, comp_size, orig_size, (uint8_t)comp_type);
    }
};

static_assert(sizeof(HashEntry) == 56, "HashEntry size must be 56 bytes");
struct CacheFileHeader {
    static const uint64_t MAGIC   = 0x4c42545f48534148; // "HASH_TBL"
    static const uint64_t VERSION = 9; // Added keyset_id to HashEntry

    uint64_t magic = MAGIC;
    uint32_t version = VERSION;
    uint32_t entry_size = sizeof(HashEntry);
    uint64_t num_entries = 0;
    uint64_t number_of_devices = 1;
    bool valid(std::size_t num_of_devices) const {
        return magic == MAGIC && version == VERSION && entry_size == sizeof(HashEntry) && number_of_devices == num_of_devices;
    }
};

class HashTable {
public:
    HashTable() = default;
    
    bool loadFromTextFile(const std::string& filename, uint8_t device_index = 0);
    bool sortEntries();
    bool saveToCache(const std::filesystem::path&,std::size_t num_of_devices) const;
    bool loadFromCache(const std::filesystem::path&,std::size_t num_of_devices);

    const HashEntry* findHash(const digest_t& needle) const;
    size_t size() const { return (*this) ? (m_end - m_begin) : 0; }

    explicit operator bool() const {
        return m_begin != nullptr && m_end != nullptr;
    }
    
private:
    std::vector<HashEntry> m_entries;
    mio::mmap_source m_mmap;
    const HashEntry* m_begin = nullptr;
    const HashEntry* m_end = nullptr;

    bool loadMMap(const std::filesystem::path& cacheFilename, std::size_t num_of_devices);
    bool readAll(const std::filesystem::path& cacheFilename, std::size_t num_of_devices);

    static uint64_t Hex2Dec64(const std::string& hex);
    static std::vector<std::string> splitCSV(const std::string& str, char delimiter = ';');
    static ECompType parseCompType(const std::string& comp_str);
};
