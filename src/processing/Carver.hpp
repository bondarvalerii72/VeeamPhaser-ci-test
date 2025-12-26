#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/types.h>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <winioctl.h>
#endif

#include "MD5.hpp"

class Carver {
public:
    Carver();
    ~Carver();

    bool OpenInput(const std::string& path, int64_t offset);
    bool OpenOutputFiles(const std::string& baseOutputPath);
    void Process();
    std::vector<std::string> stats() const;

private:
    bool OpenFile(const std::string& filePath);
    bool SetFilePointerEx(
    #ifdef _WIN32
        HANDLE fileHandle,
    #else
        int fileHandle, 
    #endif
    int64_t offset);

    void WriteResults();
    void UpdateProgress();
    std::string IntToHex(uint64_t value, int width = 8);
    std::string PFU_ConvertFSizeToStr(uint64_t size);
    std::string CalculateMD5(const unsigned char* data, size_t length);

    std::ofstream fOut, fOutM;
    buf_t buf, prevBuf, lzBuf2;

    #ifdef _WIN32
        HANDLE fd;
    #else
        int fd;
    #endif

    bool m_find_empty_blocks = true;
    bool m_find_data_blocks = true;
    std::string m_basePath;
    std::string m_metaPath;

    uint64_t diskSize = 0;
    uint64_t diskReaden = 0;
    
    uint64_t m_iter_empty_blocks_found = 0;
    uint64_t m_iter_data_blocks_found = 0;
    uint64_t m_total_empty_blocks_found = 0;
    uint64_t m_total_data_blocks_found = 0;
    // uint64_t tk1 = 0, tk2 = 0;
    int64_t startOffset = 0;
    std::string szBuf, szBufM;
    MD5 m_md5;

    // Constants
    static constexpr uint32_t BLOCK_SIZE_CARVER = 0x2000000;
    static constexpr uint32_t STEP_SIZE = 64;
    static constexpr uint32_t STEP_BLOCK = BLOCK_SIZE_CARVER * STEP_SIZE;
    static constexpr uint32_t V_BLOCK_SIZE = 0x110000;
};
