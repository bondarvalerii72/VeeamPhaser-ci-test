#pragma once
#include "utils/common.hpp"

struct VBlockDesc;
struct BlockDescriptor;

extern const uint8_t array1Skip[];
extern const uint8_t array0Skip[];

bool vGuessMetaIsVIB1(buf_t& vm, bool& isVib, uint64_t& blLow, uint64_t& blLow2, uint64_t& blHigh, uint64_t& blHigh2);
bool vGuessMetaID1(buf_t& vm, uint32_t& mpidOut);

struct VMetaCx {
    uint64_t Offset;
    uint64_t Size;
    uint32_t MID;
    uint64_t blLow;
    uint64_t blHigh;
    uint64_t PhyOffset;
    uint64_t blLow2;
    uint64_t blHigh2;
};

class Metaprocess {
public:
    Metaprocess(const std::string& edit2Text, const std::string& edit3Text);
    void Execute();

private:
    std::string edit2Text;
    std::string edit3Text;
    std::vector<std::string> szHt;
    
    bool CompareMem(const void* buf1, const void* buf2, size_t len);
    void szGetCsvN(const std::string& sz, std::vector<std::string>& ot);
    void szPutCsvN(const std::vector<std::string>& ot, std::string& sz);
    uint64_t Hex2Dec64(const std::string& hex);
    std::string IntToHex(uint64_t value, int width);
    std::string IntToStr(int64_t value);

    std::string m_csvPath;
    std::string m_dataPath;
    std::vector<VMetaCx> m_metaCx;
    std::vector<uint8_t> m_dummyBuffer;
    static constexpr uint8_t XBYTE = 0x00;

    void processMetadata();
    void readCSV();
    void processMID0Entries();
    std::string getHexString(uint64_t value, int width = 8);
    uint64_t hexToInt(const std::string& hex);
    std::vector<std::string> splitCSV(const std::string& line);
    void writeMetadataBlock(const std::string& outputPath, uint64_t offset, uint32_t size);
    void appendDummyBlock(std::ofstream& outFile);
    std::string getOutputPath(uint64_t physOffset);
};
