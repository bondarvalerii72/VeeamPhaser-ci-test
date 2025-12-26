/**
 * @file Carver.cpp
 * @brief Implementation of the Carver class for extracting data blocks from VIB/VBK files.
 *
 * This file provides functionality to carve (extract) LZ4 compressed data blocks and
 * metadata blocks from Veeam backup files. It scans for block signatures, validates
 * blocks, decompresses them, and outputs block information to CSV files for later use
 * in file reconstruction or repair operations.
 */

#include <cstring>
#include <iomanip>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <chrono>
#include "lz4.h"

#include "core/CMeta.hpp"
#include "Carver.hpp"
#include "io/Reader.hpp"
#include "utils/units.hpp"

extern "C" {
    uint32_t vcrc32(uint32_t crc, const void *buf, unsigned int len);
}

/**
 * @brief Constructs a Carver and initializes internal buffers.
 */
Carver::Carver() 
    #ifdef _WIN32
        : fd(INVALID_HANDLE_VALUE)
    #else
        : fd(-1)
    #endif
    {
    buf.resize(BLOCK_SIZE_CARVER * 2);
    prevBuf.resize(BLOCK_SIZE_CARVER * 2);
    lzBuf2.resize(BLOCK_SIZE_CARVER * 2);
    std::memset(prevBuf.data(), 0, prevBuf.size());
}

Carver::~Carver() {
    #ifdef _WIN32
        if (fd != INVALID_HANDLE_VALUE) {
            CloseHandle(fd);
        }
    #else
        if (fd != -1) {
            close(fd);
        }
    #endif
    
    if (fOut.is_open()) fOut.close();
    if (fOutM.is_open()) fOutM.close();
}

bool Carver::OpenInput(const std::string& path, int64_t offset) {
    startOffset = offset;

    return OpenFile(path);
}

bool Carver::OpenFile(const std::string& filePath) {
    #ifdef _WIN32
        if (filePath.substr(0, 4) == "\\\\.\\") {
            fd = CreateFileA(
                filePath.c_str(),
                GENERIC_READ,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                NULL,
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL,
                NULL
            );
            
            if (fd == INVALID_HANDLE_VALUE) {
                throw std::runtime_error("Failed to open physical drive: " + std::to_string(GetLastError()));
            }
        } else {
            fd = CreateFileA(
                filePath.c_str(), 
                GENERIC_READ,
                FILE_SHARE_READ,
                NULL,
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL,
                NULL
            );
        }
        
        if (fd == INVALID_HANDLE_VALUE) {
            throw std::runtime_error("Failed to open file: " + std::to_string(GetLastError()));
        }
        
        LARGE_INTEGER size;
        if (!GetFileSizeEx(fd, &size)) {
            DISK_GEOMETRY_EX geometry;
            DWORD bytesReturned;
            if (!DeviceIoControl(
                    fd,
                    IOCTL_DISK_GET_DRIVE_GEOMETRY_EX,
                    NULL,
                    0,
                    &geometry,
                    sizeof(geometry),
                    &bytesReturned,
                    NULL)) {
                throw std::runtime_error("Failed to get drive geometry: " + std::to_string(GetLastError()));
            }
            diskSize = geometry.DiskSize.QuadPart;
        } else {
            diskSize = size.QuadPart;
        }
    #else
        fd = open(filePath.c_str(), O_RDONLY);
        if (fd == -1) {
            throw std::runtime_error("Failed to open file: " + std::string(strerror(errno)));
        }
        diskSize = Reader::get_size(filePath);
    #endif

    return SetFilePointerEx(fd, startOffset);
}

bool Carver::SetFilePointerEx(
    #ifdef _WIN32
        HANDLE fileHandle,
    #else
        int fileHandle,
    #endif
    int64_t offset) {
    
    #ifdef _WIN32
        LARGE_INTEGER li;
        li.QuadPart = offset;
        return ::SetFilePointerEx(fileHandle, li, NULL, FILE_BEGIN) != 0;
    #else
        return lseek(fileHandle, offset, SEEK_SET) != -1;
    #endif
}

bool Carver::OpenOutputFiles(const std::string& baseOutputPath) {
    m_basePath = baseOutputPath;
    m_metaPath = baseOutputPath.substr(0, baseOutputPath.find_last_of('.')) + "-meta.csv";

    #ifdef _WIN32
        fOut.open(baseOutputPath, std::ios::out | std::ios::binary);
        fOutM.open(m_metaPath, std::ios::out | std::ios::binary);
    #else
        fOut.open(baseOutputPath);
        fOutM.open(m_metaPath);
    #endif
    
    if (!fOut.is_open() || !fOutM.is_open()) {
        throw std::runtime_error("Failed to open output files");
    }
    
    return true;
}

std::string Carver::CalculateMD5(const unsigned char* data, size_t length) {
    digest_t digest = m_md5.Calculate(data, length);
    return fmt::format("{}", digest);
}

