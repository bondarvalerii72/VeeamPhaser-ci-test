/**
 * @file RepoIndexer.cpp
 * @brief Implementation of repository indexer for block-level deduplication and repair.
 *
 * This file provides functionality to index VIB/VBK files into an LMDB database,
 * enabling efficient hash-based lookups for data recovery and file repair operations.
 * It supports both LZ4 block and flat indexing modes, and can repair files using
 * indexed block hashes.
 */

#include "RepoIndexer.hpp"
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <random>
#include <lmdb.h>

#include "io/Errorlogger.hpp"
#include "io/Writer.hpp"

#include <lz4.h>

/**
 * @brief Lists all LZ4 blocks in a repository file by scanning for signatures.
 *
 * Scans the file for LZ4 block magic numbers and extracts block metadata
 * (position, CRC, size) for each found block.
 *
 * @param f File pointer to scan (positioned at beginning).
 * @return Vector of BlockStruct containing found block information.
 */
std::vector<BlockStruct> RepoIndexer::vbListBlocksRepo(FILE* f) {
    std::vector<BlockStruct> blocks;
    blocks.reserve(0x1000);

    fseek(f, 0, SEEK_END);
    size_t fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    const size_t bufsize = 0x8000000;
    unsigned char* buf = (unsigned char*)malloc(bufsize);
    uint64_t hs = 0;
    const unsigned char prlg[4] = { 0x0F, 0x00, 0x00, 0xF8 };

    while (hs < fsize) {
        size_t lzsz = (hs + bufsize > fsize) ? (fsize - hs) : bufsize;
        fread(buf, 1, lzsz, f);
        for (size_t i = 0; i + 11 < lzsz; i++) {
            if (memcmp(buf + i, prlg, 4) == 0 && buf[i + 11] == 0) {
                BlockStruct blk;
                blk.pos = hs + i;
                blk.crc = *(uint32_t*)(buf + i + 4);
                blk.srcSize = *(uint32_t*)(buf + i + 8);
                blocks.push_back(blk);
            }
        }
        hs += lzsz;
    }
    free(buf);
    return blocks;
}

RepoIndexer::RepoIndexer(const std::string& lmdbPath, const std::string& sampleFile) {
    int rc = mdb_env_create(&env);
    if (rc != MDB_SUCCESS) {
        std::cerr << "Error creating LMDB environment: " << mdb_strerror(rc) << std::endl;
        exit(1);
    }
    mdb_env_set_maxdbs(env, 2);

    size_t estimatedSize = 0;
    if (!sampleFile.empty()) {
        FILE* f = fopen(sampleFile.c_str(), "rb");
        if (f) {
            std::vector<BlockStruct> blocks = vbListBlocksRepo(f);
            fclose(f);

            size_t n = blocks.size();
            const size_t overhead = 10 * 1024 * 1024;
            const size_t factor = 4;
            estimatedSize = overhead + n * sizeof(FileEntry) * factor;
            std::cout << "Estimated mapsize based on " << n << " blocks: " << estimatedSize << " bytes" << std::endl;
        } else {
            std::cerr << "Sample file " << sampleFile << " could not be opened. Using default mapsize." << std::endl;
            estimatedSize = 10LL * 1024 * 1024 * 1024;
        }
    } else {
        estimatedSize = 10LL * 1024 * 1024 * 1024;
    }

    rc = mdb_env_set_mapsize(env, estimatedSize);
    if (rc != MDB_SUCCESS) {
        std::cerr << "Error setting mapsize: " << mdb_strerror(rc) << std::endl;
        exit(1);
    }

    rc = mdb_env_open(env, lmdbPath.c_str(), MDB_NOSUBDIR, 0664);
    if (rc != MDB_SUCCESS) {
        std::cerr << "Error opening LMDB: " << mdb_strerror(rc) << std::endl;
        exit(1);
    }
    MDB_txn* txn;
    rc = mdb_txn_begin(env, nullptr, 0, &txn);
    if (rc != MDB_SUCCESS) {
        std::cerr << "Error starting LMDB transaction: " << mdb_strerror(rc) << std::endl;
        exit(1);
    }
    rc = mdb_dbi_open(txn, "repo", MDB_CREATE, &repo_db);
    if (rc != MDB_SUCCESS) {
        std::cerr << "Error opening repo DB: " << mdb_strerror(rc) << std::endl;
        mdb_txn_abort(txn);
        exit(1);
    }
    rc = mdb_dbi_open(txn, "file_lookup", MDB_CREATE, &lookup_db);
    if (rc != MDB_SUCCESS) {
        std::cerr << "Error opening file_lookup DB: " << mdb_strerror(rc) << std::endl;
        mdb_txn_abort(txn);
        exit(1);
    }
    rc = mdb_txn_commit(txn);
    if (rc != MDB_SUCCESS) {
        std::cerr << "Error committing LMDB transaction: " << mdb_strerror(rc) << std::endl;
        exit(1);
    }
}

