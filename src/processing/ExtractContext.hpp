#pragma once
#include "core/CMeta.hpp"
#include "FileTestInfo.hpp"
#include "data/HashTable.hpp"
#include "data/lru_set.hpp"
#include "MD5.hpp"
#include "io/Reader.hpp"
#include <memory>

struct ExtractContext {
    using cache_t = lru_set<digest_t>;

    // vars required in constructor
    CMeta& meta;

    // vars required in process_file
    fs::path md_fname;
    fs::path json_fname;
    std::unique_ptr<Reader> vbkf;
    const HashTable& exHT;
    std::vector<std::unique_ptr<Reader>>& device_files;
    std::string xname;
    bool xname_is_glob = false;
    bool xname_is_full = false;
    PhysPageId needle_ppi;
    bool test_only;
    bool need_table_header = true;
    bool have_vbk = true;
    bool found = false;
    bool no_read = false;
    uint64_t vbk_offset = 0;
    cache_t& m_cache;
    MD5 m_md5;

    std::unordered_set<digest_t> used_bds;
    BlockDescriptors bds;

    ExtractContext(CMeta& meta, std::unique_ptr<Reader> vbkf, const HashTable& exHT, std::vector<std::unique_ptr<Reader>>& device_files, cache_t& cache, const Logger::level prev_level, const bool level_changed);
    ~ExtractContext();

    void process_file(const std::string& pathname, const CMeta::VFile& vFile, bool resume = false);
};
