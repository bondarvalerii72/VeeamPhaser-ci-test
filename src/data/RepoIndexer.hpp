#pragma once
#include "core/structs.hpp"
#include "processing/MD5.hpp"
#include <lmdb.h>

struct FileEntry {
    uint64_t file_id;
    uint64_t offset;
    uint32_t compLen;    // Stored size (compressed); 0 if flat
    uint32_t unCompLen;  // Original size (uncompressed)
    uint32_t typ;        // Type: 1 = flat, 2 = LZ4 (compressed)
};

struct LookupEntry {
    uint64_t file_id;
    std::string filePath;
};

class RepoIndexer {
public:
    explicit RepoIndexer(const std::string& lmdbPath = "repo.dat", const std::string& sampleFile = "");
    ~RepoIndexer();

    void indexFiles(int mode, const std::vector<std::string>& files);
    bool repairFile(const std::string& errorFilePath, const std::string& targetFilePath);
    bool findHash(digest_t md5hash, std::string &filePath, uint64_t &offset, uint32_t &compLenp, uint32_t &unCompLenp);

private:
    MDB_env* env;
    MDB_dbi repo_db;
    MDB_dbi lookup_db;
    MD5 m_md5;

    bool updateRepo(digest_t md5hash, uint64_t file_id, uint64_t offset, uint32_t unCompLen, uint32_t compLen, uint32_t typ);
    void updateFileLookup(uint64_t file_id, const std::string& filePath);

    bool processFileMode0(const std::string& inputFile, uint64_t file_id);
    bool processFileMode1(const std::string& inputFile, uint64_t file_id);

    void md5buf(const uint8_t* data, size_t length, uint8_t* md5_result);
    bool compareHashes(const uint8_t* hash1, const uint8_t* hash2);

    std::vector<BlockStruct> vbListBlocksRepo(FILE* f);
};