RepoIndexer::~RepoIndexer() {
    mdb_dbi_close(env, repo_db);
    mdb_dbi_close(env, lookup_db);
    mdb_env_close(env);
}

bool RepoIndexer::repairFile(const std::string& errorFilePath, const std::string& targetFilePath) {
    ErrorLogger errorData(errorFilePath);
    if (!errorData.loadFromFile()) {
        std::cerr << "Error loading the error file: " << errorFilePath << std::endl;
        return false;
    }

    int repairedBlocks = 0;
    for (const auto& block : errorData.getRecords()) {
        std::string filePath;
        uint64_t offset;
        uint32_t compLenb;
        uint32_t unCompLenb;

        if (findHash(block.hash, filePath, offset, compLenb, unCompLenb)) {
            std::ifstream sourceFile(filePath, std::ios::binary);
            if (!sourceFile) {
                std::cerr << "Error opening the source file: " << filePath << std::endl;
                continue;
            }
            if(compLenb == 0){
                uint32_t SizeB = unCompLenb;
                buf_t uncompressedBuffer(SizeB);
                sourceFile.seekg(offset);
                sourceFile.read(reinterpret_cast<char*>(uncompressedBuffer.data()), SizeB);
                digest_t computedHash = m_md5.Calculate(uncompressedBuffer);
                if (computedHash == block.hash){
                    Writer writer(targetFilePath, false);
                    writer.write_at(block.filePos, uncompressedBuffer.data(), SizeB);

                    repairedBlocks++;
                    std::cout << "Fixed block: Hash " << fmt::format("{}", block.hash)
                      << " of " << filePath << " to file: " << targetFilePath << " pos: " << block.filePos << std::endl;

                }

            } else {
                uint32_t SizeB = (compLenb * 4);
                buf_t uncompressedBuffer(SizeB);

                sourceFile.seekg(offset);
                sourceFile.read(reinterpret_cast<char*>(uncompressedBuffer.data()), (SizeB));

                /*int lz4Result =*/ LZ4_decompress_safe(reinterpret_cast<const char*>(uncompressedBuffer.data()),
                                                reinterpret_cast<char*>(uncompressedBuffer.data()), 
                                                SizeB-0xC, sizeof(uncompressedBuffer));

                digest_t computedHash = m_md5.Calculate(uncompressedBuffer);
                if (computedHash == block.hash){
                    Writer writer(targetFilePath, false);
                    writer.write_at(block.filePos, uncompressedBuffer.data(), unCompLenb);
                    repairedBlocks++;
                }
            }
        }
    }

    std::cout << "Repair complete. Blocks fixed: " << repairedBlocks << std::endl;
    return repairedBlocks > 0;
}

bool RepoIndexer::updateRepo(digest_t md5hash, uint64_t file_id, uint64_t offset, uint32_t unCompLen, uint32_t compLen, uint32_t typ) {
    MDB_txn* txn;
    int rc = mdb_txn_begin(env, nullptr, 0, &txn);
    if (rc != MDB_SUCCESS) {
        std::cerr << "Error starting transaction in updateRepo: " << mdb_strerror(rc) << std::endl;
        return false;
    }
    MDB_val key, data;
    key.mv_size = MD5::DIGEST_LENGTH;
    key.mv_data = &md5hash;
    rc = mdb_get(txn, repo_db, &key, &data);
    if (rc == MDB_SUCCESS) {
        mdb_txn_commit(txn);
        return false;
    } else if (rc != MDB_NOTFOUND) {
        std::cerr << "Error in mdb_get: " << mdb_strerror(rc) << std::endl;
        mdb_txn_abort(txn);
        return false;
    }
    FileEntry entry;
    entry.file_id = file_id;
    entry.offset = offset;
    entry.unCompLen = unCompLen;
    entry.compLen = compLen;
    entry.typ = typ;
    data.mv_size = sizeof(FileEntry);
    data.mv_data = &entry;
    rc = mdb_put(txn, repo_db, &key, &data, 0);
    if (rc != MDB_SUCCESS) {
        std::cerr << "Error in mdb_put: " << mdb_strerror(rc) << std::endl;
        mdb_txn_abort(txn);
        return false;
    }
    rc = mdb_txn_commit(txn);
    if (rc != MDB_SUCCESS) {
        std::cerr << "Error committing transaction in updateRepo: " << mdb_strerror(rc) << std::endl;
        return false;
    }
    return true;
}

