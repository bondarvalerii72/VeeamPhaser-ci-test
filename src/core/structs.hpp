#pragma once
#include <array>
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <spdlog/fmt/bundled/core.h>
#include "utils/common.hpp"
#include "Veeam/VBK.hpp"

using namespace Veeam::VBK;

#define LZ_START_MAGIC 0x0f800000f
// XXX VeeamAgent code:
// if ( (v4 & 0xF0FFFFFF) != 0xF000000F )
//     runtime_exc_t::ctor(pExceptionObject, "Failed to decompress LZ4 block: incorrect block header.");
//
// 0x8000000 is some flag

// {0xb6, 0xd8, 0x1b, 0x36, 0x0a, 0x56, 0x72, 0xd8, 0x0c, 0x27, 0x43, 0x0f, 0x39, 0x15, 0x3e, 0x2c}
static constexpr digest_t EMPTY_BLOCK_DIGEST = {0xd872560a361bd8b6ULL, 0x2c3e15390f43270cULL};
static constexpr digest_t ZERO_BLOCK_DIGEST = 0;

typedef std::vector<buf_t> VMeta;  

struct BlockStruct {
    uint64_t pos;
    uint32_t crc;
    uint32_t srcSize;
};

struct veEncrKey {
    std::string EncryptedKey;
    std::string TestPattern;
    std::string Salt;
    std::string DecryptedKey;
    std::string Key;
    std::string IV;
};

typedef std::array<uint8_t, 0x1000> ParsedMD;
typedef std::vector<std::vector<ParsedMD>> ParsedMds;

enum EBlockLocation : uint8_t {
    BL_NORMAL,
    BL_SPARSE,
    BL_RESERVED,
    BL_ARCHIVED,
    BL_BLOCK_IN_BLOB,
    BL_BLOCK_IN_BLOB_RESERVED
};

enum ECompType : uint8_t {
    CT_NONE = 0xff,
    CT_RLE = 2,
    CT_ZLIB_HI = 3,
    CT_ZLIB_LO = 4,
    CT_LZ4 = 7,
    CT_ZSTD3 = 8,
    CT_ZSTD9 = 9
};

#define VALID_COMP_TYPE(x) ((x) == CT_NONE || ((x) >= CT_RLE && (x) <= CT_ZSTD9))

// size = 0x3c, was named VHTEntry in VeeamBlaster, but it's BlockDescriptor in VeeamAgent
struct __attribute__((packed)) BlockDescriptor {
    EBlockLocation location;
    uint32_t usageCnt;
    uint64_t offset;
    uint32_t allocSize; // can be greater than BLOCK_SIZE !
    uint8_t dedup;
    digest_t digest;
    ECompType compType;
    uint8_t unused;
    uint32_t compSize;
    uint32_t srcSize;
    digest_t keysetID;

    bool operator==(const BlockDescriptor& other) const {
        return memcmp(this, &other, sizeof(BlockDescriptor)) == 0;
    }

    bool operator!=(const BlockDescriptor& other) const {
        return !(*this == other);
    }

    // XXX not sure about it
    bool valid() const {
        return location == BL_BLOCK_IN_BLOB && allocSize != 0 && allocSize >= compSize && (
            // XXX compSize may be greater than srcSize
            (digest  && compSize != 0 && srcSize != 0 && VALID_COMP_TYPE(compType)) ||
            (!digest && compSize == 0 && srcSize == 0 && compType == 0 && dedup == 0)
            );
    }

    bool empty() const {
        static const uint8_t z0[sizeof(BlockDescriptor)] = {0};
        static const uint8_t z1[sizeof(BlockDescriptor)] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

        return memcmp(this, z0, sizeof(BlockDescriptor)) == 0
            || memcmp(this, z1, sizeof(BlockDescriptor)) == 0;
    }

