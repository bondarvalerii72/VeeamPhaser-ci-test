#include <cstdint>
#include <vector>
#include <string>
#include "utils/common.hpp"

// POD
#pragma pack(push, 1)
struct ErrorRecord {
    uint32_t offset;       // 4 bytes: offset of the problematic block
    uint32_t filePos;      // 4 bytes: current writing position in the output file
    uint32_t uncompSize;   // 4 bytes: expected uncompressed block size
    char errorCode;        // 1 byte: error code ('L' for LZ4 error, 'M' for LZMagic mismatch)
    digest_t hash;         // 16 bytes: MD5 hash of the block
};
#pragma pack(pop)

class ErrorLogger {
public:

    explicit ErrorLogger(const std::string& filename);
    void addRecord(uint32_t offset, uint32_t filePos, uint32_t uncompSize, char errorCode, const digest_t hash);
    bool saveToFile() const;
    bool loadFromFile();
    void clearRecords();

    const std::vector<ErrorRecord>& getRecords() const { return records; }

private:
    std::vector<ErrorRecord> records;
    std::string outputFilename;
};