void Carver::Process() {
    bool firstRead = true;
    uint32_t flip = 0;

    while (diskReaden < diskSize) {
        size_t toRead = std::min(static_cast<size_t>(BLOCK_SIZE_CARVER), 
                                static_cast<size_t>(diskSize - diskReaden));
                                
        #ifdef _WIN32
            DWORD bytesRead;
            if (!ReadFile(fd, buf.data() + flip, toRead, &bytesRead, NULL)) {
                throw std::runtime_error("Read failed: " + std::to_string(GetLastError()));
            }
            if (bytesRead == 0) {
                break;
            }
        #else
            ssize_t bytesRead = read(fd, buf.data() + flip, toRead);
            if (bytesRead <= 0) {
                break;
            }
        #endif

        // Start processing a little before the end of the last block so you can catch patterns between blocks
        uint32_t startPos;
        if (firstRead) {
            startPos = 0;
            firstRead = false;
        } else {
            // (V_BLOCK_SIZE + 0xC) is the max size he can hit.
            startPos = flip - (V_BLOCK_SIZE + sizeof(lz_hdr));
        }

        for (uint32_t i = startPos; i < flip + BLOCK_SIZE_CARVER - sizeof(LZ_START_MAGIC); i++) {
            while (m_find_data_blocks) {
                const lz_hdr* plz = (const lz_hdr*)(&buf[i]);
                if (!plz->valid())
                    break;

                uint64_t qOffset = diskReaden + i + startOffset - flip;

                if (i + (V_BLOCK_SIZE - sizeof(lz_hdr)) <= buf.size()) {
                    int lz4res = LZ4_decompress_safe(
                        (const char*)(plz+1),
                        (char*)lzBuf2.data(),
                        V_BLOCK_SIZE - sizeof(lz_hdr),
                        lzBuf2.size()
                        );

                    uint32_t crc = vcrc32(0, lzBuf2.data(), plz->srcSize);

                    if (plz->crc == crc) {
                        std::stringstream ss;
                        ss << IntToHex(qOffset) << ";"
                            << IntToHex(lz4res & 0xFFFFFFFF, 4) << ";"
                            << IntToHex(plz->srcSize, 8) << ";"
                            << CalculateMD5(lzBuf2.data(), plz->srcSize) << ";"
                            << IntToHex(plz->crc, 8) << "\r\n";
                        szBuf += ss.str();
                        m_iter_data_blocks_found++;
                    }
                }
                break;
            }
            
            if (m_find_empty_blocks) {
                if (i + sizeof(EMPTY_BLOCK_DIGEST) <= buf.size() && *(digest_t*)(&buf[i]) == EMPTY_BLOCK_DIGEST) {
                    szBufM += "M;" + IntToHex(diskReaden + i + startOffset) + "\r\n";
                    m_iter_empty_blocks_found++;
                }
            }
        }
        
        diskReaden += bytesRead;
        if (diskReaden % (BLOCK_SIZE_CARVER * STEP_SIZE) == 0) {
            WriteResults();
            UpdateProgress();
        }

        // Cp the current block to the first half of the buffer.
        std::memcpy(buf.data(), buf.data() + flip, BLOCK_SIZE_CARVER);
        flip = BLOCK_SIZE_CARVER;
    }
    WriteResults();
    UpdateProgress();
}

void Carver::WriteResults() {
    if (!szBuf.empty()) {
        fOut << szBuf;
        szBuf.clear();
    }
    if (!szBufM.empty()) {
        fOutM << szBufM;
        szBufM.clear();
    }
    fOut.flush();
    fOutM.flush();
}

void Carver::UpdateProgress() {
    // auto now = std::chrono::steady_clock::now();
    // auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
    //     now.time_since_epoch()).count();
    // 
    // tk1 = tk2;
    // tk2 = duration;
    
    //uint64_t timeDiff = (tk2 > tk1) ? (tk2 - tk1) : 1; // zero_div :c
    
    std::cout << "Carved " << bytes2human(diskReaden) 
              << " out of " << bytes2human(diskSize)
              //<< " " << PFU_ConvertFSizeToStr((STEP_BLOCK / timeDiff) * 1000)
              << " @ " << IntToHex(diskReaden) << std::endl;
    
    if (m_iter_data_blocks_found > 0 || m_iter_empty_blocks_found > 0) {
        std::cout << "|---- VEEAM Data Blocks Found: " << m_iter_data_blocks_found 
                  << ", Empty Blocks Found: " << m_iter_empty_blocks_found << " -----|" << std::endl;
    }
    
    m_total_empty_blocks_found += m_iter_empty_blocks_found;
    m_total_data_blocks_found += m_iter_data_blocks_found;
    m_iter_empty_blocks_found = 0;
    m_iter_data_blocks_found = 0;
}

std::string Carver::IntToHex(uint64_t value, int width) {
    std::stringstream ss;
    ss << std::uppercase << std::setfill('0') << std::setw(width) << std::hex << value;
    return ss.str();
}

std::vector<std::string> Carver::stats() const {
    std::vector<std::string> stats;
    stats.push_back(fmt::format("saved {} data blocks to {}", m_total_data_blocks_found, m_basePath));
    stats.push_back(fmt::format("saved {} empty blocks to {}", m_total_empty_blocks_found, m_metaPath));
    return stats;
}