    std::string to_string() const {
        std::string result = fmt::format(
            "<BlockDescriptor location={:x}, usageCnt={:x}, offset={:x}, allocSize={:x}, dedup={:x}, digest={}, compType={:x}, compSize={:x}, srcSize={:x}",
            (int)location, usageCnt, offset, allocSize, dedup, digest, (uint8_t)compType, compSize, srcSize
            );
        if( unused ){
            result += fmt::format(" unused={:x}", unused);
        }
        if( keysetID ){
            result += fmt::format(" keysetID={}", keysetID);
        }
        return result + ">";
    }
};

typedef std::unordered_map<digest_t, BlockDescriptor> BlockDescriptors;

// size = 0x2e, SFibBlockDescriptorV7
typedef struct __attribute__((packed)) VBlockDesc {
    uint32_t size;
    uint8_t type;
    digest_t hash;
    uint64_t id;
    uint64_t vib_offset; // multiply by 0x100000 to get real offset
    uint64_t qw2;
    uint8_t t2;

    bool is_empty() const {
        return hash == EMPTY_BLOCK_DIGEST || hash == ZERO_BLOCK_DIGEST;
    }

    bool is_patch() const {
        return size == BLOCK_SIZE && vib_offset != 0;
    }

    bool is_zero() const {
        static const char zeroes[sizeof(VBlockDesc)] = {0};
        return memcmp(this, zeroes, sizeof(VBlockDesc)) == 0;
    }

    bool is_sparse() const {
        return is_zero();
    }

    std::string to_string() const {
        if (is_zero())
            return "<VBlockDesc zero>";

        return fmt::format(
            "<VBlockDesc size={:x}, type={:x}, hash={}, id={:x}, vib_offset={:x}, qw2={:x}, t2={:x}>",
            size, type, hash, id, vib_offset, qw2, t2
        );
    }
} VBlockDesc;

typedef std::vector<VBlockDesc> VAllBlocks;

struct FileDescriptor {
    PhysPageId startPPI;
    PhysPageId stopPPI;
    buf_t compBitmap;
    buf_t unCompBitmap;
    uint32_t compSize;
    uint32_t srcSize;
    uint64_t diskSize;
    uint32_t clustSize;
    uint64_t totalBlSize;
    buf_t genBitmap;
//    uint64_t arrMapStart;
    buf_t arrMap;
//    VMeta allMds;
    ParsedMds parsed;
//    VAllBlocks vBls;
//    uint32_t TotalRec;
//    uint32_t RemainRec;
//    std::vector<SMetaTableDescriptor> vUpblocks;
};

typedef std::vector<FileDescriptor> FileDescriptors;

// XXX wrongly aligned PhysPageId?
struct __attribute__((packed)) MidPid {
    uint32_t bank_id;
    uint32_t page_id;
};

struct __attribute__((packed)) lz_hdr {
    uint32_t magic; // LZ_START_MAGIC
    uint32_t crc;
    uint32_t srcSize;

    inline bool valid() const {
        return magic == LZ_START_MAGIC && srcSize > 0 && srcSize <= BLOCK_SIZE;
    }
};

struct EmptyHash {
    PhysPageId ppi;
    uint32_t srcOff;
    uint64_t dstOff;
};

typedef std::vector<EmptyHash> EmptyHashes;

struct RecoBlock {
    bool recoNeeded;
    bool recoDone;
    uint32_t bank_id;
    bool isVib;
    uint64_t bLow;
    uint64_t bHigh;
    uint64_t bLow2;
    uint64_t bHigh2;
    uint32_t metaPtr;
    buf_t* Meta;
    bool unneeded;
};

//struct RecoEntry {
//    uint32_t src_bank_id;
//    uint32_t src_page_id;
//    uint32_t bank_id;
//    uint32_t page_id;
//};

struct RecoCache {
    uint32_t bank_id;
//    std::vector<RecoEntry> recoEntries;
    bool skip;
};

//typedef uint8_t vhash[16]; 

//struct RecoHash {
//    vhash hash;
//    uint32_t bank_id;
//    uint32_t page_id;
//    bool htAvail;
//    bool blAvail;
//};

//typedef std::vector<RecoHash> RecoHashes;
typedef std::vector<RecoCache> RecoSupp;
typedef std::vector<RecoBlock> RecoBlocks;