void RepoIndexer::updateFileLookup(uint64_t file_id, const std::string& filePath) {
    MDB_txn* txn;
    int rc = mdb_txn_begin(env, nullptr, 0, &txn);
    if (rc != MDB_SUCCESS) {
        std::cerr << "Error starting transaction in updateFileLookup: " << mdb_strerror(rc) << std::endl;
        return;
    }
    MDB_val key, data;
    key.mv_size = sizeof(file_id);
    key.mv_data = &file_id;
    data.mv_size = filePath.size();
    data.mv_data = const_cast<char*>(filePath.data());
    rc = mdb_put(txn, lookup_db, &key, &data, 0);
    if (rc != MDB_SUCCESS) {
        std::cerr << "Error in mdb_put in updateFileLookup: " << mdb_strerror(rc) << std::endl;
        mdb_txn_abort(txn);
        return;
    }
    rc = mdb_txn_commit(txn);
    if (rc != MDB_SUCCESS) {
        std::cerr << "Error committing transaction in updateFileLookup: " << mdb_strerror(rc) << std::endl;
    }
}

bool RepoIndexer::findHash(digest_t md5hash, std::string &filePath, uint64_t &offset, uint32_t &compLenp, uint32_t &unCompLenp) {
    MDB_txn* txn;
    int rc = mdb_txn_begin(env, nullptr, MDB_RDONLY, &txn);
    if (rc != MDB_SUCCESS) {
        std::cerr << "Error starting read-only transaction in findHash: " << mdb_strerror(rc) << std::endl;
        return false;
    }
    MDB_val key, data;
    key.mv_size = MD5::DIGEST_LENGTH;
    key.mv_data = &md5hash;
    rc = mdb_get(txn, repo_db, &key, &data);
    if (rc != MDB_SUCCESS) {
        std::cerr << "Hash not found in repo_db: " << mdb_strerror(rc) << std::endl;
        mdb_txn_abort(txn);
        return false;
    }
    if (data.mv_size != sizeof(FileEntry)) {
        std::cerr << "Unexpected data size for FileEntry" << std::endl;
        mdb_txn_abort(txn);
        return false;
    }
    FileEntry* entry = static_cast<FileEntry*>(data.mv_data);
    offset = entry->offset;
    compLenp = entry->compLen;
    unCompLenp = entry->unCompLen;

    MDB_val key2, data2;
    key2.mv_size = sizeof(entry->file_id);
    key2.mv_data = &entry->file_id;
    rc = mdb_get(txn, lookup_db, &key2, &data2);
    if (rc != MDB_SUCCESS) {
        std::cerr << "File lookup not found for file_id: " << mdb_strerror(rc) << std::endl;
        mdb_txn_abort(txn);
        return false;
    }
    filePath.assign(static_cast<char*>(data2.mv_data), data2.mv_size);
    mdb_txn_commit(txn);
    return true;
}

bool RepoIndexer::processFileMode0(const std::string& inputFile, uint64_t file_id) {
    bool newHashInserted = false;
    std::cout << "Processing file (mode 0): " << inputFile << std::endl;
    
    FILE* f = fopen(inputFile.c_str(), "rb");
    if (!f) {
        std::cerr << "Error opening file: " << inputFile << std::endl;
        return false;
    }
    
    std::vector<BlockStruct> blocks = vbListBlocksRepo(f);
    if (blocks.size() < 2) {
        std::cerr << "File without sufficient blocks for processing (mode 0): " << inputFile << std::endl;
        fclose(f);
        return false;
    }
    std::cout << "Total blocks listed: " << blocks.size() << std::endl;
    
    const size_t bufferSize = 0x1600000;
    std::vector<char> unpBuf(bufferSize);
    std::vector<char> unpBuf2(bufferSize);
    
    for (size_t i = 0; i < blocks.size() - 1; i++) {
        uint64_t ppSize = blocks[i+1].pos - blocks[i].pos;
        /*std::cout << "Block " << i << " | Pos: " << blocks[i].pos 
                  << " | ppSize: " << ppSize << " | srcSize: " << blocks[i].srcSize << std::endl;*/
        
        if (blocks[i].srcSize > 0 && blocks[i].srcSize <= 0x100000 && ppSize < bufferSize) {
            if (fseek(f, blocks[i].pos, SEEK_SET) != 0) {
                std::cerr << "Error seeking to block " << i << std::endl;
                continue;
            }
            size_t bytesRead = fread(unpBuf.data(), 1, ppSize, f);
            if (bytesRead != ppSize) {
                std::cerr << "Error reading block " << i << " (read: " << bytesRead 
                          << ", expected: " << ppSize << ")" << std::endl;
                continue;
            }
            
            std::fill(unpBuf2.begin(), unpBuf2.end(), 0);
            
            /*int lz4res =*/ LZ4_decompress_safe(unpBuf.data() + 0xC,
                                             unpBuf2.data(),
                                             ppSize,
                                             static_cast<int>(bufferSize));
            digest_t md5hash = m_md5.Calculate(unpBuf2.data(), blocks[i].srcSize);
            
            std::string hashStr = fmt::format("{}", md5hash);
            //std::cout << "Block " << i << " | MD5: " << hashStr << std::endl;
            
            if (updateRepo(md5hash, file_id, blocks[i].pos, blocks[i].srcSize, static_cast<uint32_t>(ppSize), 2))
                newHashInserted = true;
            else
                std::cout << "Block " << i << " not added (hash exists): " << hashStr << std::endl;
        } else {
            std::cout << "Block " << i << " not used." << std::endl;
        }
        
        if (i % 100 == 0)
            std::cout << "Processed " << i << " blocks out of " << (blocks.size()-1) << std::endl;
    }
    
    fclose(f);
    return newHashInserted;
}

bool RepoIndexer::processFileMode1(const std::string& inputFile, uint64_t file_id) {
    bool newHashInserted = false;
    std::cout << "Processing file (mode 1): " << inputFile << std::endl;
    
    std::ifstream inFile(inputFile, std::ios::binary);
    if (!inFile) {
        std::cerr << "Error opening file: " << inputFile << std::endl;
        return false;
    }
    
    const size_t EmptyBlockSize = BLOCK_SIZE;
    inFile.seekg(0, std::ios::end);
    uint64_t fileSize = inFile.tellg();
    inFile.seekg(0, std::ios::beg);
    
    std::vector<unsigned char> buf(EmptyBlockSize);
    uint64_t pos = 0;
    while (pos < fileSize) {
        size_t bytesToRead = EmptyBlockSize;
        if (pos + EmptyBlockSize > fileSize)
            bytesToRead = fileSize - pos;
        
        inFile.read(reinterpret_cast<char*>(buf.data()), bytesToRead);
        size_t bytesRead = inFile.gcount();
        if (bytesRead == 0)
            break;
        
        digest_t md5hash = m_md5.Calculate(buf.data(), bytesRead);
        std::string hashStr = fmt::format("{}", md5hash);
        //std::cout << "Flat block | pos: " << pos 
        //          << " | MD5: " << hashStr << std::endl;
        
        if (updateRepo(md5hash, file_id, pos, static_cast<uint32_t>(EmptyBlockSize), 0, 1)) {
            newHashInserted = true;
        } else {
            std::cout << "Flat block at pos " << pos 
                      << " not added (hash exists): " << hashStr << std::endl;
        }
        
        pos += bytesRead;
        
        if ((pos / EmptyBlockSize) % 256 == 0)
            std::cout << "Processed " << pos << " bytes..." << std::endl;
    }
    
    inFile.close();
    return newHashInserted;
}

void RepoIndexer::indexFiles(int mode, const std::vector<std::string>& files) {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    
    for (const auto& inputFile : files) {
        uint64_t file_id = gen();
        std::cout << "Processing file: " << inputFile 
                  << " | file_id: " << file_id << std::endl;
        
        bool newHash = false;
        if (mode == 0) {
            newHash = processFileMode0(inputFile, file_id);
        } else if (mode == 1) {
            newHash = processFileMode1(inputFile, file_id);
        } else {
            std::cerr << "Invalid mode: " << mode << std::endl;
        }
        
        if (newHash) {
            updateFileLookup(file_id, inputFile);
            std::cout << "File " << inputFile << " processed. New hashes added." << std::endl;
        } else {
            std::cout << "No block added for " << inputFile << ". File indexed." << std::endl;
        }
    }
    std::cout << "Repository processing finished." << std::endl;
}
